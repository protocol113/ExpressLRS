#include "FreqStageNegotiation.h"
#include <string.h>

#if defined(RUNTIME_FREQ_DEBUG) && !defined(UNIT_TEST)
#include "logging.h"
#define FREQ_DBG(fmt, ...) DBGLN("[FREQ] " fmt, ##__VA_ARGS__)
#else
#define FREQ_DBG(...) do {} while (0)
#endif

// --- Lead-time ------------------------------------------------------------

uint32_t FreqStageComputeLeadNonces(uint8_t tlmDenom)
{
    // STAGE payload is 28 B; Stubborn fragments it into ~5 chunks, each
    // delivered one-per-telemetry-slot. ACK is 1 chunk on the downlink.
    // Worst-case round trip: (5 + 1) chunks * tlmDenom packets per chunk.
    // 3x headroom covers occasional Stubborn resync + retransmit.
    // Floor at 500 packets (~1 s @ 500 Hz, ~10 s @ 50 Hz binding) so even
    // 1:1 TLM has margin and ACK has time to arrive before epoch.
    const uint32_t chunksRoundTrip = 6;
    const uint32_t headroom        = 3;
    const uint32_t floorNonces     = 500;

    uint32_t denom = tlmDenom == 0 ? 1 : tlmDenom;
    uint32_t lead  = chunksRoundTrip * denom * headroom;
    return lead < floorNonces ? floorNonces : lead;
}

// --- Msg builder ----------------------------------------------------------

FreqStageMsg FreqStageBuildMsgFromConfig(const FHSSFreqConfig *cfg, uint32_t currentNonce, uint8_t tlmDenom)
{
    FreqStageMsg m{};
    m.schema_version   = FREQ_STAGE_SCHEMA_VERSION;
    if (cfg == nullptr) return m;  // caller validates; return zero msg

    m.flags            = FREQ_STAGE_FLAG_HAS_PRIMARY;
    m.primary_start_hz = cfg->params.freq_start;
    m.primary_stop_hz  = cfg->params.freq_stop;
    m.primary_count    = cfg->params.freq_count;
    m.primary_sync     = cfg->params.sync_channel;
    // Dual-band deferred to PR 7; zero fields stay zero.
    m.epoch_nonce      = currentNonce + FreqStageComputeLeadNonces(tlmDenom);
    return m;
}

// --- RX-side glue ---------------------------------------------------------

static uint32_t g_lastAcceptedEpoch  = 0;
static bool     g_hasAcceptedAnEpoch = false;

void FreqStageRxResetDuplicateCache(void)
{
    g_lastAcceptedEpoch  = 0;
    g_hasAcceptedAnEpoch = false;
}

FreqStageAckStatus FreqStageRxHandleStage(const uint8_t *payload,
                                          uint32_t      payloadLen,
                                          uint32_t      uidSeed,
                                          uint32_t      currentNonce,
                                          FreqStageMsg *outMsg)
{
    FreqStageMsg msg{};
    FreqStageAckStatus status = decodeFreqStageStatus(payload, payloadLen, &msg);
    if (outMsg != nullptr) *outMsg = msg;
    if (status != FREQ_ACK_OK) { FREQ_DBG("rx stage decode failed status=%u", status); return status; }

    // Idempotent duplicate: same epoch already staged/swapped. Re-ACK, no
    // state-machine mutation. Protects against Stubborn retransmits and
    // late duplicates arriving after the original already swapped.
    if (g_hasAcceptedAnEpoch && msg.epoch_nonce == g_lastAcceptedEpoch)
    {
        return FREQ_ACK_OK;
    }

    // Epoch must be in the future relative to current nonce. Reject stale
    // stages (e.g., from a ghost retransmit after a rebind).
    if (msg.epoch_nonce <= currentNonce) return FREQ_ACK_BAD_VERSION;

    // PR 3.5 scope: primary band only. Dual-band STAGE → UNSUPPORTED.
    if (msg.flags & FREQ_STAGE_FLAG_HAS_DUALBAND) return FREQ_ACK_UNSUPPORTED;
    if (!(msg.flags & FREQ_STAGE_FLAG_HAS_PRIMARY)) return FREQ_ACK_UNSUPPORTED;

    // Build into the STAGED pool slot.
    FHSSFreqConfig *staged = FHSSgetPoolSlot(FHSS_SLOT_STAGED);
    if (staged == nullptr) return FREQ_ACK_BUILD_FAILED;

    FHSSFreqParams params{};
    // Hz → register-value conversion happens at this boundary (per wire
    // protocol contract: Hz on the air, reg-val in memory).
    params.freq_start   = FREQ_HZ_TO_REG_VAL(msg.primary_start_hz);
    params.freq_stop    = FREQ_HZ_TO_REG_VAL(msg.primary_stop_hz);
    params.freq_count   = msg.primary_count;
    params.sync_channel = msg.primary_sync;
    // Name is TX-UX only; carry a generic marker. RX never surfaces it.
    strncpy(params.name, "STAGED", FHSS_FREQ_NAME_MAXLEN - 1);
    params.name[FHSS_FREQ_NAME_MAXLEN - 1] = '\0';

    if (!FHSSbuildConfig(staged, &params, uidSeed)) return FREQ_ACK_BUILD_FAILED;

    // RX stages with requireAck=false: receiving STAGE IS the proof.
    if (!FHSSstageConfig(staged, msg.epoch_nonce, /*requireAck=*/false))
    {
        return FREQ_ACK_BUILD_FAILED;
    }

    g_lastAcceptedEpoch  = msg.epoch_nonce;
    g_hasAcceptedAnEpoch = true;
    FREQ_DBG("rx stage OK name=%.16s count=%u epoch=%u",
             params.name, (unsigned)params.freq_count, (unsigned)msg.epoch_nonce);
    return FREQ_ACK_OK;
}

// --- TX-side production send path -----------------------------------------

#if !defined(UNIT_TEST)

#include "CRSFRouter.h"
#include "msp.h"
#include "msptypes.h"

static bool txSendRxtxConfig(uint8_t subcmd, const uint8_t *payload, uint8_t payloadLen)
{
    mspPacket_t msp;
    msp.reset();
    msp.makeCommand();
    msp.function = MSP_ELRS_RXTX_CONFIG;
    msp.addByte(subcmd);
    for (uint8_t i = 0; i < payloadLen; i++) msp.addByte(payload[i]);
    crsfRouter.AddMspMessage(&msp, CRSF_ADDRESS_CRSF_RECEIVER, CRSF_ADDRESS_CRSF_TRANSMITTER);
    return true;
}

bool FreqStageSendStage(const FHSSFreqConfig *cfg, uint32_t currentNonce, uint8_t tlmDenom)
{
    if (cfg == nullptr) return false;

    FreqStageMsg msg = FreqStageBuildMsgFromConfig(cfg, currentNonce, tlmDenom);
    uint8_t buf[FREQ_STAGE_WIRE_LEN];
    if (encodeFreqStage(&msg, buf, sizeof(buf)) != FREQ_STAGE_WIRE_LEN) return false;

    if (!txSendRxtxConfig((uint8_t)MSP_ELRS_RXTX_CONFIG_SUBCMD::FREQ_STAGE, buf, sizeof(buf))) return false;

    FREQ_DBG("tx send stage name=%s nonce=%u epoch=%u tlm=%u",
             cfg->params.name, (unsigned)currentNonce, (unsigned)msg.epoch_nonce, (unsigned)tlmDenom);

    // Record local state. TX uses requireAck=true — will only swap at
    // epoch if FHSSnotifyAckReceived(msg.epoch_nonce) is called first.
    return FHSSstageConfig(cfg, msg.epoch_nonce, /*requireAck=*/true);
}

bool FreqStageSendAbort(void)
{
    FreqAbort abort{FREQ_STAGE_SCHEMA_VERSION, 0};
    uint8_t buf[FREQ_ABORT_WIRE_LEN];
    if (encodeFreqAbort(&abort, buf, sizeof(buf)) != FREQ_ABORT_WIRE_LEN) return false;
    txSendRxtxConfig((uint8_t)MSP_ELRS_RXTX_CONFIG_SUBCMD::FREQ_ABORT, buf, sizeof(buf));
    FHSSabortStagedConfig();
    return true;
}

#endif // !UNIT_TEST

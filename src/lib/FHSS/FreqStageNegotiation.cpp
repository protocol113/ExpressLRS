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
    // ELRS OtaNonce is uint8_t and wraps every 256 packets. The swap-epoch
    // comparison in FHSSactivateIfEpochReached uses wrap-aware 8-bit math
    // which requires the lead to stay under 128 packets. Cap at 120 for
    // safety margin.
    //
    // At 100 Hz: 120 packets = 1.2s. At 500 Hz: 240ms.
    // Stubborn round-trip: ~(chunks * tlmDenom) packets per direction.
    // This fits low tlm ratios (1:2..1:8) at typical rates. Higher tlm
    // ratios (1:32+) may exceed this window and time out — that's a
    // known limitation of the 8-bit nonce; lifting it requires a 32-bit
    // nonce extension (tracked for a future PR).
    const uint32_t chunksRoundTrip = 6;
    const uint32_t headroom        = 2;
    const uint32_t floorNonces     = 60;
    const uint32_t ceilNonces      = 120;

    uint32_t denom = tlmDenom == 0 ? 1 : tlmDenom;
    uint32_t lead  = chunksRoundTrip * denom * headroom;
    if (lead < floorNonces) lead = floorNonces;
    if (lead > ceilNonces)  lead = ceilNonces;
    return lead;
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
    if (cfg->has_dualband)
    {
        m.flags       |= FREQ_STAGE_FLAG_HAS_DUALBAND;
        m.db_start_hz  = cfg->db_params.freq_start;
        m.db_stop_hz   = cfg->db_params.freq_stop;
        m.db_count     = cfg->db_params.freq_count;
        m.db_sync      = cfg->db_params.sync_channel;
    }
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

    // Epoch must be in the future relative to current nonce. Use the same
    // wrap-aware int8 delta trick as FHSSactivateIfEpochReached — OtaNonce
    // is uint8_t (wraps every 256 packets). A plain uint32 `<=` compare
    // over-rejects after the RX's OtaNonce has wrapped past the epoch
    // value stored in msg.epoch_nonce (which is the raw wire uint32
    // `currentNonce + lead` from TX, unsigned-wrap-agnostic). In the
    // wrap-aware frame, "future" means delta of (epoch - current) in
    // [0, 127]; negative delta = stale.
    {
        int8_t epochDelta = (int8_t)((uint8_t)msg.epoch_nonce - (uint8_t)currentNonce);
        if (epochDelta <= 0) return FREQ_ACK_BAD_VERSION;
    }

    // Caller must indicate at least one band; empty STAGE is malformed.
    if (!(msg.flags & (FREQ_STAGE_FLAG_HAS_PRIMARY | FREQ_STAGE_FLAG_HAS_DUALBAND)))
        return FREQ_ACK_UNSUPPORTED;

    // Build into the STAGED pool slot. PR 7: when the STAGE carries only
    // one band's changes, copy the OTHER band's params from the currently
    // active config so FHSSbuildConfig can rebuild both sequences with
    // identical params — the unchanged side produces a bit-identical
    // sequence (deterministic on UID seed), so mirroring it back is a
    // no-op behavior-wise but keeps the on-chip state coherent.
    FHSSFreqConfig *staged = FHSSgetPoolSlot(FHSS_SLOT_STAGED);
    if (staged == nullptr) return FREQ_ACK_BUILD_FAILED;

    const FHSSFreqConfig *active = FHSSgetActiveConfig();

    FHSSFreqParams primary{};
    if (msg.flags & FREQ_STAGE_FLAG_HAS_PRIMARY)
    {
        // Hz → register-value conversion happens at this boundary (per wire
        // protocol contract: Hz on the air, reg-val in memory).
        primary.freq_start   = FREQ_HZ_TO_REG_VAL(msg.primary_start_hz);
        primary.freq_stop    = FREQ_HZ_TO_REG_VAL(msg.primary_stop_hz);
        primary.freq_count   = msg.primary_count;
        primary.sync_channel = msg.primary_sync;
    }
    else if (active != nullptr)
    {
        primary = active->params;
    }
    else
    {
        return FREQ_ACK_UNSUPPORTED;  // primary unchanged + no active to copy
    }
    // Name is TX-UX only; carry a generic marker. RX never surfaces it.
    strncpy(primary.name, "STAGED", FHSS_FREQ_NAME_MAXLEN - 1);
    primary.name[FHSS_FREQ_NAME_MAXLEN - 1] = '\0';

    FHSSFreqParams dbParams{};
    bool hasDb = false;
    if (msg.flags & FREQ_STAGE_FLAG_HAS_DUALBAND)
    {
        dbParams.freq_start   = FREQ_HZ_TO_REG_VAL(msg.db_start_hz);
        dbParams.freq_stop    = FREQ_HZ_TO_REG_VAL(msg.db_stop_hz);
        dbParams.freq_count   = msg.db_count;
        dbParams.sync_channel = msg.db_sync;
        strncpy(dbParams.name, "STAGED_DB", FHSS_FREQ_NAME_MAXLEN - 1);
        dbParams.name[FHSS_FREQ_NAME_MAXLEN - 1] = '\0';
        hasDb = true;
    }
    else if (active != nullptr && active->has_dualband)
    {
        dbParams = active->db_params;
        hasDb = true;
    }

    if (!FHSSbuildConfig(staged, &primary, uidSeed, hasDb ? &dbParams : nullptr))
        return FREQ_ACK_BUILD_FAILED;

    // RX stages with requireAck=false: receiving STAGE IS the proof.
    if (!FHSSstageConfig(staged, msg.epoch_nonce, /*requireAck=*/false))
    {
        return FREQ_ACK_BUILD_FAILED;
    }

    g_lastAcceptedEpoch  = msg.epoch_nonce;
    g_hasAcceptedAnEpoch = true;
    FREQ_DBG("rx stage OK pri=%.16s count=%u db=%d epoch=%u",
             primary.name, (unsigned)primary.freq_count,
             (int)hasDb, (unsigned)msg.epoch_nonce);
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

    // Single send. The uplink MSP path feeds through TXOTAConnector which
    // hands the buffer to DataUlSender (StubbornSender) — that layer
    // already handles reliable chunked delivery with RX-confirmed retry.
    // An earlier version retransmitted 5x from here, but that just queued
    // duplicate copies behind the StubbornSender's existing retry loop,
    // delaying rather than reinforcing delivery. Caveat: forwardMessage
    // only queues when connectionState == connected; if the caller hits
    // Apply mid-dropout the STAGE is silently dropped. That's acceptable
    // here — swap on dropped link is fragile anyway; recovery comes from
    // persistence + boot-to-stored-state (PR 4/5).
    if (!txSendRxtxConfig((uint8_t)MSP_ELRS_RXTX_CONFIG_SUBCMD::FREQ_STAGE, buf, sizeof(buf))) return false;

    FREQ_DBG("tx send stage name=%s nonce=%u epoch=%u tlm=%u",
             cfg->params.name, (unsigned)currentNonce, (unsigned)msg.epoch_nonce, (unsigned)tlmDenom);

    // requireAck=false: same swap-at-epoch contract as RX. At high tlm
    // denoms the downlink MSP ACK round-trip exceeds our 120-nonce epoch
    // budget (8-bit OtaNonce wrap limit), so requiring the ACK caused
    // TX-abort-alone failures. Safety net is the 1.5s watchdog on both
    // sides: if either side gets no valid packets on the new band, it
    // reverts to rendezvous. RX stages the same way.
    return FHSSstageConfig(cfg, msg.epoch_nonce, /*requireAck=*/false);
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

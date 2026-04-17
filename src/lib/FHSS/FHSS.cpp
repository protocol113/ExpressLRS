#include "FHSS.h"
#include "logging.h"
#include "options.h"
#include <string.h>

#if defined(UNIT_TEST)
#define POWER_OUTPUT_VALUES_COUNT 4
#define POWER_OUTPUT_VALUES_DUAL_COUNT 0
#endif

#if defined(RADIO_SX127X) || defined(RADIO_LR1121)

#if defined(RADIO_LR1121)
#include "LR1121Driver.h"
#else
#include "SX127xDriver.h"
#endif

const fhss_config_t domains[] = {
    {"AU915",  FREQ_HZ_TO_REG_VAL(915500000), FREQ_HZ_TO_REG_VAL(926900000), 20, 921000000},
    {"FCC915", FREQ_HZ_TO_REG_VAL(903500000), FREQ_HZ_TO_REG_VAL(926900000), 40, 915000000},
    {"EU868",  FREQ_HZ_TO_REG_VAL(863275000), FREQ_HZ_TO_REG_VAL(869575000), 13, 868000000},
    {"IN866",  FREQ_HZ_TO_REG_VAL(865375000), FREQ_HZ_TO_REG_VAL(866950000), 4, 866000000},
    {"AU433",  FREQ_HZ_TO_REG_VAL(433420000), FREQ_HZ_TO_REG_VAL(434420000), 3, 434000000},
    {"EU433",  FREQ_HZ_TO_REG_VAL(433100000), FREQ_HZ_TO_REG_VAL(434450000), 3, 434000000},
    {"US433",  FREQ_HZ_TO_REG_VAL(433250000), FREQ_HZ_TO_REG_VAL(438000000), 8, 434000000},
    {"US433W",  FREQ_HZ_TO_REG_VAL(423500000), FREQ_HZ_TO_REG_VAL(438000000), 20, 434000000},
};

#if defined(RADIO_LR1121)
const fhss_config_t domainsDualBand[] = {
    {
    #if defined(Regulatory_Domain_EU_CE_2400)
        "CE_LBT",
    #else
        "ISM2G4",
    #endif
    FREQ_HZ_TO_REG_VAL(2400400000), FREQ_HZ_TO_REG_VAL(2479400000), 80, 2440000000}
};
#endif

#elif defined(RADIO_SX128X)
#include "SX1280Driver.h"

const fhss_config_t domains[] = {
    {
    #if defined(Regulatory_Domain_EU_CE_2400)
        "CE_LBT",
    #elif defined(Regulatory_Domain_ISM_2400)
        "ISM2G4",
    #endif
    FREQ_HZ_TO_REG_VAL(2400400000), FREQ_HZ_TO_REG_VAL(2479400000), 80, 2440000000}
};
#endif

// Our table of FHSS frequencies. Define a regulatory domain to select the correct set for your location and radio
const fhss_config_t *FHSSconfig;
const fhss_config_t *FHSSconfigDualBand;

// Actual sequence of hops as indexes into the frequency list
uint8_t FHSSsequence[FHSS_SEQUENCE_LEN];
uint8_t FHSSsequence_DualBand[FHSS_SEQUENCE_LEN];

// Which entry in the sequence we currently are on
uint8_t volatile FHSSptr;

// Channel for sync packets and initial connection establishment
uint_fast8_t sync_channel;
uint_fast8_t sync_channel_DualBand;

// Offset from the predefined frequency determined by AFC on Team900 (register units)
int32_t FreqCorrection;
int32_t FreqCorrection_2;

// Frequency hop separation
uint32_t freq_spread;
uint32_t freq_spread_DualBand;

// Variable for Dual Band radios
bool FHSSusePrimaryFreqBand = true;
bool FHSSuseDualBand = false;

uint16_t primaryBandCount;
uint16_t secondaryBandCount;

constexpr uint8_t VERSION_DOMAIN_MAXLEN = 26 + 1;   // max. number of characters (plus '\0') the Lua script can display
                                                    // on color LCD radios w/o being overwritten by the commit info
char version_domain[VERSION_DOMAIN_MAXLEN] {};


static void initRendezvousFromLegacy(uint32_t seed);  // v2; defined below

void FHSSrandomiseFHSSsequence(const uint32_t seed)
{
    FHSSconfig = &domains[firmwareOptions.domain];
    sync_channel = FHSSconfig->freq_count / 2;
    freq_spread = (FHSSconfig->freq_stop - FHSSconfig->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfig->freq_count - 1);
    primaryBandCount = (FHSS_SEQUENCE_LEN / FHSSconfig->freq_count) * FHSSconfig->freq_count;

    DBGLN("Primary Domain %s, %u channels, sync=%u",
        FHSSconfig->domain, FHSSconfig->freq_count, sync_channel);

    FHSSrandomiseFHSSsequenceBuild(seed, FHSSconfig->freq_count, sync_channel, FHSSsequence);

#if defined(RADIO_LR1121)
    FHSSconfigDualBand = &domainsDualBand[0];
    sync_channel_DualBand = FHSSconfigDualBand->freq_count / 2;
    freq_spread_DualBand = (FHSSconfigDualBand->freq_stop - FHSSconfigDualBand->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfigDualBand->freq_count - 1);
    secondaryBandCount = (FHSS_SEQUENCE_LEN / FHSSconfigDualBand->freq_count) * FHSSconfigDualBand->freq_count;

    DBGLN("Dual Domain %s, %u channels, sync=%u",
        FHSSconfigDualBand->domain, FHSSconfigDualBand->freq_count, sync_channel_DualBand);

    FHSSusePrimaryFreqBand = false;
    FHSSrandomiseFHSSsequenceBuild(seed, FHSSconfigDualBand->freq_count, sync_channel_DualBand, FHSSsequence_DualBand);
    FHSSusePrimaryFreqBand = true;
#endif

    // add frequency and regulatory domain to the string used by the Lua script
    addDomainInfo(version_domain, VERSION_DOMAIN_MAXLEN);

    // v2: snapshot the compile-time domain as the rendezvous config so future
    // FHSSstageConfig / FHSSrevertToRendezvous calls have a known home.
    initRendezvousFromLegacy(seed);
}

/**
Requirements:
1. 0 every n hops
2. No two repeated channels
3. Equal occurance of each (or as even as possible) of each channel
4. Pseudorandom

Approach:
  Fill the sequence array with the sync channel every FHSS_FREQ_CNT
  Iterate through the array, and for each block, swap each entry in it with
  another random entry, excluding the sync channel.

*/
void FHSSrandomiseFHSSsequenceBuild(const uint32_t seed, uint32_t freqCount, uint_fast8_t syncChannel, uint8_t *inSequence)
{
    // reset the pointer (otherwise the tests fail)
    FHSSptr = 0;
    rngSeed(seed);

    // initialize the sequence array
    for (uint16_t i = 0; i < FHSSgetSequenceCount(); i++)
    {
        if (i % freqCount == 0) {
            inSequence[i] = syncChannel;
        } else if (i % freqCount == syncChannel) {
            inSequence[i] = 0;
        } else {
            inSequence[i] = i % freqCount;
        }
    }

    for (uint16_t i = 0; i < FHSSgetSequenceCount(); i++)
    {
        // if it's not the sync channel
        if (i % freqCount != 0)
        {
            uint8_t offset = (i / freqCount) * freqCount;   // offset to start of current block
            uint8_t rand = rngN(freqCount - 1) + 1;         // random number between 1 and FHSS_FREQ_CNT

            // switch this entry and another random entry in the same block
            uint8_t temp = inSequence[i];
            inSequence[i] = inSequence[offset+rand];
            inSequence[offset+rand] = temp;
        }
    }

    // output FHSS sequence
    // for (uint16_t i=0; i < FHSSgetSequenceCount(); i++)
    // {
    //     DBG("%u ",inSequence[i]);
    //     if (i % 10 == 9)
    //         DBGCR;
    // }
    // DBGCR;
}

/**
 * @brief Add frequency and regulatory domain to the version string used by the Lua script. Outputs the version_domain string as:
 * [version:0..20] [subGHz domain | 2.4GHz domain] truncated to maxlen-1 for single band devices
 * [version:0..20] [subGHz domain]/[2.4GHz domain] truncated to maxlen-1 for dual band devices
 * Examples:
 *   4.0.0 CE_LBT
 *   4.1.7 AU915
 *   4.11.17 FCC915/ISM2G4
 *   someBranch EU868/CE_LBT
 *
 * @param version_domain a pointer to a buffer holding the version and extra space for additional data
 * @param maxlen the size of the provided buffer
 */
void addDomainInfo(char *version_domain, uint8_t maxlen)
{
    if (strlen(version) < 21)
    {
        strlcpy(version_domain, version, 21);
        strlcat(version_domain, " ", maxlen);
    }
    else
    {
        strlcpy(version_domain, version, 18);
        strlcat(version_domain, "... ", maxlen);
    }

    if (POWER_OUTPUT_VALUES_COUNT != 0)
    {
        strlcat(version_domain, FHSSconfig->domain, maxlen);            // single band: subghz or 2.4GHz, dual band: subghz
    }
    if (POWER_OUTPUT_VALUES_COUNT != 0 && POWER_OUTPUT_VALUES_DUAL_COUNT != 0)
    {
        strlcat(version_domain, "/", maxlen);
    }
    if (POWER_OUTPUT_VALUES_DUAL_COUNT != 0)
    {
        strlcat(version_domain, FHSSconfigDualBand->domain, maxlen);    // 2.4GHz
    }
}

bool isUsingPrimaryFreqBand()
{
    return FHSSusePrimaryFreqBand;
}

// --- runtime-freq-v2 -------------------------------------------------------

static FHSSFreqConfig g_configPool[FHSS_SLOT_COUNT];

// Shadow fhss_config_t so FHSSconfig can point at runtime-built data without
// changing the type of the legacy extern (older getters and external readers
// still dereference it). Kept in sync by mirrorLegacyGlobalsFromActive().
static fhss_config_t g_legacyShadow;
static char          g_legacyShadowName[FHSS_FREQ_NAME_MAXLEN];

static const FHSSFreqConfig *g_rendezvousConfig = nullptr;
static const FHSSFreqConfig *g_activeConfig     = nullptr;
static const FHSSFreqConfig *g_stagedConfig     = nullptr;
static uint32_t              g_switchEpochNonce = 0;
static bool                  g_switchArmed      = false;
static bool                  g_stageRequiresAck = false;  // TX sets true; RX leaves false
static bool                  g_ackReceived      = false;  // set by FHSSnotifyAckReceived
static uint32_t              g_msSinceSwap      = 0;
static bool                  g_watchdogArmed    = false;
static FHSSRuntimeState      g_runtimeState     = FHSS_STATE_RENDEZVOUS;

FHSSFreqConfig *FHSSgetPoolSlot(FHSSConfigSlot slot)
{
    return (slot < FHSS_SLOT_COUNT) ? &g_configPool[slot] : nullptr;
}

const FHSSFreqConfig *FHSSgetRendezvousConfig(void) { return g_rendezvousConfig; }
const FHSSFreqConfig *FHSSgetActiveConfig(void)     { return g_activeConfig; }
const FHSSFreqConfig *FHSSgetStagedConfig(void)     { return g_stagedConfig; }
FHSSRuntimeState      FHSSgetRuntimeState(void)     { return g_runtimeState; }

// Mirror the active config into the legacy globals that the inline getters
// and external consumers still read. Called at boot and on every swap.
static void mirrorLegacyGlobalsFromActive(void)
{
    if (g_activeConfig == nullptr) return;

    for (uint8_t i = 0; i < FHSS_FREQ_NAME_MAXLEN; i++)
    {
        g_legacyShadowName[i] = g_activeConfig->params.name[i];
    }
    g_legacyShadow.domain      = g_legacyShadowName;
    g_legacyShadow.freq_start  = g_activeConfig->params.freq_start;
    g_legacyShadow.freq_stop   = g_activeConfig->params.freq_stop;
    g_legacyShadow.freq_count  = g_activeConfig->params.freq_count;
    // freq_center isn't carried by FHSSFreqConfig (not needed by the builder);
    // approximate as the midpoint so callers that read it (e.g. subGHz
    // detection in tx_main) continue to get a sensible value.
    g_legacyShadow.freq_center = (g_activeConfig->params.freq_start +
                                  g_activeConfig->params.freq_stop) / 2;

    FHSSconfig       = &g_legacyShadow;
    freq_spread      = g_activeConfig->freq_spread;
    sync_channel     = g_activeConfig->params.sync_channel;
    primaryBandCount = g_activeConfig->band_count;
    memcpy(FHSSsequence, g_activeConfig->sequence, FHSS_SEQUENCE_LEN);
    FHSSptr = 0;
}

// Snapshot the currently-active compile-time domain into the rendezvous slot.
// Must be called after the existing FHSSrandomiseFHSSsequence body has set
// FHSSconfig / freq_spread / sync_channel / FHSSsequence.
static void initRendezvousFromLegacy(uint32_t seed)
{
    FHSSFreqConfig *rdv = &g_configPool[FHSS_SLOT_RENDEZVOUS];
    FHSSFreqParams p{};
    p.freq_start   = FHSSconfig->freq_start;
    p.freq_stop    = FHSSconfig->freq_stop;
    p.freq_count   = (uint8_t)FHSSconfig->freq_count;
    p.sync_channel = (uint8_t)sync_channel;
    const char *name = FHSSconfig->domain ? FHSSconfig->domain : "";
    uint8_t i = 0;
    for (; i < FHSS_FREQ_NAME_MAXLEN - 1 && name[i] != '\0'; i++)
    {
        p.name[i] = name[i];
    }
    p.name[i] = '\0';

    FHSSbuildConfig(rdv, &p, seed);

    g_rendezvousConfig = rdv;
    g_activeConfig     = rdv;
    g_stagedConfig     = nullptr;
    g_switchArmed      = false;
    g_watchdogArmed    = false;
    g_runtimeState     = FHSS_STATE_RENDEZVOUS;
    // Legacy globals were populated by the existing FHSSrandomiseFHSSsequence
    // body; no need to mirror here — mirrorLegacyGlobalsFromActive() runs on
    // swap. Boot state is already consistent.
}

bool FHSSstageConfig(const FHSSFreqConfig *cfg, uint32_t epochNonce, bool requireAck)
{
    if (cfg == nullptr) return false;
    if (cfg->band_count == 0) return false;
    g_stagedConfig     = cfg;
    g_switchEpochNonce = epochNonce;
    g_switchArmed      = true;
    g_stageRequiresAck = requireAck;
    g_ackReceived      = false;  // fresh stage — any prior ack doesn't count
    g_runtimeState     = FHSS_STATE_STAGED;
    return true;
}

void FHSSnotifyAckReceived(uint32_t epochNonce)
{
    // Only honor the ACK if it matches the currently staged epoch. This
    // rejects stale ACKs for stages that already swapped or were aborted.
    if (!g_switchArmed) return;
    if (epochNonce != g_switchEpochNonce) return;
    g_ackReceived = true;
}

void FHSSactivateIfEpochReached(uint32_t currentNonce)
{
    if (!g_switchArmed || g_stagedConfig == nullptr) return;
    // Monotonic 32-bit compare; OtaNonce won't wrap in any realistic session.
    if (currentNonce < g_switchEpochNonce) return;

    // ACK gate (TX side). If the stager required an ACK and it didn't
    // arrive before epoch, abort rather than swap alone — this is the
    // v1 Nomad failure fix. RX stages with requireAck=false because
    // receiving STAGE IS the proof.
    if (g_stageRequiresAck && !g_ackReceived)
    {
        g_stagedConfig  = nullptr;
        g_switchArmed   = false;
        g_runtimeState  = (g_activeConfig == g_rendezvousConfig)
                          ? FHSS_STATE_RENDEZVOUS : FHSS_STATE_ACTIVE;
        return;
    }

    g_activeConfig  = g_stagedConfig;
    g_stagedConfig  = nullptr;
    g_switchArmed   = false;
    g_ackReceived   = false;
    g_msSinceSwap   = 0;
    g_watchdogArmed = (g_activeConfig != g_rendezvousConfig);
    g_runtimeState  = FHSS_STATE_SWITCHING;
    mirrorLegacyGlobalsFromActive();
}

void FHSSrevertToRendezvous(void)
{
    g_activeConfig  = g_rendezvousConfig;
    g_stagedConfig  = nullptr;
    g_switchArmed   = false;
    g_ackReceived   = false;
    g_watchdogArmed = false;
    g_msSinceSwap   = 0;
    if (g_rendezvousConfig != nullptr) mirrorLegacyGlobalsFromActive();
    g_runtimeState  = FHSS_STATE_FALLBACK;
}

void FHSSnotifyValidPacket(void)
{
    if (g_runtimeState == FHSS_STATE_SWITCHING)
    {
        g_runtimeState = FHSS_STATE_ACTIVE;
    }
    g_msSinceSwap = 0;  // feed the watchdog
}

void FHSSwatchdogTick(uint32_t deltaMs)
{
    if (!g_watchdogArmed) return;
    // Only SWITCHING arms a real fallback — once ACTIVE, the switch is
    // confirmed and the link owns the recovery path.
    if (g_runtimeState != FHSS_STATE_SWITCHING) { g_watchdogArmed = false; return; }

    g_msSinceSwap += deltaMs;
    if (g_msSinceSwap >= FHSS_WATCHDOG_MS)
    {
        FHSSrevertToRendezvous();
    }
}

void FHSSabortStagedConfig(void)
{
    g_stagedConfig = nullptr;
    g_switchArmed  = false;
    g_ackReceived  = false;
    if (g_runtimeState == FHSS_STATE_STAGED)
    {
        g_runtimeState = (g_activeConfig == g_rendezvousConfig)
                         ? FHSS_STATE_RENDEZVOUS : FHSS_STATE_ACTIVE;
    }
}

// Pure variant of the sequence-building loop used by FHSSrandomiseFHSSsequenceBuild.
// Does not touch FHSSptr or the band_count globals — operates only on the output
// buffer plus the shared RNG.
static void buildSequenceInto(uint32_t seed, uint8_t freqCount, uint8_t syncChannel, uint8_t *sequence)
{
    const uint16_t sequenceLen = (FHSS_SEQUENCE_LEN / freqCount) * freqCount;
    rngSeed(seed);

    for (uint16_t i = 0; i < sequenceLen; i++)
    {
        if (i % freqCount == 0) {
            sequence[i] = syncChannel;
        } else if (i % freqCount == syncChannel) {
            sequence[i] = 0;
        } else {
            sequence[i] = i % freqCount;
        }
    }

    for (uint16_t i = 0; i < sequenceLen; i++)
    {
        if (i % freqCount != 0)
        {
            uint8_t offset = (i / freqCount) * freqCount;
            uint8_t rand = rngN(freqCount - 1) + 1;
            uint8_t temp = sequence[i];
            sequence[i] = sequence[offset + rand];
            sequence[offset + rand] = temp;
        }
    }
}

bool FHSSbuildConfig(FHSSFreqConfig *dst, const FHSSFreqParams *params, uint32_t seed)
{
    if (dst == nullptr || params == nullptr) return false;
    if (params->freq_count < 2) return false;
    if (params->freq_start >= params->freq_stop) return false;
    if (params->sync_channel >= params->freq_count) return false;

    dst->params = *params;
    dst->freq_spread = (params->freq_stop - params->freq_start) * FREQ_SPREAD_SCALE / (params->freq_count - 1);
    dst->band_count = (FHSS_SEQUENCE_LEN / params->freq_count) * params->freq_count;
    buildSequenceInto(seed, params->freq_count, params->sync_channel, dst->sequence);
    return true;
}
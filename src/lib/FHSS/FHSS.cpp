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
    // Regulatory presets first. Indices 0-7 are the legacy firmwareOptions.domain IDs.
    {"AU915",  FREQ_HZ_TO_REG_VAL(915500000), FREQ_HZ_TO_REG_VAL(926900000), 20, 921000000},
    {"FCC915", FREQ_HZ_TO_REG_VAL(903500000), FREQ_HZ_TO_REG_VAL(926900000), 40, 915000000},
    {"EU868",  FREQ_HZ_TO_REG_VAL(863275000), FREQ_HZ_TO_REG_VAL(869575000), 13, 868000000},
    {"IN866",  FREQ_HZ_TO_REG_VAL(865375000), FREQ_HZ_TO_REG_VAL(866950000), 4, 866000000},
    {"AU433",  FREQ_HZ_TO_REG_VAL(433420000), FREQ_HZ_TO_REG_VAL(434420000), 3, 434000000},
    {"EU433",  FREQ_HZ_TO_REG_VAL(433100000), FREQ_HZ_TO_REG_VAL(434450000), 3, 434000000},
    {"US433",  FREQ_HZ_TO_REG_VAL(433250000), FREQ_HZ_TO_REG_VAL(438000000), 8, 434000000},
    {"US433W", FREQ_HZ_TO_REG_VAL(423500000), FREQ_HZ_TO_REG_VAL(438000000), 20, 434000000},
    // Runtime-only wide-tuning presets (v1 "dense runtime presets"). Names match
    // the v1 Lua selector order so muscle memory carries over. LR1121 LF path
    // covers 150-960 MHz; these 80-ch slices cover that densely for max
    // tuning flexibility (TechTactical use case).
    {"TrainA",   FREQ_HZ_TO_REG_VAL(840000000), FREQ_HZ_TO_REG_VAL(860000000), 32, 850000000},
    {"700-779",  FREQ_HZ_TO_REG_VAL(700000000), FREQ_HZ_TO_REG_VAL(779000000), 80, 739500000},
    {"780-859",  FREQ_HZ_TO_REG_VAL(780000000), FREQ_HZ_TO_REG_VAL(859000000), 80, 819500000},
    {"860-939",  FREQ_HZ_TO_REG_VAL(860000000), FREQ_HZ_TO_REG_VAL(939000000), 80, 899500000},
    {"881-960",  FREQ_HZ_TO_REG_VAL(881000000), FREQ_HZ_TO_REG_VAL(960000000), 80, 920500000},
    {"MaxRangeLo", FREQ_HZ_TO_REG_VAL(700000000), FREQ_HZ_TO_REG_VAL(960000000), 80, 830000000},
};

#if defined(RADIO_LR1121)
// PR 7: high-band presets, user-selectable at runtime via Lua. Mirrors the
// v1 runtime-freq preset list (from the other branch's master). LR1121 HF
// path covers 1900-2500 MHz per datasheet; 80-ch slices at 80 MHz widths
// densely cover the chip's HF tuning window so the operator can pick
// essentially any sub-band. MaxRange is the full HF span, useful for
// TechTactical's "max tuning flexibility" use case.
const fhss_config_t domainsDualBand[] = {
    {"ISM2G4",   FREQ_HZ_TO_REG_VAL(2400400000), FREQ_HZ_TO_REG_VAL(2479400000), 80, 2440000000},
    {"CE_LBT",   FREQ_HZ_TO_REG_VAL(2400400000), FREQ_HZ_TO_REG_VAL(2479400000), 80, 2440000000},
    {"ISM2G4W",  FREQ_HZ_TO_REG_VAL(2400000000), FREQ_HZ_TO_REG_VAL(2500000000), 80, 2450000000},
    {"SBand",    FREQ_HZ_TO_REG_VAL(2300000000), FREQ_HZ_TO_REG_VAL(2399000000), 40, 2349500000},
    {"Train24",  FREQ_HZ_TO_REG_VAL(2000000000), FREQ_HZ_TO_REG_VAL(2100000000), 40, 2050000000},
    {"1900-1979", FREQ_HZ_TO_REG_VAL(1900000000), FREQ_HZ_TO_REG_VAL(1979000000), 80, 1939500000},
    {"1980-2059", FREQ_HZ_TO_REG_VAL(1980000000), FREQ_HZ_TO_REG_VAL(2059000000), 80, 2019500000},
    {"2060-2139", FREQ_HZ_TO_REG_VAL(2060000000), FREQ_HZ_TO_REG_VAL(2139000000), 80, 2099500000},
    {"2140-2219", FREQ_HZ_TO_REG_VAL(2140000000), FREQ_HZ_TO_REG_VAL(2219000000), 80, 2179500000},
    {"2220-2299", FREQ_HZ_TO_REG_VAL(2220000000), FREQ_HZ_TO_REG_VAL(2299000000), 80, 2259500000},
    {"2300-2379", FREQ_HZ_TO_REG_VAL(2300000000), FREQ_HZ_TO_REG_VAL(2379000000), 80, 2339500000},
    {"2380-2459", FREQ_HZ_TO_REG_VAL(2380000000), FREQ_HZ_TO_REG_VAL(2459000000), 80, 2419500000},
    {"2421-2500", FREQ_HZ_TO_REG_VAL(2421000000), FREQ_HZ_TO_REG_VAL(2500000000), 80, 2460500000},
    {"MaxRangeHi", FREQ_HZ_TO_REG_VAL(1900000000), FREQ_HZ_TO_REG_VAL(2500000000), 80, 2200000000},
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
    // PR 7: compile-time default picks index 0 (ISM2G4) or index 1 (CE_LBT)
    // depending on the regulatory-domain flag. Runtime Lua can swap among
    // all entries in domainsDualBand[] without a reflash.
    #if defined(Regulatory_Domain_EU_CE_2400)
        FHSSconfigDualBand = &domainsDualBand[1];  // CE_LBT
    #else
        FHSSconfigDualBand = &domainsDualBand[0];  // ISM2G4 default
    #endif
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

#if defined(RUNTIME_FREQ_DEBUG)
#define FREQ_DBG(fmt, ...) DBGLN("[FREQ] " fmt, ##__VA_ARGS__)
#else
#define FREQ_DBG(...) do {} while (0)
#endif

static FHSSFreqConfig g_configPool[FHSS_SLOT_COUNT];

// Shadow fhss_config_t so FHSSconfig can point at runtime-built data without
// changing the type of the legacy extern (older getters and external readers
// still dereference it). Kept in sync by mirrorLegacyGlobalsFromActive().
static fhss_config_t g_legacyShadow;
static char          g_legacyShadowName[FHSS_FREQ_NAME_MAXLEN];
// PR 7: parallel shadow for the dual-band (2.4 GHz) side. Populated only
// when the active config has has_dualband=true; consumers should never
// read it otherwise (same pattern as the primary shadow).
static fhss_config_t g_legacyShadowDb;
static char          g_legacyShadowDbName[FHSS_FREQ_NAME_MAXLEN];

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

// Deferred work: the swap runs in ISR context, but LR1121 image-rejection
// calibration is an SPI command and must run from the main loop. The ISR
// sets this flag + records the new range; tx_main/rx_main poll and consume.
static volatile bool         g_pendingImageCal   = false;
static uint32_t              g_pendingImageCalMinHz = 0;
static uint32_t              g_pendingImageCalMaxHz = 0;

FHSSFreqConfig *FHSSgetPoolSlot(FHSSConfigSlot slot)
{
    return (slot < FHSS_SLOT_COUNT) ? &g_configPool[slot] : nullptr;
}

const fhss_config_t *FHSSgetCompileTimeDomain(uint8_t index)
{
    return (index < FHSSgetCompileTimeDomainCount()) ? &domains[index] : nullptr;
}

uint8_t FHSSgetCompileTimeDomainCount(void)
{
    return (uint8_t)(sizeof(domains) / sizeof(domains[0]));
}

const fhss_config_t *FHSSgetCompileTimeDomainDb(uint8_t index)
{
#if defined(RADIO_LR1121)
    const uint8_t n = (uint8_t)(sizeof(domainsDualBand) / sizeof(domainsDualBand[0]));
    return (index < n) ? &domainsDualBand[index] : nullptr;
#else
    (void)index;
    return nullptr;
#endif
}

uint8_t FHSSgetCompileTimeDomainDbCount(void)
{
#if defined(RADIO_LR1121)
    return (uint8_t)(sizeof(domainsDualBand) / sizeof(domainsDualBand[0]));
#else
    return 0;
#endif
}

const FHSSFreqConfig *FHSSgetRendezvousConfig(void) { return g_rendezvousConfig; }
const FHSSFreqConfig *FHSSgetActiveConfig(void)     { return g_activeConfig; }
const FHSSFreqConfig *FHSSgetStagedConfig(void)     { return g_stagedConfig; }
uint32_t              FHSSgetStagedEpoch(void)      { return g_switchArmed ? g_switchEpochNonce : 0; }
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

    // PR 7: mirror the dual-band half when the active config carries one.
    // Only update the 2.4 GHz globals when has_dualband is true — leaves
    // upstream-managed defaults alone on single-band configs.
    if (g_activeConfig->has_dualband)
    {
        for (uint8_t i = 0; i < FHSS_FREQ_NAME_MAXLEN; i++)
        {
            g_legacyShadowDbName[i] = g_activeConfig->db_params.name[i];
        }
        g_legacyShadowDb.domain      = g_legacyShadowDbName;
        g_legacyShadowDb.freq_start  = g_activeConfig->db_params.freq_start;
        g_legacyShadowDb.freq_stop   = g_activeConfig->db_params.freq_stop;
        g_legacyShadowDb.freq_count  = g_activeConfig->db_params.freq_count;
        g_legacyShadowDb.freq_center = (g_activeConfig->db_params.freq_start +
                                        g_activeConfig->db_params.freq_stop) / 2;
        FHSSconfigDualBand    = &g_legacyShadowDb;
        freq_spread_DualBand  = g_activeConfig->db_freq_spread;
        sync_channel_DualBand = g_activeConfig->db_params.sync_channel;
        secondaryBandCount    = g_activeConfig->db_band_count;
        memcpy(FHSSsequence_DualBand, g_activeConfig->db_sequence, FHSS_SEQUENCE_LEN);
    }
}

// Snapshot the currently-active compile-time domain into the rendezvous slot.
// Must be called after the existing FHSSrandomiseFHSSsequence body has set
// FHSSconfig / freq_spread / sync_channel / FHSSsequence (and, on LR1121
// dual-band targets, FHSSconfigDualBand / sync_channel_DualBand /
// FHSSsequence_DualBand as well).
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

    // PR 7: if this build has a dual-band (2.4 GHz) chip, snapshot that
    // compile-time preset into the rendezvous too so swaps that only
    // touch one band leave the other band alive on the rendezvous config.
#if defined(RADIO_LR1121)
    FHSSFreqParams dbP{};
    bool haveDb = (FHSSconfigDualBand != nullptr);
    if (haveDb)
    {
        dbP.freq_start   = FHSSconfigDualBand->freq_start;
        dbP.freq_stop    = FHSSconfigDualBand->freq_stop;
        dbP.freq_count   = (uint8_t)FHSSconfigDualBand->freq_count;
        dbP.sync_channel = (uint8_t)sync_channel_DualBand;
        const char *dbName = FHSSconfigDualBand->domain ? FHSSconfigDualBand->domain : "";
        uint8_t j = 0;
        for (; j < FHSS_FREQ_NAME_MAXLEN - 1 && dbName[j] != '\0'; j++)
        {
            dbP.name[j] = dbName[j];
        }
        dbP.name[j] = '\0';
    }
    FHSSbuildConfig(rdv, &p, seed, haveDb ? &dbP : nullptr);
#else
    FHSSbuildConfig(rdv, &p, seed);
#endif

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
    FREQ_DBG("stage name=%s epoch=%u requireAck=%d", cfg->params.name, (unsigned)epochNonce, (int)requireAck);
    return true;
}

void FHSSnotifyAckReceived(uint32_t epochNonce)
{
    // Only honor the ACK if it matches the currently staged epoch. This
    // rejects stale ACKs for stages that already swapped or were aborted.
    if (!g_switchArmed) return;
    if (epochNonce != g_switchEpochNonce) return;
    g_ackReceived = true;
    FREQ_DBG("ack received for epoch=%u -> gate open", (unsigned)epochNonce);
}

void FHSSactivateIfEpochReached(uint32_t currentNonce)
{
    if (!g_switchArmed || g_stagedConfig == nullptr) return;
    // ELRS OtaNonce is uint8_t — wraps every 256 packets. Use wrap-aware
    // comparison with an 8-bit signed delta: delta in [-128, 127] where
    // negative = "epoch not yet reached", non-negative = "epoch passed".
    // This constrains the max lead to 127 packets (~1.3s @ 100Hz, ~0.25s
    // @ 500Hz). FreqStageComputeLeadNonces caps the lead accordingly.
    int8_t delta = (int8_t)((uint8_t)currentNonce - (uint8_t)g_switchEpochNonce);
    if (delta < 0) return;

    // ACK gate (TX side). If the stager required an ACK and it didn't
    // arrive before epoch, abort rather than swap alone — this is the
    // v1 Nomad failure fix. RX stages with requireAck=false because
    // receiving STAGE IS the proof.
    //
    // NOTE: no DBGLN here. FHSSactivateIfEpochReached runs in timer-ISR
    // context on both TX (timerCallback / nonceAdvance) and RX
    // (HWtimerCallbackTock). Serial.printf is not ISR-safe on ESP32.
    // Observable state lives in g_runtimeState + Lua status line.
    if (g_stageRequiresAck && !g_ackReceived)
    {
        g_stagedConfig  = nullptr;
        g_switchArmed   = false;
        g_runtimeState  = (g_activeConfig == g_rendezvousConfig)
                          ? FHSS_STATE_RENDEZVOUS : FHSS_STATE_ACTIVE;
        return;
    }

    // Snapshot old primary range BEFORE swapping pointers so we can detect
    // whether the sub-GHz band actually changed. If it didn't, arming
    // CalibImage is gratuitous and — on dual-band hardware — can drop the
    // first post-swap Radio_2 packet (Radio_1 holds BUSY ~3.5 ms during
    // the SPI cal, overlapping with Radio_2's packet reception). That lost
    // packet can start a disconnect-timeout / cycleRfMode spiral that
    // takes several seconds to unwind, especially on narrow dual-band
    // presets where cycleRfMode's rate-scan interval is shorter than the
    // watchdog revert.
    const uint32_t prevPrimaryStart = (g_activeConfig != nullptr) ? g_activeConfig->params.freq_start : 0;
    const uint32_t prevPrimaryStop  = (g_activeConfig != nullptr) ? g_activeConfig->params.freq_stop  : 0;

    g_activeConfig  = g_stagedConfig;
    g_stagedConfig  = nullptr;
    g_switchArmed   = false;
    g_ackReceived   = false;
    g_msSinceSwap   = 0;
    g_watchdogArmed = (g_activeConfig != g_rendezvousConfig);
    g_runtimeState  = FHSS_STATE_SWITCHING;
    mirrorLegacyGlobalsFromActive();

    // Only re-calibrate Radio_1 image rejection if the primary band range
    // actually moved. For LR1121 FREQ_HZ_TO_REG_VAL is identity, so
    // freq_start/freq_stop are Hz. Non-LR1121 radios ignore the flag.
    const bool primaryChanged =
        (prevPrimaryStart != g_activeConfig->params.freq_start) ||
        (prevPrimaryStop  != g_activeConfig->params.freq_stop);
    if (primaryChanged)
    {
        g_pendingImageCalMinHz = g_activeConfig->params.freq_start;
        g_pendingImageCalMaxHz = g_activeConfig->params.freq_stop;
        g_pendingImageCal      = true;
    }
}

void FHSSrevertToRendezvous(void)
{
    FREQ_DBG("revert -> rendezvous (from state=%u)", (unsigned)g_runtimeState);
    g_activeConfig  = g_rendezvousConfig;
    g_stagedConfig  = nullptr;
    g_switchArmed   = false;
    g_ackReceived   = false;
    g_watchdogArmed = false;
    g_msSinceSwap   = 0;
    if (g_rendezvousConfig != nullptr)
    {
        mirrorLegacyGlobalsFromActive();
        g_pendingImageCalMinHz = g_activeConfig->params.freq_start;
        g_pendingImageCalMaxHz = g_activeConfig->params.freq_stop;
        g_pendingImageCal      = true;
    }
    g_runtimeState  = FHSS_STATE_FALLBACK;
}

bool FHSSconsumePendingImageCal(uint32_t *minHzOut, uint32_t *maxHzOut)
{
    if (!g_pendingImageCal) return false;
    if (minHzOut != nullptr) *minHzOut = g_pendingImageCalMinHz;
    if (maxHzOut != nullptr) *maxHzOut = g_pendingImageCalMaxHz;
    g_pendingImageCal = false;
    return true;
}

// Observability: main-loop-safe state-change logger. Call every loop; when
// observable state (runtime state, active config, staged config, ack/armed
// flags) has changed since the last call, emits a single DBGLN line. Zero
// work when nothing has changed. Cannot DBGLN from inside the ISR paths,
// so this is how we make transitions visible on hardware.
void FHSSlogStateIfChanged(uint32_t currentNonce)
{
#if defined(RUNTIME_FREQ_DEBUG) && !defined(UNIT_TEST)
    static FHSSRuntimeState lastState   = (FHSSRuntimeState)0xFF;
    static const FHSSFreqConfig *lastActive = (const FHSSFreqConfig *)0x1;
    static const FHSSFreqConfig *lastStaged = (const FHSSFreqConfig *)0x1;
    static bool lastArmed   = false;
    static bool lastAck     = false;

    if (g_runtimeState  == lastState  &&
        g_activeConfig  == lastActive &&
        g_stagedConfig  == lastStaged &&
        g_switchArmed   == lastArmed  &&
        g_ackReceived   == lastAck) return;

    const char *stateStr = "?";
    switch (g_runtimeState)
    {
        case FHSS_STATE_RENDEZVOUS: stateStr = "RDV";    break;
        case FHSS_STATE_STAGED:     stateStr = "STGD";   break;
        case FHSS_STATE_SWITCHING:  stateStr = "SWTCH";  break;
        case FHSS_STATE_ACTIVE:     stateStr = "ACTIVE"; break;
        case FHSS_STATE_FALLBACK:   stateStr = "FALLBK"; break;
    }
    const char *actName = (g_activeConfig != nullptr) ? g_activeConfig->params.name : "-";
    const char *stgName = (g_stagedConfig != nullptr) ? g_stagedConfig->params.name : "-";
    DBGLN("[FREQ] state=%s act=%s stg=%s epoch=%u nonce=%u armed=%d ack=%d",
          stateStr, actName, stgName,
          (unsigned)g_switchEpochNonce, (unsigned)currentNonce,
          (int)g_switchArmed, (int)g_ackReceived);

    lastState   = g_runtimeState;
    lastActive  = g_activeConfig;
    lastStaged  = g_stagedConfig;
    lastArmed   = g_switchArmed;
    lastAck     = g_ackReceived;
#else
    (void)currentNonce;
#endif
}

void FHSSnotifyValidPacket(void)
{
    // Runs in RX/TX packet-reception ISR context; no Serial output here.
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
        FREQ_DBG("watchdog fired after %ums -> revert", (unsigned)g_msSinceSwap);
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

bool FHSSbuildConfig(FHSSFreqConfig *dst, const FHSSFreqParams *params, uint32_t seed,
                     const FHSSFreqParams *dbParams)
{
    if (dst == nullptr || params == nullptr) return false;
    if (params->freq_count < 2) return false;
    if (params->freq_start >= params->freq_stop) return false;
    if (params->sync_channel >= params->freq_count) return false;

    dst->params = *params;
    dst->freq_spread = (params->freq_stop - params->freq_start) * FREQ_SPREAD_SCALE / (params->freq_count - 1);
    dst->band_count = (FHSS_SEQUENCE_LEN / params->freq_count) * params->freq_count;
    buildSequenceInto(seed, params->freq_count, params->sync_channel, dst->sequence);

    // PR 7: dual-band payload. Same seed as primary so TX and RX produce
    // identical db_sequence from the same UID — the underlying RNG is
    // reseeded inside buildSequenceInto, so calling it twice in sequence
    // is deterministic regardless of ordering.
    if (dbParams != nullptr)
    {
        if (dbParams->freq_count < 2) return false;
        if (dbParams->freq_start >= dbParams->freq_stop) return false;
        if (dbParams->sync_channel >= dbParams->freq_count) return false;

        dst->has_dualband    = true;
        dst->db_params       = *dbParams;
        dst->db_freq_spread  = (dbParams->freq_stop - dbParams->freq_start) * FREQ_SPREAD_SCALE / (dbParams->freq_count - 1);
        dst->db_band_count   = (FHSS_SEQUENCE_LEN / dbParams->freq_count) * dbParams->freq_count;
        buildSequenceInto(seed, dbParams->freq_count, dbParams->sync_channel, dst->db_sequence);
    }
    else
    {
        dst->has_dualband = false;
        // Leave db_* fields zero-valued; consumers must gate on has_dualband.
    }
    return true;
}
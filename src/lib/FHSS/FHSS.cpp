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
char version_domain[VERSION_DOMAIN_MAXLEN];

static fhss_config_t runtimeDomain = {"CUSTOM", 0, 0, 0, 0};
static fhss_config_t runtimeDualBandDomain = {"CUSTOM2G4", 0, 0, 0, 0};
static bool runtimeFreqActive = false;
static bool runtimeHighFreqActive = false;

static bool isValidRuntimeFrequency(uint32_t startHz, uint32_t stopHz, uint8_t channelCount, uint32_t minHz, uint32_t maxHz)
{
#if !defined(RADIO_LR1121)
    (void)startHz;
    (void)stopHz;
    (void)channelCount;
    (void)minHz;
    (void)maxHz;
    return false;
#else
    return channelCount >= 4
        && channelCount <= 80
        && startHz >= minHz
        && stopHz <= maxHz
        && stopHz > startHz;
#endif
}

static const char *resolveRuntimeLabel(const char *label, const char *fallback)
{
    if (label[0] != '\0')
    {
        return label;
    }

    return fallback;
}

static bool isValidPrimaryRuntimeFrequency(uint32_t startHz, uint32_t stopHz, uint8_t channelCount)
{
    return isValidRuntimeFrequency(startHz, stopHz, channelCount, 700000000U, 960000000U);
}

static bool isValidHighRuntimeFrequency(uint32_t startHz, uint32_t stopHz, uint8_t channelCount)
{
    return isValidRuntimeFrequency(startHz, stopHz, channelCount, 1900000000U, 2500000000U);
}

static const fhss_config_t *resolvePrimaryDomain()
{
#if defined(RADIO_LR1121)
    if (firmwareOptions.runtime_freq_enabled
        && runtimeFreqActive
        && isValidPrimaryRuntimeFrequency(
            firmwareOptions.runtime_freq_start,
            firmwareOptions.runtime_freq_stop,
            firmwareOptions.runtime_freq_count))
    {
        runtimeDomain.domain = resolveRuntimeLabel(firmwareOptions.runtime_freq_label, "CUSTOM");
        runtimeDomain.freq_start = FREQ_HZ_TO_REG_VAL(firmwareOptions.runtime_freq_start);
        runtimeDomain.freq_stop = FREQ_HZ_TO_REG_VAL(firmwareOptions.runtime_freq_stop);
        runtimeDomain.freq_count = firmwareOptions.runtime_freq_count;
        runtimeDomain.freq_center = (firmwareOptions.runtime_freq_start + firmwareOptions.runtime_freq_stop) / 2U;
        return &runtimeDomain;
    }
#endif

    return &domains[firmwareOptions.domain];
}

static const fhss_config_t *resolveHighBandDomain()
{
#if defined(RADIO_LR1121)
    if (firmwareOptions.runtime_high_freq_enabled
        && runtimeHighFreqActive
        && isValidHighRuntimeFrequency(
            firmwareOptions.runtime_high_freq_start,
            firmwareOptions.runtime_high_freq_stop,
            firmwareOptions.runtime_high_freq_count))
    {
        runtimeDualBandDomain.domain = resolveRuntimeLabel(firmwareOptions.runtime_high_freq_label, "CUSTOM2G4");
        runtimeDualBandDomain.freq_start = FREQ_HZ_TO_REG_VAL(firmwareOptions.runtime_high_freq_start);
        runtimeDualBandDomain.freq_stop = FREQ_HZ_TO_REG_VAL(firmwareOptions.runtime_high_freq_stop);
        runtimeDualBandDomain.freq_count = firmwareOptions.runtime_high_freq_count;
        runtimeDualBandDomain.freq_center = (firmwareOptions.runtime_high_freq_start + firmwareOptions.runtime_high_freq_stop) / 2U;
        return &runtimeDualBandDomain;
    }
    return &domainsDualBand[0];
#else
    return nullptr;
#endif
}


void FHSSrandomiseFHSSsequence(const uint32_t seed)
{
    FHSSconfig = resolvePrimaryDomain();
    sync_channel = FHSSconfig->freq_count / 2;
    freq_spread = (FHSSconfig->freq_stop - FHSSconfig->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfig->freq_count - 1);
    primaryBandCount = (FHSS_SEQUENCE_LEN / FHSSconfig->freq_count) * FHSSconfig->freq_count;

    DBGLN("Primary Domain %s, %u channels, sync=%u",
        FHSSconfig->domain, FHSSconfig->freq_count, sync_channel);

    FHSSrandomiseFHSSsequenceBuild(seed, FHSSconfig->freq_count, sync_channel, FHSSsequence);

#if defined(RADIO_LR1121)
    FHSSconfigDualBand = resolveHighBandDomain();
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

bool FHSSruntimeFreqEnabled()
{
    return firmwareOptions.runtime_freq_enabled;
}

bool FHSSruntimeFreqValid()
{
    return isValidPrimaryRuntimeFrequency(
        firmwareOptions.runtime_freq_start,
        firmwareOptions.runtime_freq_stop,
        firmwareOptions.runtime_freq_count);
}

bool FHSSruntimeFreqPending()
{
    return firmwareOptions.runtime_freq_enabled && !runtimeFreqActive && FHSSruntimeFreqValid();
}

void FHSSactivateRuntimeFrequency()
{
    runtimeFreqActive = firmwareOptions.runtime_freq_enabled && FHSSruntimeFreqValid();
}

void FHSSsetRuntimeFrequency(uint32_t startHz, uint32_t stopHz, uint8_t channelCount, uint8_t presetSlot, const char *label)
{
    firmwareOptions.runtime_freq_enabled = true;
    firmwareOptions.runtime_freq_start = startHz;
    firmwareOptions.runtime_freq_stop = stopHz;
    firmwareOptions.runtime_freq_count = channelCount;
    firmwareOptions.runtime_freq_preset = presetSlot;
    strlcpy(firmwareOptions.runtime_freq_label, label ? label : "CUSTOM", sizeof(firmwareOptions.runtime_freq_label));
}

void FHSSdisableRuntimeFrequency()
{
    firmwareOptions.runtime_freq_enabled = false;
    firmwareOptions.runtime_freq_preset = 0;
    firmwareOptions.runtime_freq_label[0] = '\0';
    runtimeFreqActive = false;
}

bool FHSShighRuntimeFreqEnabled()
{
    return firmwareOptions.runtime_high_freq_enabled;
}

bool FHSShighRuntimeFreqValid()
{
    return isValidHighRuntimeFrequency(
        firmwareOptions.runtime_high_freq_start,
        firmwareOptions.runtime_high_freq_stop,
        firmwareOptions.runtime_high_freq_count);
}

bool FHSShighRuntimeFreqPending()
{
    return firmwareOptions.runtime_high_freq_enabled && !runtimeHighFreqActive && FHSShighRuntimeFreqValid();
}

void FHSSactivateHighRuntimeFrequency()
{
    runtimeHighFreqActive = firmwareOptions.runtime_high_freq_enabled && FHSShighRuntimeFreqValid();
}

void FHSSactivatePendingRuntimeFrequencies()
{
    FHSSactivateRuntimeFrequency();
    FHSSactivateHighRuntimeFrequency();
}

void FHSSsetHighRuntimeFrequency(uint32_t startHz, uint32_t stopHz, uint8_t channelCount, uint8_t presetSlot, const char *label)
{
    firmwareOptions.runtime_high_freq_enabled = true;
    firmwareOptions.runtime_high_freq_start = startHz;
    firmwareOptions.runtime_high_freq_stop = stopHz;
    firmwareOptions.runtime_high_freq_count = channelCount;
    firmwareOptions.runtime_high_freq_preset = presetSlot;
    strlcpy(firmwareOptions.runtime_high_freq_label, label ? label : "CUSTOM2G4", sizeof(firmwareOptions.runtime_high_freq_label));
}

void FHSSdisableHighRuntimeFrequency()
{
    firmwareOptions.runtime_high_freq_enabled = false;
    firmwareOptions.runtime_high_freq_preset = 0;
    firmwareOptions.runtime_high_freq_label[0] = '\0';
    runtimeHighFreqActive = false;
}

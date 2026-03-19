#include "FHSS.h"
#include "logging.h"
#include "options.h"
#include <string.h>

#if defined(RADIO_SX127X) || defined(RADIO_LR1121)

#if defined(RADIO_LR1121)
#include "LR1121Driver.h"
#else
#include "SX127xDriver.h"
#endif

const fhss_config_t domains[] = {
    // WIDEBAND: 700-960MHz for maximum frequency diversity
    {"700-960", FREQ_HZ_TO_REG_VAL(700000000), FREQ_HZ_TO_REG_VAL(960000000), 80, 830000000},
    {"700-960", FREQ_HZ_TO_REG_VAL(700000000), FREQ_HZ_TO_REG_VAL(960000000), 80, 830000000},
    {"700-960", FREQ_HZ_TO_REG_VAL(700000000), FREQ_HZ_TO_REG_VAL(960000000), 80, 830000000},
    {"700-960", FREQ_HZ_TO_REG_VAL(700000000), FREQ_HZ_TO_REG_VAL(960000000), 80, 830000000},
    {"700-960", FREQ_HZ_TO_REG_VAL(700000000), FREQ_HZ_TO_REG_VAL(960000000), 80, 830000000},
    {"700-960", FREQ_HZ_TO_REG_VAL(700000000), FREQ_HZ_TO_REG_VAL(960000000), 80, 830000000},
    {"700-960", FREQ_HZ_TO_REG_VAL(700000000), FREQ_HZ_TO_REG_VAL(960000000), 80, 830000000},
    {"700-960", FREQ_HZ_TO_REG_VAL(700000000), FREQ_HZ_TO_REG_VAL(960000000), 80, 830000000},
};

#if defined(RADIO_LR1121)
const fhss_config_t domainsDualBand[] = {
    {
    // WIDEBAND S-BAND: 1.9-2.2GHz (full LR1121 S-band range)
    "SBAND19-22",
    FREQ_HZ_TO_REG_VAL(1900000000), FREQ_HZ_TO_REG_VAL(2200000000), 80, 2050000000}
};
#endif

#elif defined(RADIO_SX128X)
#include "SX1280Driver.h"

const fhss_config_t domains[] = {
    {
    #if defined(Regulatory_Domain_EU_CE_2400)
        "CE_LBT",
    #elif defined(Regulatory_Domain_ISM_2400)
        "CUST2G2",
    #endif
    FREQ_HZ_TO_REG_VAL(2200000000), FREQ_HZ_TO_REG_VAL(2300000000), 80, 2250000000}
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

bool isDomain868()
{
    return strcmp(FHSSconfig->domain, "EU868") == 0;
}

bool isUsingPrimaryFreqBand()
{
    return FHSSusePrimaryFreqBand;
}

#if defined(RADIO_SX127X) || defined(RADIO_LR1121)
// Custom frequency configuration for runtime changes via Lua
static fhss_config_t customDomain = {"CUSTOM", 0, 0, 0, 0};
static bool customFreqEnabled = false;

void FHSSsetCustomFrequency(uint32_t freqStart, uint32_t freqStop, uint8_t freqCount)
{
    // Validate parameters
    if (freqCount < 4 || freqCount > 80) {
        DBGLN("Custom freq: invalid channel count %u", freqCount);
        return;
    }
    if (freqStop <= freqStart) {
        DBGLN("Custom freq: stop must be > start");
        return;
    }

    // Configure custom domain
    customDomain.freq_start = FREQ_HZ_TO_REG_VAL(freqStart);
    customDomain.freq_stop = FREQ_HZ_TO_REG_VAL(freqStop);
    customDomain.freq_count = freqCount;
    customDomain.freq_center = (freqStart + freqStop) / 2;

    // Switch to custom domain
    FHSSconfig = &customDomain;
    customFreqEnabled = true;

    // Recalculate FHSS parameters
    sync_channel = FHSSconfig->freq_count / 2;
    freq_spread = (FHSSconfig->freq_stop - FHSSconfig->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfig->freq_count - 1);
    primaryBandCount = (FHSS_SEQUENCE_LEN / FHSSconfig->freq_count) * FHSSconfig->freq_count;

    DBGLN("Custom Domain: %u-%u MHz, %u channels, sync=%u",
        freqStart / 1000000, freqStop / 1000000, FHSSconfig->freq_count, sync_channel);
}

void FHSSdisableCustomFrequency()
{
    if (customFreqEnabled) {
        // Restore default domain
        FHSSconfig = &domains[firmwareOptions.domain];
        customFreqEnabled = false;

        // Recalculate FHSS parameters
        sync_channel = FHSSconfig->freq_count / 2;
        freq_spread = (FHSSconfig->freq_stop - FHSSconfig->freq_start) * FREQ_SPREAD_SCALE / (FHSSconfig->freq_count - 1);
        primaryBandCount = (FHSS_SEQUENCE_LEN / FHSSconfig->freq_count) * FHSSconfig->freq_count;

        DBGLN("Restored default domain %s", FHSSconfig->domain);
    }
}

bool FHSSisCustomFrequencyEnabled()
{
    return customFreqEnabled;
}
#endif

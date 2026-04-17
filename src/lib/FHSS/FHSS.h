#pragma once

#include "targets.h"
#include "random.h"

#if defined(RADIO_SX127X)
#define FreqCorrectionMax ((int32_t)(100000/FREQ_STEP))
#elif defined(RADIO_LR1121)
#define FreqCorrectionMax ((int32_t)(100000/FREQ_STEP)) // TODO - This needs checking !!!
#elif defined(RADIO_SX128X)
#define FreqCorrectionMax ((int32_t)(200000/FREQ_STEP))
#endif
#define FreqCorrectionMin (-FreqCorrectionMax)

#if defined(RADIO_LR1121)
#define FREQ_HZ_TO_REG_VAL(freq) (freq)
#define FREQ_SPREAD_SCALE 1
#else
#define FREQ_HZ_TO_REG_VAL(freq) ((uint32_t)((double)freq/(double)FREQ_STEP))
#define FREQ_SPREAD_SCALE 256
#endif

#define FHSS_SEQUENCE_LEN 256

typedef struct {
    const char  *domain;
    uint32_t    freq_start;
    uint32_t    freq_stop;
    uint32_t    freq_count;
    uint32_t    freq_center;
} fhss_config_t;

extern volatile uint8_t FHSSptr;
extern int32_t FreqCorrection;      // Only used for the SX1276
extern int32_t FreqCorrection_2;    // Only used for the SX1276

// Primary Band
extern uint16_t primaryBandCount;
extern uint32_t freq_spread;
extern uint8_t FHSSsequence[];
extern uint_fast8_t sync_channel;
extern const fhss_config_t *FHSSconfig;

// DualBand Variables
extern bool FHSSusePrimaryFreqBand;
extern bool FHSSuseDualBand;
extern uint16_t secondaryBandCount;
extern uint32_t freq_spread_DualBand;
extern uint8_t FHSSsequence_DualBand[];
extern uint_fast8_t sync_channel_DualBand;
extern const fhss_config_t *FHSSconfigDualBand;

// version/domain string
extern char version_domain[];

// create and randomise an FHSS sequence
void FHSSrandomiseFHSSsequence(uint32_t seed);
void FHSSrandomiseFHSSsequenceBuild(uint32_t seed, uint32_t freqCount, uint_fast8_t sync_channel, uint8_t *sequence);

// add domain info for Lua
void addDomainInfo(char *version_domain, uint8_t maxlen);

// --- runtime-freq-v2 additions ---------------------------------------------

#define FHSS_FREQ_NAME_MAXLEN 16

// Slot indices into the runtime FHSS config pool. Rendezvous is the compile-time
// domain established at boot; Active is what the getters read (== Rendezvous
// until the negotiation path switches it); Staged is written during negotiation.
enum FHSSConfigSlot : uint8_t {
    FHSS_SLOT_RENDEZVOUS = 0,
    FHSS_SLOT_ACTIVE     = 1,
    FHSS_SLOT_STAGED     = 2,
    FHSS_SLOT_COUNT      = 3,
};

// Inputs to FHSSbuildConfig. Frequencies are in register-value units (already
// passed through FREQ_HZ_TO_REG_VAL by the caller); Hz→reg conversion happens
// at boundaries (wire protocol, JSON loader), not inside this struct.
struct FHSSFreqParams {
    uint32_t freq_start;
    uint32_t freq_stop;
    uint8_t  freq_count;    // 2..FHSS_SEQUENCE_LEN; 255 is the hard upper bound
    uint8_t  sync_channel;  // < freq_count
    char     name[FHSS_FREQ_NAME_MAXLEN];
};

// A fully-built runtime FHSS config. Once built, treat as immutable — the
// pointer-swap activation path assumes the content doesn't change under it.
struct FHSSFreqConfig {
    FHSSFreqParams params;
    uint32_t       freq_spread;   // (stop-start) * FREQ_SPREAD_SCALE / (count-1)
    uint16_t       band_count;    // (FHSS_SEQUENCE_LEN / count) * count
    uint8_t        sequence[FHSS_SEQUENCE_LEN];
};

// Build an FHSSFreqConfig from params + seed into dst.
// Pure wrt dst: deterministic, same inputs → same output.
// Caveat: shares the random.h RNG state (rngSeed), so not re-entrant across
// threads/ISRs. That matches every other FHSS builder in this codebase.
// Returns false on invalid params (count<2, start>=stop, sync>=count, null ptr).
bool FHSSbuildConfig(FHSSFreqConfig *dst, const FHSSFreqParams *params, uint32_t seed);

// Access to the runtime config pool. PR 1 exposes these for tests; the
// rendezvous / active wiring lands in PR 2.
FHSSFreqConfig *FHSSgetPoolSlot(FHSSConfigSlot slot);

static inline uint32_t FHSSgetMinimumFreq(void)
{
    return FHSSconfig->freq_start;
}

static inline uint32_t FHSSgetMaximumFreq(void)
{
    return FHSSconfig->freq_stop;
}

// The number of frequencies for this regulatory domain
static inline uint32_t FHSSgetChannelCount(void)
{
    if (FHSSusePrimaryFreqBand)
    {
        return FHSSconfig->freq_count;
    }
    else
    {
        return FHSSconfigDualBand->freq_count;
    }
}

// get the number of entries in the FHSS sequence
static inline uint16_t FHSSgetSequenceCount()
{
    if (FHSSuseDualBand) // Use the smaller of the 2 bands as not to go beyond the max index for each sequence.
    {
        if (primaryBandCount < secondaryBandCount)
        {
            return primaryBandCount;
        }
        else
        {
            return secondaryBandCount;
        }
    }

    if (FHSSusePrimaryFreqBand)
    {
        return primaryBandCount;
    }
    else
    {
        return secondaryBandCount;
    }
}

// get the initial frequency, which is also the sync channel
static inline uint32_t FHSSgetInitialFreq()
{
    if (FHSSusePrimaryFreqBand)
    {
        return FHSSconfig->freq_start + (sync_channel * freq_spread / FREQ_SPREAD_SCALE) - FreqCorrection;
    }
    else
    {
        return FHSSconfigDualBand->freq_start + (sync_channel_DualBand * freq_spread_DualBand / FREQ_SPREAD_SCALE);
    }
}

// Get the current sequence pointer
static inline uint8_t FHSSgetCurrIndex()
{
    return FHSSptr;
}

// Is the current frequency the sync frequency
static inline uint8_t FHSSonSyncChannel()
{
    if (FHSSusePrimaryFreqBand)
    {
        return FHSSsequence[FHSSptr] == sync_channel;
    }
    else
    {
        return FHSSsequence_DualBand[FHSSptr] == sync_channel_DualBand;
    }
}

// Set the sequence pointer, used by RX on SYNC
static inline void FHSSsetCurrIndex(const uint8_t value)
{
    FHSSptr = value % FHSSgetSequenceCount();
}

// Advance the pointer to the next hop and return the frequency of that channel
static inline uint32_t FHSSgetNextFreq()
{
    FHSSptr = (FHSSptr + 1) % FHSSgetSequenceCount();

    if (FHSSusePrimaryFreqBand)
    {
        return FHSSconfig->freq_start + (freq_spread * FHSSsequence[FHSSptr] / FREQ_SPREAD_SCALE) - FreqCorrection;
    }
    else
    {
        return FHSSconfigDualBand->freq_start + (freq_spread_DualBand * FHSSsequence_DualBand[FHSSptr] / FREQ_SPREAD_SCALE);
    }
}

static inline const char *FHSSgetRegulatoryDomain()
{
    if (FHSSusePrimaryFreqBand)
    {
        return FHSSconfig->domain;
    }
    else
    {
        return FHSSconfigDualBand->domain;
    }
}

// Get frequency offset by half of the domain frequency range
static inline uint32_t FHSSGeminiFreq(uint8_t FHSSsequenceIdx)
{
    uint32_t freq;
    uint32_t numfhss = FHSSgetChannelCount();
    uint8_t offSetIdx = (FHSSsequenceIdx + (numfhss / 2)) % numfhss; 

    if (FHSSusePrimaryFreqBand)
    {
        freq = FHSSconfig->freq_start + (freq_spread * offSetIdx / FREQ_SPREAD_SCALE) - FreqCorrection_2;
    }
    else
    {
        freq = FHSSconfigDualBand->freq_start + (freq_spread_DualBand * offSetIdx / FREQ_SPREAD_SCALE);
    }

    return freq;
}

static inline uint32_t FHSSgetGeminiFreq()
{
    if (FHSSuseDualBand)
    {
        // When using Dual Band there is no need to calculate an offset frequency. Unlike Gemini with 2 frequencies in the same band.
        return FHSSconfigDualBand->freq_start + (FHSSsequence_DualBand[FHSSptr] * freq_spread_DualBand / FREQ_SPREAD_SCALE);
    }
    else
    {
        if (FHSSusePrimaryFreqBand)
        {
            return FHSSGeminiFreq(FHSSsequence[FHSSgetCurrIndex()]);
        }
        else
        {
            return FHSSGeminiFreq(FHSSsequence_DualBand[FHSSgetCurrIndex()]);
        }
    }
}

static inline uint32_t FHSSgetInitialGeminiFreq()
{
    if (FHSSuseDualBand)
    {
        return FHSSconfigDualBand->freq_start + (sync_channel_DualBand * freq_spread_DualBand / FREQ_SPREAD_SCALE);
    }
    else
    {
        if (FHSSusePrimaryFreqBand)
        {
            return FHSSGeminiFreq(sync_channel);
        }
        else
        {
            return FHSSGeminiFreq(sync_channel_DualBand);
        }
    }
}

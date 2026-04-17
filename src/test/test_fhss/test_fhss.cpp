#include <cstdint>
#include <SX1280_Regs.h>
#include <FHSS.h>
#include <unity.h>
#include <set>

void test_fhss_first(void)
{
    FHSSrandomiseFHSSsequence(0x01020304L);
    TEST_ASSERT_EQUAL(FHSSgetInitialFreq(), FHSSconfig->freq_start + freq_spread * sync_channel / FREQ_SPREAD_SCALE);
}

void test_fhss_assignment(void)
{
    FHSSrandomiseFHSSsequence(0x01020304L);

    const uint32_t numFhss = FHSSgetChannelCount();
    uint32_t initFreq = FHSSgetInitialFreq();

    uint32_t freq = initFreq;
    for (unsigned int i = 0; i < 512; i++) {
        if ((i % numFhss) == 0) {
            TEST_ASSERT_EQUAL(initFreq, freq);
        } else {
            TEST_ASSERT_NOT_EQUAL(initFreq, freq);
        }
        freq = FHSSgetNextFreq();
    }
}

void test_fhss_unique(void)
{
    FHSSrandomiseFHSSsequence(0x01020304L);

    const uint32_t numFhss = FHSSgetChannelCount();
    std::set<uint32_t> freqs;

    for (unsigned int i = 0; i < 256; i++) {
        uint32_t freq = FHSSgetNextFreq();

        if ((i % numFhss) == 0) {
            freqs.clear();
            freqs.insert(freq);
        } else {
            bool inserted = freqs.insert(freq).second;
            TEST_ASSERT_TRUE_MESSAGE(inserted, "Should only see a frequency one time per number initial value");
        }
    }
}

void test_fhss_same(void)
{
    FHSSrandomiseFHSSsequence(0x01020304L);

    const uint32_t numFhss = FHSSgetSequenceCount();

    uint32_t fhss[numFhss];

    for (unsigned int i = 0; i < FHSSgetSequenceCount(); i++) {
        uint32_t freq = FHSSgetNextFreq();
        fhss[i] = freq;
    }

    FHSSrandomiseFHSSsequence(0x01020304L);

    for (unsigned int i = 0; i < FHSSgetSequenceCount(); i++) {
        uint32_t freq = FHSSgetNextFreq();
        TEST_ASSERT_EQUAL(fhss[i],freq);
    }
}

void test_fhss_reg_same(void)
{
    FHSSrandomiseFHSSsequence(0x01020304L);

    const uint32_t numFhss = FHSSgetSequenceCount();

    uint32_t fhss[numFhss];

    for (unsigned int i = 1; i < FHSSgetSequenceCount(); i++) {
        uint32_t freq = FHSSgetNextFreq();
        uint32_t reg = FREQ_HZ_TO_REG_VAL((2400400000 + FHSSsequence[i]*1000000));
        TEST_ASSERT_UINT32_WITHIN(1, reg, freq);
    }
}

// --- runtime-freq-v2: FHSSbuildConfig tests -------------------------------

static FHSSFreqParams sampleParams()
{
    FHSSFreqParams p{};
    // AU915-ish in reg-val units via the local FREQ_HZ_TO_REG_VAL
    p.freq_start   = FREQ_HZ_TO_REG_VAL(915500000UL);
    p.freq_stop    = FREQ_HZ_TO_REG_VAL(926900000UL);
    p.freq_count   = 20;
    p.sync_channel = 10;
    // name explicitly zeroed so memcmp-style checks are stable
    for (uint8_t i = 0; i < FHSS_FREQ_NAME_MAXLEN; i++) p.name[i] = 0;
    p.name[0] = 'T'; p.name[1] = 'S'; p.name[2] = 'T';
    return p;
}

void test_FHSSbuildConfig_deterministic(void)
{
    FHSSFreqParams p = sampleParams();
    FHSSFreqConfig a{}, b{};
    TEST_ASSERT_TRUE(FHSSbuildConfig(&a, &p, 0xDEADBEEFUL));
    TEST_ASSERT_TRUE(FHSSbuildConfig(&b, &p, 0xDEADBEEFUL));
    TEST_ASSERT_EQUAL_UINT32(a.freq_spread, b.freq_spread);
    TEST_ASSERT_EQUAL_UINT16(a.band_count, b.band_count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(a.sequence, b.sequence, FHSS_SEQUENCE_LEN);
}

void test_FHSSbuildConfig_seed_matters(void)
{
    FHSSFreqParams p = sampleParams();
    FHSSFreqConfig a{}, b{};
    TEST_ASSERT_TRUE(FHSSbuildConfig(&a, &p, 0x11111111UL));
    TEST_ASSERT_TRUE(FHSSbuildConfig(&b, &p, 0x22222222UL));
    bool differs = false;
    for (uint16_t i = 0; i < a.band_count; i++) {
        if (a.sequence[i] != b.sequence[i]) { differs = true; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(differs, "Different seeds should yield different sequences");
}

void test_FHSSbuildConfig_rejects_invalid(void)
{
    FHSSFreqConfig c{};
    FHSSFreqParams bad = sampleParams();

    bad.freq_count = 1;
    TEST_ASSERT_FALSE_MESSAGE(FHSSbuildConfig(&c, &bad, 1), "count<2 must fail");

    bad = sampleParams();
    bad.freq_start = bad.freq_stop;
    TEST_ASSERT_FALSE_MESSAGE(FHSSbuildConfig(&c, &bad, 1), "start>=stop must fail");

    bad = sampleParams();
    bad.sync_channel = bad.freq_count;
    TEST_ASSERT_FALSE_MESSAGE(FHSSbuildConfig(&c, &bad, 1), "sync>=count must fail");

    FHSSFreqParams ok = sampleParams();
    TEST_ASSERT_FALSE_MESSAGE(FHSSbuildConfig(nullptr, &ok, 1), "null dst must fail");
    TEST_ASSERT_FALSE_MESSAGE(FHSSbuildConfig(&c, nullptr, 1), "null params must fail");
}

void test_FHSSbuildConfig_sequence_wellformed(void)
{
    FHSSFreqParams p = sampleParams();
    FHSSFreqConfig c{};
    TEST_ASSERT_TRUE(FHSSbuildConfig(&c, &p, 0xA5A5A5A5UL));

    // band_count rounds FHSS_SEQUENCE_LEN down to a multiple of freq_count
    TEST_ASSERT_EQUAL_UINT16((FHSS_SEQUENCE_LEN / p.freq_count) * p.freq_count, c.band_count);

    // Every entry is a valid channel index
    for (uint16_t i = 0; i < c.band_count; i++) {
        TEST_ASSERT_TRUE_MESSAGE(c.sequence[i] < p.freq_count, "entry must be < freq_count");
    }

    // Sync channel appears at the start of every freq_count-sized block
    for (uint16_t i = 0; i < c.band_count; i += p.freq_count) {
        TEST_ASSERT_EQUAL_UINT8(p.sync_channel, c.sequence[i]);
    }
}

void test_FHSSbuildConfig_pool_slots(void)
{
    // Each slot is a distinct memory region and round-trips independently.
    FHSSFreqConfig *rdv  = FHSSgetPoolSlot(FHSS_SLOT_RENDEZVOUS);
    FHSSFreqConfig *act  = FHSSgetPoolSlot(FHSS_SLOT_ACTIVE);
    FHSSFreqConfig *stg  = FHSSgetPoolSlot(FHSS_SLOT_STAGED);
    FHSSFreqConfig *bad  = FHSSgetPoolSlot((FHSSConfigSlot)FHSS_SLOT_COUNT);

    TEST_ASSERT_NOT_NULL(rdv);
    TEST_ASSERT_NOT_NULL(act);
    TEST_ASSERT_NOT_NULL(stg);
    TEST_ASSERT_NULL(bad);
    TEST_ASSERT_TRUE(rdv != act && act != stg && rdv != stg);

    FHSSFreqParams p = sampleParams();
    TEST_ASSERT_TRUE(FHSSbuildConfig(rdv, &p, 1));
    TEST_ASSERT_TRUE(FHSSbuildConfig(stg, &p, 2));
    // Slots hold independent sequences
    bool differs = false;
    for (uint16_t i = 0; i < rdv->band_count; i++) {
        if (rdv->sequence[i] != stg->sequence[i]) { differs = true; break; }
    }
    TEST_ASSERT_TRUE(differs);
}

// Unity setup/teardown
void setUp() {}
void tearDown() {}

int main(int argc, char **argv)
{
    UNITY_BEGIN();
    RUN_TEST(test_fhss_first);
    RUN_TEST(test_fhss_assignment);
    RUN_TEST(test_fhss_unique);
    RUN_TEST(test_fhss_same);
    RUN_TEST(test_fhss_reg_same);
    RUN_TEST(test_FHSSbuildConfig_deterministic);
    RUN_TEST(test_FHSSbuildConfig_seed_matters);
    RUN_TEST(test_FHSSbuildConfig_rejects_invalid);
    RUN_TEST(test_FHSSbuildConfig_sequence_wellformed);
    RUN_TEST(test_FHSSbuildConfig_pool_slots);
    UNITY_END();

    return 0;
}

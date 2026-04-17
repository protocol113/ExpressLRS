#include <unity.h>
#include <string.h>
#include <FreqStageMsg.h>

static FreqStageMsg sampleStage()
{
    FreqStageMsg m{};
    m.schema_version   = FREQ_STAGE_SCHEMA_VERSION;
    m.flags            = FREQ_STAGE_FLAG_HAS_PRIMARY;
    m.primary_start_hz = 915500000UL;
    m.primary_stop_hz  = 926900000UL;
    m.primary_count    = 20;
    m.primary_sync     = 10;
    m.db_start_hz      = 0;
    m.db_stop_hz       = 0;
    m.db_count         = 0;
    m.db_sync          = 0;
    m.epoch_nonce      = 0xDEADBEEFUL;
    return m;
}

static FreqStageMsg sampleStageDualBand()
{
    FreqStageMsg m = sampleStage();
    m.flags         = FREQ_STAGE_FLAG_HAS_PRIMARY | FREQ_STAGE_FLAG_HAS_DUALBAND;
    m.db_start_hz   = 2400400000UL;
    m.db_stop_hz    = 2479400000UL;
    m.db_count      = 80;
    m.db_sync       = 40;
    return m;
}

void test_crc16_known_vector(void)
{
    // CRC-16/CCITT-FALSE check values.
    TEST_ASSERT_EQUAL_HEX16(0x29B1, freqStageCrc16((const uint8_t*)"123456789", 9));
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, freqStageCrc16(nullptr, 0));
}

void test_stage_round_trip(void)
{
    FreqStageMsg src = sampleStage();
    uint8_t buf[FREQ_STAGE_WIRE_LEN];
    TEST_ASSERT_EQUAL(FREQ_STAGE_WIRE_LEN, encodeFreqStage(&src, buf, sizeof(buf)));

    FreqStageMsg dst{};
    TEST_ASSERT_TRUE(decodeFreqStage(buf, sizeof(buf), &dst));
    TEST_ASSERT_EQUAL_MEMORY(&src, &dst, sizeof(FreqStageMsg));
}

void test_stage_round_trip_dualband(void)
{
    FreqStageMsg src = sampleStageDualBand();
    uint8_t buf[FREQ_STAGE_WIRE_LEN];
    TEST_ASSERT_EQUAL(FREQ_STAGE_WIRE_LEN, encodeFreqStage(&src, buf, sizeof(buf)));

    FreqStageMsg dst{};
    TEST_ASSERT_TRUE(decodeFreqStage(buf, sizeof(buf), &dst));
    TEST_ASSERT_EQUAL_MEMORY(&src, &dst, sizeof(FreqStageMsg));
}

void test_stage_rejects_bad_crc(void)
{
    FreqStageMsg src = sampleStage();
    uint8_t buf[FREQ_STAGE_WIRE_LEN];
    encodeFreqStage(&src, buf, sizeof(buf));
    buf[5] ^= 0x01;  // flip a bit in the payload; CRC no longer matches

    FreqStageMsg dst{};
    TEST_ASSERT_FALSE(decodeFreqStage(buf, sizeof(buf), &dst));
    TEST_ASSERT_EQUAL(FREQ_ACK_BAD_CRC, decodeFreqStageStatus(buf, sizeof(buf), nullptr));
}

void test_stage_rejects_bad_version(void)
{
    FreqStageMsg src = sampleStage();
    src.schema_version = FREQ_STAGE_SCHEMA_VERSION + 1;
    uint8_t buf[FREQ_STAGE_WIRE_LEN];
    encodeFreqStage(&src, buf, sizeof(buf));  // CRC now covers the bad version

    FreqStageMsg dst{};
    TEST_ASSERT_FALSE(decodeFreqStage(buf, sizeof(buf), &dst));
    TEST_ASSERT_EQUAL(FREQ_ACK_BAD_VERSION, decodeFreqStageStatus(buf, sizeof(buf), nullptr));
}

void test_stage_rejects_short_buffer(void)
{
    FreqStageMsg src = sampleStage();
    uint8_t buf[FREQ_STAGE_WIRE_LEN];
    encodeFreqStage(&src, buf, sizeof(buf));

    FreqStageMsg dst{};
    TEST_ASSERT_FALSE(decodeFreqStage(buf, FREQ_STAGE_WIRE_LEN - 1, &dst));
    TEST_ASSERT_FALSE(decodeFreqStage(buf, FREQ_STAGE_WIRE_LEN + 1, &dst));
}

void test_stage_encode_rejects_short_buffer(void)
{
    FreqStageMsg src = sampleStage();
    uint8_t buf[FREQ_STAGE_WIRE_LEN - 1];
    TEST_ASSERT_EQUAL(0, encodeFreqStage(&src, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(0, encodeFreqStage(nullptr, buf, sizeof(buf)));
}

void test_ack_round_trip(void)
{
    FreqStageAck src{};
    src.schema_version = FREQ_STAGE_SCHEMA_VERSION;
    src.status         = FREQ_ACK_OK;
    src.epoch_nonce    = 0xCAFEBABEUL;

    uint8_t buf[FREQ_ACK_WIRE_LEN];
    TEST_ASSERT_EQUAL(FREQ_ACK_WIRE_LEN, encodeFreqAck(&src, buf, sizeof(buf)));

    FreqStageAck dst{};
    TEST_ASSERT_TRUE(decodeFreqAck(buf, sizeof(buf), &dst));
    TEST_ASSERT_EQUAL_MEMORY(&src, &dst, sizeof(FreqStageAck));

    buf[3] ^= 0x01;
    TEST_ASSERT_FALSE(decodeFreqAck(buf, sizeof(buf), &dst));
}

void test_ack_carries_status_codes(void)
{
    const uint8_t codes[] = {FREQ_ACK_OK, FREQ_ACK_BAD_CRC, FREQ_ACK_BAD_VERSION,
                             FREQ_ACK_UNSUPPORTED, FREQ_ACK_BUILD_FAILED};
    for (uint8_t i = 0; i < sizeof(codes); i++)
    {
        FreqStageAck src{FREQ_STAGE_SCHEMA_VERSION, codes[i], (uint32_t)(i + 1)};
        uint8_t buf[FREQ_ACK_WIRE_LEN];
        encodeFreqAck(&src, buf, sizeof(buf));
        FreqStageAck dst{};
        TEST_ASSERT_TRUE(decodeFreqAck(buf, sizeof(buf), &dst));
        TEST_ASSERT_EQUAL_UINT8(codes[i], dst.status);
        TEST_ASSERT_EQUAL_UINT32(i + 1, dst.epoch_nonce);
    }
}

void test_abort_round_trip(void)
{
    FreqAbort src{FREQ_STAGE_SCHEMA_VERSION, 0};
    uint8_t buf[FREQ_ABORT_WIRE_LEN];
    TEST_ASSERT_EQUAL(FREQ_ABORT_WIRE_LEN, encodeFreqAbort(&src, buf, sizeof(buf)));

    FreqAbort dst{};
    TEST_ASSERT_TRUE(decodeFreqAbort(buf, sizeof(buf), &dst));
    TEST_ASSERT_EQUAL_MEMORY(&src, &dst, sizeof(FreqAbort));

    buf[1] = 0xFF;  // mutate reserved; CRC no longer valid
    TEST_ASSERT_FALSE(decodeFreqAbort(buf, sizeof(buf), &dst));
}

void setUp() {}
void tearDown() {}

int main(int, char**)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc16_known_vector);
    RUN_TEST(test_stage_round_trip);
    RUN_TEST(test_stage_round_trip_dualband);
    RUN_TEST(test_stage_rejects_bad_crc);
    RUN_TEST(test_stage_rejects_bad_version);
    RUN_TEST(test_stage_rejects_short_buffer);
    RUN_TEST(test_stage_encode_rejects_short_buffer);
    RUN_TEST(test_ack_round_trip);
    RUN_TEST(test_ack_carries_status_codes);
    RUN_TEST(test_abort_round_trip);
    UNITY_END();
    return 0;
}

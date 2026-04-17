#include <unity.h>
#include <string.h>
#include <FreqStageMsg.h>
#include <FreqStageNegotiation.h>
#include <FHSS.h>

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

// --- Lead-time helper -----------------------------------------------------

void test_lead_nonces_scales_with_tlm_denom(void)
{
    // Higher tlm denom → proportionally longer lead because Stubborn
    // delivery + ACK both wait for telemetry slots.
    uint32_t lead1   = FreqStageComputeLeadNonces(1);
    uint32_t lead4   = FreqStageComputeLeadNonces(4);
    uint32_t lead128 = FreqStageComputeLeadNonces(128);

    // The floor is 500, so low denoms clamp. lead128 must clearly exceed
    // the floor (headroom ensures this).
    TEST_ASSERT_GREATER_THAN_UINT32(lead1, lead128);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(500, lead1);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(500, lead4);
}

void test_lead_nonces_floor_holds_at_denom_zero(void)
{
    // Defensive: denom == 0 would otherwise zero out the formula.
    uint32_t lead = FreqStageComputeLeadNonces(0);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(500, lead);
}

// --- RX-side handler (protocol glue) --------------------------------------

static void encodeSampleStage(uint8_t *buf, uint32_t epoch, uint8_t flags = FREQ_STAGE_FLAG_HAS_PRIMARY)
{
    FreqStageMsg m = sampleStage();
    m.flags        = flags;
    m.epoch_nonce  = epoch;
    encodeFreqStage(&m, buf, FREQ_STAGE_WIRE_LEN);
}

void test_rx_handle_stage_good_path(void)
{
    // Fresh rebind boundary — clear any previous test's dup cache.
    FreqStageRxResetDuplicateCache();
    FHSSrandomiseFHSSsequence(0x12345678UL);

    uint8_t buf[FREQ_STAGE_WIRE_LEN];
    encodeSampleStage(buf, /*epoch=*/1000);

    FreqStageMsg out{};
    FreqStageAckStatus s = FreqStageRxHandleStage(
        buf, FREQ_STAGE_WIRE_LEN, /*uidSeed=*/0x12345678UL, /*currentNonce=*/10, &out);

    TEST_ASSERT_EQUAL(FREQ_ACK_OK, s);
    TEST_ASSERT_EQUAL_UINT32(1000, out.epoch_nonce);
    // State machine should be STAGED with our pool slot as staged config.
    TEST_ASSERT_EQUAL_PTR(FHSSgetPoolSlot(FHSS_SLOT_STAGED), FHSSgetStagedConfig());
    TEST_ASSERT_EQUAL(FHSS_STATE_STAGED, FHSSgetRuntimeState());
}

void test_rx_handle_stage_duplicate_is_idempotent(void)
{
    FreqStageRxResetDuplicateCache();
    FHSSrandomiseFHSSsequence(0x12345678UL);

    uint8_t buf[FREQ_STAGE_WIRE_LEN];
    encodeSampleStage(buf, /*epoch=*/1000);

    // First delivery
    FreqStageMsg out{};
    TEST_ASSERT_EQUAL(FREQ_ACK_OK,
        FreqStageRxHandleStage(buf, FREQ_STAGE_WIRE_LEN, 0x12345678UL, 10, &out));
    // Move state machine forward: epoch reached + swapped + acked.
    FHSSactivateIfEpochReached(1000);

    // Stubborn retransmits → duplicate STAGE with same epoch. Must re-ack
    // OK without mutating state (otherwise we thrash STAGED → SWITCHING).
    FHSSRuntimeState before = FHSSgetRuntimeState();
    TEST_ASSERT_EQUAL(FREQ_ACK_OK,
        FreqStageRxHandleStage(buf, FREQ_STAGE_WIRE_LEN, 0x12345678UL, 1500, &out));
    TEST_ASSERT_EQUAL(before, FHSSgetRuntimeState());
    TEST_ASSERT_NULL_MESSAGE(FHSSgetStagedConfig(),
        "duplicate must not repopulate staged slot");
}

void test_rx_handle_stage_rejects_stale_epoch(void)
{
    FreqStageRxResetDuplicateCache();
    FHSSrandomiseFHSSsequence(0x12345678UL);

    uint8_t buf[FREQ_STAGE_WIRE_LEN];
    encodeSampleStage(buf, /*epoch=*/100);  // epoch in the past

    FreqStageMsg out{};
    FreqStageAckStatus s = FreqStageRxHandleStage(
        buf, FREQ_STAGE_WIRE_LEN, 0x12345678UL, /*currentNonce=*/500, &out);

    TEST_ASSERT_EQUAL(FREQ_ACK_BAD_VERSION, s);
    TEST_ASSERT_NULL(FHSSgetStagedConfig());
}

void test_rx_handle_stage_dualband_unsupported(void)
{
    FreqStageRxResetDuplicateCache();
    FHSSrandomiseFHSSsequence(0x12345678UL);

    uint8_t buf[FREQ_STAGE_WIRE_LEN];
    encodeSampleStage(buf, 1000,
        FREQ_STAGE_FLAG_HAS_PRIMARY | FREQ_STAGE_FLAG_HAS_DUALBAND);

    FreqStageAckStatus s = FreqStageRxHandleStage(
        buf, FREQ_STAGE_WIRE_LEN, 0x12345678UL, 10, nullptr);

    TEST_ASSERT_EQUAL(FREQ_ACK_UNSUPPORTED, s);
    TEST_ASSERT_NULL(FHSSgetStagedConfig());
}

void test_rx_handle_stage_bad_crc(void)
{
    FreqStageRxResetDuplicateCache();
    uint8_t buf[FREQ_STAGE_WIRE_LEN];
    encodeSampleStage(buf, 1000);
    buf[3] ^= 0xFF;  // corrupt payload, CRC no longer matches

    FreqStageAckStatus s = FreqStageRxHandleStage(
        buf, FREQ_STAGE_WIRE_LEN, 0x12345678UL, 10, nullptr);
    TEST_ASSERT_EQUAL(FREQ_ACK_BAD_CRC, s);
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
    RUN_TEST(test_lead_nonces_scales_with_tlm_denom);
    RUN_TEST(test_lead_nonces_floor_holds_at_denom_zero);
    RUN_TEST(test_rx_handle_stage_good_path);
    RUN_TEST(test_rx_handle_stage_duplicate_is_idempotent);
    RUN_TEST(test_rx_handle_stage_rejects_stale_epoch);
    RUN_TEST(test_rx_handle_stage_dualband_unsupported);
    RUN_TEST(test_rx_handle_stage_bad_crc);
    UNITY_END();
    return 0;
}

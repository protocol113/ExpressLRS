#include "unity.h"

#include "LinkCrypto.h"
#include "OTA.h"
#include "common.h"

#include <cstring>

extern volatile uint8_t OtaNonce;
extern uint16_t OtaCrcInitializer;
uint8_t UID[UID_LEN] = {0};

static firmware_options_t makeOptions()
{
    firmware_options_t options {};
    options.hasLinkCryptoKey = true;
    options.link_crypto_enabled = true;
    for (unsigned i = 0; i < LINK_CRYPTO_KEY_LEN; ++i)
    {
        options.link_crypto_key[i] = (uint8_t)(0x10 + i);
    }
    return options;
}

static OTA_Packet_s makeStdPacket(uint8_t packageIndex, uint8_t seed)
{
    OTA_Packet_s packet {};
    packet.std.type = PACKET_TYPE_DATA;
    packet.std.data_ul.packageIndex = packageIndex;
    for (unsigned i = 0; i < sizeof(packet.std.data_ul.payload); ++i)
    {
        packet.std.data_ul.payload[i] = (uint8_t)(seed + i);
    }
    OtaGeneratePacketCrc(&packet);
    return packet;
}

void setUp()
{
    OtaNonce = 7;
    OtaCrcInitializer = 0x1234;
    OtaUpdateSerializers(smWideOr8ch, OTA4_PACKET_SIZE);
}

void test_linkcrypto_handshake_and_uplink_roundtrip()
{
    link_crypto_ctx_t tx {};
    link_crypto_ctx_t rx {};
    auto options = makeOptions();
    LinkCryptoInit(&tx, true, options);
    LinkCryptoInit(&rx, false, options);

    const uint8_t txNonce[LINK_CRYPTO_NONCE_LEN] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t mutableTxNonce[LINK_CRYPTO_NONCE_LEN];
    memcpy(mutableTxNonce, txNonce, sizeof(mutableTxNonce));
    uint8_t rxNonce[LINK_CRYPTO_NONCE_LEN] = {9, 10, 11, 12, 13, 14, 15, 16};

    TEST_ASSERT_TRUE(LinkCryptoBeginProposal(&tx, mutableTxNonce));
    TEST_ASSERT_TRUE(LinkCryptoHandleProposal(&rx, txNonce, rxNonce));
    TEST_ASSERT_TRUE(LinkCryptoHandleAccept(&tx, rxNonce));

    TEST_ASSERT_EQUAL(LINK_CRYPTO_STATE_WAIT_SYNC, LinkCryptoGetState(&tx));
    TEST_ASSERT_EQUAL(LINK_CRYPTO_STATE_WAIT_SYNC, LinkCryptoGetState(&rx));

    LinkCryptoOnSyncSent(&tx, true);
    LinkCryptoOnSyncReceived(&rx, true);

    TEST_ASSERT_TRUE(LinkCryptoIsActive(&tx));
    TEST_ASSERT_TRUE(LinkCryptoIsActive(&rx));

    OTA_Packet_s packet = makeStdPacket(3, 0x41);
    OTA_Packet_s original = packet;

    TEST_ASSERT_TRUE(LinkCryptoEncrypt(&tx, LINK_CRYPTO_UPLINK, &packet, OTA4_PACKET_SIZE));
    TEST_ASSERT_TRUE(LinkCryptoDecrypt(&rx, LINK_CRYPTO_UPLINK, &packet, OTA4_PACKET_SIZE));
    TEST_ASSERT_EQUAL(original.std.type, packet.std.type);
    TEST_ASSERT_EQUAL(original.std.data_ul.packageIndex, packet.std.data_ul.packageIndex);
    TEST_ASSERT_EQUAL_MEMORY(original.std.data_ul.payload, packet.std.data_ul.payload, sizeof(packet.std.data_ul.payload));
}

void test_linkcrypto_counter_lookahead_survives_drop()
{
    link_crypto_ctx_t tx {};
    link_crypto_ctx_t rx {};
    auto options = makeOptions();
    LinkCryptoInit(&tx, true, options);
    LinkCryptoInit(&rx, false, options);

    uint8_t txNonce[LINK_CRYPTO_NONCE_LEN] = {1, 1, 2, 3, 5, 8, 13, 21};
    uint8_t rxNonce[LINK_CRYPTO_NONCE_LEN] = {34, 55, 89, 144, 1, 2, 3, 4};

    TEST_ASSERT_TRUE(LinkCryptoBeginProposal(&tx, txNonce));
    TEST_ASSERT_TRUE(LinkCryptoHandleProposal(&rx, txNonce, rxNonce));
    TEST_ASSERT_TRUE(LinkCryptoHandleAccept(&tx, rxNonce));
    LinkCryptoOnSyncSent(&tx, true);
    LinkCryptoOnSyncReceived(&rx, true);

    OTA_Packet_s packet1 = makeStdPacket(1, 0x20);
    OTA_Packet_s packet2 = makeStdPacket(2, 0x30);
    OTA_Packet_s original2 = packet2;

    TEST_ASSERT_TRUE(LinkCryptoEncrypt(&tx, LINK_CRYPTO_UPLINK, &packet1, OTA4_PACKET_SIZE));
    TEST_ASSERT_TRUE(LinkCryptoEncrypt(&tx, LINK_CRYPTO_UPLINK, &packet2, OTA4_PACKET_SIZE));

    TEST_ASSERT_TRUE(LinkCryptoDecrypt(&rx, LINK_CRYPTO_UPLINK, &packet2, OTA4_PACKET_SIZE));
    TEST_ASSERT_EQUAL(original2.std.type, packet2.std.type);
    TEST_ASSERT_EQUAL(original2.std.data_ul.packageIndex, packet2.std.data_ul.packageIndex);
    TEST_ASSERT_EQUAL_MEMORY(original2.std.data_ul.payload, packet2.std.data_ul.payload, sizeof(packet2.std.data_ul.payload));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_linkcrypto_handshake_and_uplink_roundtrip);
    RUN_TEST(test_linkcrypto_counter_lookahead_survives_drop);
    return UNITY_END();
}

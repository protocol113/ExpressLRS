#pragma once

#include <cstddef>
#include <cstdint>

#include "OTA.h"
#include "options.h"

constexpr uint8_t LINK_CRYPTO_NONCE_LEN = 8;
constexpr uint8_t LINK_CRYPTO_VERSION = 1;
constexpr uint8_t LINK_CRYPTO_LOOKAHEAD = 3;

enum link_crypto_state_t : uint8_t
{
    LINK_CRYPTO_STATE_DISABLED = 0,
    LINK_CRYPTO_STATE_OFF,
    LINK_CRYPTO_STATE_PROPOSED,
    LINK_CRYPTO_STATE_WAIT_SYNC,
    LINK_CRYPTO_STATE_ACTIVE,
};

enum link_crypto_direction_t : uint8_t
{
    LINK_CRYPTO_UPLINK = 0,
    LINK_CRYPTO_DOWNLINK = 1,
};

struct link_crypto_ctx_t
{
    bool isTx;
    bool enabled;
    bool haveRootKey;
    bool syncActivationPending;
    bool sawPeerProposal;
    link_crypto_state_t state;
    uint8_t rootKey[LINK_CRYPTO_KEY_LEN];
    uint8_t txNonce[LINK_CRYPTO_NONCE_LEN];
    uint8_t rxNonce[LINK_CRYPTO_NONCE_LEN];
    uint8_t uplinkKey[32];
    uint8_t downlinkKey[32];
    uint8_t uplinkNonce[12];
    uint8_t downlinkNonce[12];
    uint32_t uplinkTxCounter;
    uint32_t uplinkRxCounter;
    uint32_t downlinkTxCounter;
    uint32_t downlinkRxCounter;
};

void LinkCryptoInit(link_crypto_ctx_t *ctx, bool isTx, const firmware_options_t &options);
void LinkCryptoReset(link_crypto_ctx_t *ctx);
bool LinkCryptoIsEnabled(const link_crypto_ctx_t *ctx);
bool LinkCryptoIsActive(const link_crypto_ctx_t *ctx);
link_crypto_state_t LinkCryptoGetState(const link_crypto_ctx_t *ctx);
bool LinkCryptoBeginProposal(link_crypto_ctx_t *ctx, uint8_t txNonce[LINK_CRYPTO_NONCE_LEN]);
bool LinkCryptoHandleProposal(link_crypto_ctx_t *ctx, const uint8_t txNonce[LINK_CRYPTO_NONCE_LEN], uint8_t rxNonce[LINK_CRYPTO_NONCE_LEN]);
bool LinkCryptoHandleAccept(link_crypto_ctx_t *ctx, const uint8_t rxNonce[LINK_CRYPTO_NONCE_LEN]);
bool LinkCryptoShouldAdvertiseActivation(const link_crypto_ctx_t *ctx);
void LinkCryptoOnSyncSent(link_crypto_ctx_t *ctx, bool advertisedActivation);
void LinkCryptoOnSyncReceived(link_crypto_ctx_t *ctx, bool cryptoActive);
bool LinkCryptoEncrypt(link_crypto_ctx_t *ctx, link_crypto_direction_t direction, OTA_Packet_s *packet, size_t packetSize);
bool LinkCryptoDecrypt(link_crypto_ctx_t *ctx, link_crypto_direction_t direction, OTA_Packet_s *packet, size_t packetSize);
void LinkCryptoMakeNonce(uint8_t out[LINK_CRYPTO_NONCE_LEN], uint32_t seedA, uint32_t seedB);

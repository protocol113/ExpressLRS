#include "LinkCrypto.h"

#include <algorithm>
#include <cstring>

namespace
{
struct Sha256Ctx
{
    uint32_t state[8];
    uint64_t bitCount;
    uint8_t buffer[64];
    size_t bufferLen;
};

constexpr uint32_t kSha256[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

inline uint32_t rotr32(uint32_t value, uint8_t bits)
{
    return (value >> bits) | (value << (32 - bits));
}

void sha256Init(Sha256Ctx &ctx)
{
    ctx.state[0] = 0x6a09e667U;
    ctx.state[1] = 0xbb67ae85U;
    ctx.state[2] = 0x3c6ef372U;
    ctx.state[3] = 0xa54ff53aU;
    ctx.state[4] = 0x510e527fU;
    ctx.state[5] = 0x9b05688cU;
    ctx.state[6] = 0x1f83d9abU;
    ctx.state[7] = 0x5be0cd19U;
    ctx.bitCount = 0;
    ctx.bufferLen = 0;
}

void sha256Transform(Sha256Ctx &ctx, const uint8_t block[64])
{
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i)
    {
        w[i] = ((uint32_t)block[i * 4] << 24)
            | ((uint32_t)block[i * 4 + 1] << 16)
            | ((uint32_t)block[i * 4 + 2] << 8)
            | (uint32_t)block[i * 4 + 3];
    }
    for (unsigned i = 16; i < 64; ++i)
    {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ctx.state[0];
    uint32_t b = ctx.state[1];
    uint32_t c = ctx.state[2];
    uint32_t d = ctx.state[3];
    uint32_t e = ctx.state[4];
    uint32_t f = ctx.state[5];
    uint32_t g = ctx.state[6];
    uint32_t h = ctx.state[7];

    for (unsigned i = 0; i < 64; ++i)
    {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + s1 + ch + kSha256[i] + w[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx.state[0] += a;
    ctx.state[1] += b;
    ctx.state[2] += c;
    ctx.state[3] += d;
    ctx.state[4] += e;
    ctx.state[5] += f;
    ctx.state[6] += g;
    ctx.state[7] += h;
}

void sha256Update(Sha256Ctx &ctx, const uint8_t *data, size_t len)
{
    ctx.bitCount += (uint64_t)len * 8U;
    while (len > 0)
    {
        size_t copyLen = std::min(len, sizeof(ctx.buffer) - ctx.bufferLen);
        memcpy(&ctx.buffer[ctx.bufferLen], data, copyLen);
        ctx.bufferLen += copyLen;
        data += copyLen;
        len -= copyLen;
        if (ctx.bufferLen == sizeof(ctx.buffer))
        {
            sha256Transform(ctx, ctx.buffer);
            ctx.bufferLen = 0;
        }
    }
}

void sha256Final(Sha256Ctx &ctx, uint8_t out[32])
{
    ctx.buffer[ctx.bufferLen++] = 0x80;
    if (ctx.bufferLen > 56)
    {
        memset(&ctx.buffer[ctx.bufferLen], 0, sizeof(ctx.buffer) - ctx.bufferLen);
        sha256Transform(ctx, ctx.buffer);
        ctx.bufferLen = 0;
    }

    memset(&ctx.buffer[ctx.bufferLen], 0, 56 - ctx.bufferLen);
    for (unsigned i = 0; i < 8; ++i)
    {
        ctx.buffer[63 - i] = (uint8_t)(ctx.bitCount >> (i * 8));
    }
    sha256Transform(ctx, ctx.buffer);

    for (unsigned i = 0; i < 8; ++i)
    {
        out[i * 4] = (uint8_t)(ctx.state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(ctx.state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(ctx.state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)ctx.state[i];
    }
}

void sha256Digest(const uint8_t *data, size_t len, uint8_t out[32])
{
    Sha256Ctx ctx;
    sha256Init(ctx);
    sha256Update(ctx, data, len);
    sha256Final(ctx, out);
}

inline uint32_t load32le(const uint8_t *data)
{
    return (uint32_t)data[0]
        | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16)
        | ((uint32_t)data[3] << 24);
}

inline void store32le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

inline uint32_t quarterRoundA(uint32_t a, uint32_t b)
{
    a += b;
    return a;
}

void chacha20Block(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t output[64])
{
    static constexpr uint32_t constants[4] = {
        0x61707865U, 0x3320646eU, 0x79622d32U, 0x6b206574U
    };

    uint32_t state[16] = {
        constants[0], constants[1], constants[2], constants[3],
        load32le(&key[0]), load32le(&key[4]), load32le(&key[8]), load32le(&key[12]),
        load32le(&key[16]), load32le(&key[20]), load32le(&key[24]), load32le(&key[28]),
        counter, load32le(&nonce[0]), load32le(&nonce[4]), load32le(&nonce[8])
    };
    uint32_t working[16];
    memcpy(working, state, sizeof(state));

    auto qr = [&](int a, int b, int c, int d) {
        working[a] = quarterRoundA(working[a], working[b]); working[d] ^= working[a]; working[d] = rotr32(working[d], 16);
        working[c] = quarterRoundA(working[c], working[d]); working[b] ^= working[c]; working[b] = rotr32(working[b], 20);
        working[a] = quarterRoundA(working[a], working[b]); working[d] ^= working[a]; working[d] = rotr32(working[d], 24);
        working[c] = quarterRoundA(working[c], working[d]); working[b] ^= working[c]; working[b] = rotr32(working[b], 25);
    };

    for (unsigned round = 0; round < 10; ++round)
    {
        qr(0, 4, 8, 12);
        qr(1, 5, 9, 13);
        qr(2, 6, 10, 14);
        qr(3, 7, 11, 15);
        qr(0, 5, 10, 15);
        qr(1, 6, 11, 12);
        qr(2, 7, 8, 13);
        qr(3, 4, 9, 14);
    }

    for (unsigned i = 0; i < 16; ++i)
    {
        store32le(&output[i * 4], working[i] + state[i]);
    }
}

void chacha20Xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter, uint8_t *data, size_t len)
{
    uint8_t block[64];
    while (len > 0)
    {
        chacha20Block(key, nonce, counter++, block);
        size_t blockLen = std::min(len, sizeof(block));
        for (size_t i = 0; i < blockLen; ++i)
        {
            data[i] ^= block[i];
        }
        data += blockLen;
        len -= blockLen;
    }
}

void deriveBytes(const uint8_t rootKey[LINK_CRYPTO_KEY_LEN],
    const uint8_t txNonce[LINK_CRYPTO_NONCE_LEN],
    const uint8_t rxNonce[LINK_CRYPTO_NONCE_LEN],
    const char *label,
    uint8_t *out,
    size_t outLen)
{
    uint8_t digest[32];
    uint8_t buffer[LINK_CRYPTO_KEY_LEN + LINK_CRYPTO_NONCE_LEN + LINK_CRYPTO_NONCE_LEN + 32] = {0};
    memcpy(buffer, rootKey, LINK_CRYPTO_KEY_LEN);
    memcpy(buffer + LINK_CRYPTO_KEY_LEN, txNonce, LINK_CRYPTO_NONCE_LEN);
    memcpy(buffer + LINK_CRYPTO_KEY_LEN + LINK_CRYPTO_NONCE_LEN, rxNonce, LINK_CRYPTO_NONCE_LEN);
    size_t labelLen = strlen(label);
    memcpy(buffer + LINK_CRYPTO_KEY_LEN + (2 * LINK_CRYPTO_NONCE_LEN), label, labelLen);
    sha256Digest(buffer, LINK_CRYPTO_KEY_LEN + (2 * LINK_CRYPTO_NONCE_LEN) + labelLen, digest);
    memcpy(out, digest, outLen);
}

void deriveSession(link_crypto_ctx_t *ctx)
{
    deriveBytes(ctx->rootKey, ctx->txNonce, ctx->rxNonce, "elrs-uplink-key", ctx->uplinkKey, sizeof(ctx->uplinkKey));
    deriveBytes(ctx->rootKey, ctx->txNonce, ctx->rxNonce, "elrs-downlink-key", ctx->downlinkKey, sizeof(ctx->downlinkKey));
    deriveBytes(ctx->rootKey, ctx->txNonce, ctx->rxNonce, "elrs-uplink-iv", ctx->uplinkNonce, sizeof(ctx->uplinkNonce));
    deriveBytes(ctx->rootKey, ctx->txNonce, ctx->rxNonce, "elrs-downlink-iv", ctx->downlinkNonce, sizeof(ctx->downlinkNonce));
    ctx->uplinkTxCounter = 0;
    ctx->uplinkRxCounter = 0;
    ctx->downlinkTxCounter = 0;
    ctx->downlinkRxCounter = 0;
}

uint32_t &txCounter(link_crypto_ctx_t *ctx, link_crypto_direction_t direction)
{
    return direction == LINK_CRYPTO_UPLINK ? ctx->uplinkTxCounter : ctx->downlinkTxCounter;
}

uint32_t &rxCounter(link_crypto_ctx_t *ctx, link_crypto_direction_t direction)
{
    return direction == LINK_CRYPTO_UPLINK ? ctx->uplinkRxCounter : ctx->downlinkRxCounter;
}

uint8_t *keyFor(link_crypto_ctx_t *ctx, link_crypto_direction_t direction)
{
    return direction == LINK_CRYPTO_UPLINK ? ctx->uplinkKey : ctx->downlinkKey;
}

uint8_t *nonceFor(link_crypto_ctx_t *ctx, link_crypto_direction_t direction)
{
    return direction == LINK_CRYPTO_UPLINK ? ctx->uplinkNonce : ctx->downlinkNonce;
}

bool decryptWithCounter(link_crypto_ctx_t *ctx, link_crypto_direction_t direction, OTA_Packet_s *packet, size_t packetSize, uint32_t counter)
{
    uint8_t temp[OTA8_PACKET_SIZE];
    memcpy(temp, packet, packetSize);
    chacha20Xor(keyFor(ctx, direction), nonceFor(ctx, direction), counter, temp, packetSize);
    OTA_Packet_s *candidate = reinterpret_cast<OTA_Packet_s *>(temp);
    if (!OtaValidatePacketCrc(candidate))
    {
        return false;
    }
    memcpy(packet, temp, packetSize);
    return true;
}
}

void LinkCryptoInit(link_crypto_ctx_t *ctx, bool isTx, const firmware_options_t &options)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->isTx = isTx;
    ctx->enabled = options.link_crypto_enabled != 0;
    ctx->haveRootKey = options.hasLinkCryptoKey != 0;
    if (ctx->haveRootKey)
    {
        memcpy(ctx->rootKey, options.link_crypto_key, sizeof(ctx->rootKey));
        ctx->state = ctx->enabled ? LINK_CRYPTO_STATE_OFF : LINK_CRYPTO_STATE_DISABLED;
    }
    else
    {
        ctx->state = LINK_CRYPTO_STATE_DISABLED;
    }
}

void LinkCryptoReset(link_crypto_ctx_t *ctx)
{
    bool enabled = ctx->enabled;
    bool haveRootKey = ctx->haveRootKey;
    bool isTx = ctx->isTx;
    uint8_t rootKey[LINK_CRYPTO_KEY_LEN];
    memcpy(rootKey, ctx->rootKey, sizeof(rootKey));
    memset(ctx, 0, sizeof(*ctx));
    ctx->enabled = enabled;
    ctx->haveRootKey = haveRootKey;
    ctx->isTx = isTx;
    memcpy(ctx->rootKey, rootKey, sizeof(rootKey));
    ctx->state = (enabled && haveRootKey) ? LINK_CRYPTO_STATE_OFF : LINK_CRYPTO_STATE_DISABLED;
}

bool LinkCryptoIsEnabled(const link_crypto_ctx_t *ctx)
{
    return ctx->state != LINK_CRYPTO_STATE_DISABLED;
}

bool LinkCryptoIsActive(const link_crypto_ctx_t *ctx)
{
    return ctx->state == LINK_CRYPTO_STATE_ACTIVE;
}

link_crypto_state_t LinkCryptoGetState(const link_crypto_ctx_t *ctx)
{
    return ctx->state;
}

void LinkCryptoMakeNonce(uint8_t out[LINK_CRYPTO_NONCE_LEN], uint32_t seedA, uint32_t seedB)
{
    uint32_t state = seedA ^ 0x9e3779b9U;
    uint32_t mix = seedB ^ 0x7f4a7c15U;
    for (unsigned i = 0; i < LINK_CRYPTO_NONCE_LEN; ++i)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        mix = (mix * 1664525U) + 1013904223U + i;
        out[i] = (uint8_t)((state + rotr32(mix, (i % 4) * 8)) & 0xFFU);
    }
}

bool LinkCryptoBeginProposal(link_crypto_ctx_t *ctx, uint8_t txNonce[LINK_CRYPTO_NONCE_LEN])
{
    if (!ctx->enabled || !ctx->haveRootKey)
    {
        return false;
    }
    if (ctx->state == LINK_CRYPTO_STATE_ACTIVE || ctx->state == LINK_CRYPTO_STATE_WAIT_SYNC)
    {
        memcpy(txNonce, ctx->txNonce, LINK_CRYPTO_NONCE_LEN);
        return true;
    }

    memcpy(ctx->txNonce, txNonce, LINK_CRYPTO_NONCE_LEN);
    memset(ctx->rxNonce, 0, sizeof(ctx->rxNonce));
    ctx->syncActivationPending = false;
    ctx->sawPeerProposal = false;
    ctx->state = LINK_CRYPTO_STATE_PROPOSED;
    return true;
}

bool LinkCryptoHandleProposal(link_crypto_ctx_t *ctx, const uint8_t txNonce[LINK_CRYPTO_NONCE_LEN], uint8_t rxNonce[LINK_CRYPTO_NONCE_LEN])
{
    if (!ctx->enabled || !ctx->haveRootKey)
    {
        return false;
    }

    memcpy(ctx->txNonce, txNonce, LINK_CRYPTO_NONCE_LEN);
    if (!ctx->sawPeerProposal)
    {
        memcpy(ctx->rxNonce, rxNonce, LINK_CRYPTO_NONCE_LEN);
    }
    else
    {
        memcpy(rxNonce, ctx->rxNonce, LINK_CRYPTO_NONCE_LEN);
    }
    deriveSession(ctx);
    ctx->sawPeerProposal = true;
    ctx->syncActivationPending = true;
    ctx->state = LINK_CRYPTO_STATE_WAIT_SYNC;
    memcpy(rxNonce, ctx->rxNonce, LINK_CRYPTO_NONCE_LEN);
    return true;
}

bool LinkCryptoHandleAccept(link_crypto_ctx_t *ctx, const uint8_t rxNonce[LINK_CRYPTO_NONCE_LEN])
{
    if (!ctx->enabled || !ctx->haveRootKey || ctx->state != LINK_CRYPTO_STATE_PROPOSED)
    {
        return false;
    }

    memcpy(ctx->rxNonce, rxNonce, LINK_CRYPTO_NONCE_LEN);
    deriveSession(ctx);
    ctx->syncActivationPending = true;
    ctx->state = LINK_CRYPTO_STATE_WAIT_SYNC;
    return true;
}

bool LinkCryptoShouldAdvertiseActivation(const link_crypto_ctx_t *ctx)
{
    return ctx->state == LINK_CRYPTO_STATE_WAIT_SYNC && ctx->syncActivationPending && ctx->isTx;
}

void LinkCryptoOnSyncSent(link_crypto_ctx_t *ctx, bool advertisedActivation)
{
    if (advertisedActivation && ctx->state == LINK_CRYPTO_STATE_WAIT_SYNC)
    {
        ctx->syncActivationPending = false;
        ctx->state = LINK_CRYPTO_STATE_ACTIVE;
    }
}

void LinkCryptoOnSyncReceived(link_crypto_ctx_t *ctx, bool cryptoActive)
{
    if (cryptoActive && ctx->state == LINK_CRYPTO_STATE_WAIT_SYNC)
    {
        ctx->syncActivationPending = false;
        ctx->state = LINK_CRYPTO_STATE_ACTIVE;
    }
}

bool LinkCryptoEncrypt(link_crypto_ctx_t *ctx, link_crypto_direction_t direction, OTA_Packet_s *packet, size_t packetSize)
{
    if (!LinkCryptoIsActive(ctx) || packet->std.type == PACKET_TYPE_SYNC)
    {
        return false;
    }

    chacha20Xor(keyFor(ctx, direction), nonceFor(ctx, direction), txCounter(ctx, direction), reinterpret_cast<uint8_t *>(packet), packetSize);
    txCounter(ctx, direction)++;
    return true;
}

bool LinkCryptoDecrypt(link_crypto_ctx_t *ctx, link_crypto_direction_t direction, OTA_Packet_s *packet, size_t packetSize)
{
    if (!LinkCryptoIsActive(ctx))
    {
        return false;
    }

    uint32_t expected = rxCounter(ctx, direction);
    uint32_t candidates[LINK_CRYPTO_LOOKAHEAD + 2];
    size_t candidateCount = 0;
    candidates[candidateCount++] = expected;
    for (uint32_t i = 1; i <= LINK_CRYPTO_LOOKAHEAD; ++i)
    {
        candidates[candidateCount++] = expected + i;
    }
    if (expected > 0)
    {
        candidates[candidateCount++] = expected - 1;
    }

    for (size_t i = 0; i < candidateCount; ++i)
    {
        if (decryptWithCounter(ctx, direction, packet, packetSize, candidates[i]))
        {
            rxCounter(ctx, direction) = candidates[i] + 1;
            return true;
        }
    }

    return false;
}

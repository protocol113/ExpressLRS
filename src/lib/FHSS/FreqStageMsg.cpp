#include "FreqStageMsg.h"

// Little-endian scalar helpers — wire format is LE to match the rest of ELRS.
static inline void putU16(uint8_t *b, uint16_t v) { b[0] = v; b[1] = v >> 8; }
static inline void putU32(uint8_t *b, uint32_t v) { b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; }
static inline uint16_t getU16(const uint8_t *b) { return (uint16_t)b[0] | ((uint16_t)b[1] << 8); }
static inline uint32_t getU32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

uint16_t freqStageCrc16(const uint8_t *data, size_t len)
{
    // CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no xorout.
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (uint8_t bit = 0; bit < 8; bit++)
        {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

// ---- STAGE ---------------------------------------------------------------
// Layout (LE):
//   [0]    schema_version
//   [1]    flags
//   [2..5] primary_start_hz
//   [6..9] primary_stop_hz
//   [10]   primary_count
//   [11]   primary_sync
//   [12..15] db_start_hz
//   [16..19] db_stop_hz
//   [20]   db_count
//   [21]   db_sync
//   [22..25] epoch_nonce
//   [26..27] crc16 over [0..25]

size_t encodeFreqStage(const FreqStageMsg *msg, uint8_t *buf, size_t bufLen)
{
    if (msg == nullptr || buf == nullptr || bufLen < FREQ_STAGE_WIRE_LEN) return 0;
    buf[0]  = msg->schema_version;
    buf[1]  = msg->flags;
    putU32(&buf[2],  msg->primary_start_hz);
    putU32(&buf[6],  msg->primary_stop_hz);
    buf[10] = msg->primary_count;
    buf[11] = msg->primary_sync;
    putU32(&buf[12], msg->db_start_hz);
    putU32(&buf[16], msg->db_stop_hz);
    buf[20] = msg->db_count;
    buf[21] = msg->db_sync;
    putU32(&buf[22], msg->epoch_nonce);
    putU16(&buf[26], freqStageCrc16(buf, 26));
    return FREQ_STAGE_WIRE_LEN;
}

FreqStageAckStatus decodeFreqStageStatus(const uint8_t *buf, size_t len, FreqStageMsg *out)
{
    if (buf == nullptr || len != FREQ_STAGE_WIRE_LEN) return FREQ_ACK_BAD_CRC;
    const uint16_t expected = freqStageCrc16(buf, 26);
    const uint16_t actual   = getU16(&buf[26]);
    if (expected != actual) return FREQ_ACK_BAD_CRC;
    if (buf[0] != FREQ_STAGE_SCHEMA_VERSION) return FREQ_ACK_BAD_VERSION;
    if (out != nullptr)
    {
        out->schema_version   = buf[0];
        out->flags            = buf[1];
        out->primary_start_hz = getU32(&buf[2]);
        out->primary_stop_hz  = getU32(&buf[6]);
        out->primary_count    = buf[10];
        out->primary_sync     = buf[11];
        out->db_start_hz      = getU32(&buf[12]);
        out->db_stop_hz       = getU32(&buf[16]);
        out->db_count         = buf[20];
        out->db_sync          = buf[21];
        out->epoch_nonce      = getU32(&buf[22]);
    }
    return FREQ_ACK_OK;
}

bool decodeFreqStage(const uint8_t *buf, size_t len, FreqStageMsg *out)
{
    return decodeFreqStageStatus(buf, len, out) == FREQ_ACK_OK;
}

// ---- ACK -----------------------------------------------------------------
// Layout (LE):
//   [0] schema_version
//   [1] status
//   [2..5] epoch_nonce
//   [6..7] crc16 over [0..5]

size_t encodeFreqAck(const FreqStageAck *msg, uint8_t *buf, size_t bufLen)
{
    if (msg == nullptr || buf == nullptr || bufLen < FREQ_ACK_WIRE_LEN) return 0;
    buf[0] = msg->schema_version;
    buf[1] = msg->status;
    putU32(&buf[2], msg->epoch_nonce);
    putU16(&buf[6], freqStageCrc16(buf, 6));
    return FREQ_ACK_WIRE_LEN;
}

bool decodeFreqAck(const uint8_t *buf, size_t len, FreqStageAck *out)
{
    if (buf == nullptr || len != FREQ_ACK_WIRE_LEN) return false;
    if (freqStageCrc16(buf, 6) != getU16(&buf[6])) return false;
    if (buf[0] != FREQ_STAGE_SCHEMA_VERSION) return false;
    if (out != nullptr)
    {
        out->schema_version = buf[0];
        out->status         = buf[1];
        out->epoch_nonce    = getU32(&buf[2]);
    }
    return true;
}

// ---- ABORT ---------------------------------------------------------------
// Layout (LE):
//   [0] schema_version
//   [1] reserved (zero)
//   [2..3] crc16 over [0..1]

size_t encodeFreqAbort(const FreqAbort *msg, uint8_t *buf, size_t bufLen)
{
    if (msg == nullptr || buf == nullptr || bufLen < FREQ_ABORT_WIRE_LEN) return 0;
    buf[0] = msg->schema_version;
    buf[1] = msg->reserved;
    putU16(&buf[2], freqStageCrc16(buf, 2));
    return FREQ_ABORT_WIRE_LEN;
}

bool decodeFreqAbort(const uint8_t *buf, size_t len, FreqAbort *out)
{
    if (buf == nullptr || len != FREQ_ABORT_WIRE_LEN) return false;
    if (freqStageCrc16(buf, 2) != getU16(&buf[2])) return false;
    if (buf[0] != FREQ_STAGE_SCHEMA_VERSION) return false;
    if (out != nullptr)
    {
        out->schema_version = buf[0];
        out->reserved       = buf[1];
    }
    return true;
}

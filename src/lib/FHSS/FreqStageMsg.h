#pragma once

#include <stdint.h>
#include <stddef.h>

// Wire protocol for runtime frequency negotiation (runtime-freq-v2).
//
// Three message types ride under MSP_ELRS_RXTX_CONFIG as subcommand IDs:
//   FREQ_STAGE  (TX -> RX)  carries the next active config + a future
//                           OtaNonce at which both sides swap
//   FREQ_STAGE_ACK (RX -> TX) echoes the nonce + status
//   FREQ_ABORT  (either way) cancels a pending stage
//
// Frequencies travel in Hz — each side converts to register-value units with
// its local FREQ_HZ_TO_REG_VAL before building the FHSSFreqConfig. This keeps
// the wire format portable across radio chips that disagree on register units.

#define FREQ_STAGE_SCHEMA_VERSION  1

// Flags byte in STAGE payload.
#define FREQ_STAGE_FLAG_HAS_PRIMARY   (1u << 0)
#define FREQ_STAGE_FLAG_HAS_DUALBAND  (1u << 1)

// ACK status codes.
enum FreqStageAckStatus : uint8_t {
    FREQ_ACK_OK             = 0,
    FREQ_ACK_BAD_CRC        = 1,
    FREQ_ACK_BAD_VERSION    = 2,
    FREQ_ACK_UNSUPPORTED    = 3,  // e.g. dual-band requested on single-band radio
    FREQ_ACK_BUILD_FAILED   = 4,  // FHSSbuildConfig rejected the params
};

// ---- In-memory forms (host-side; not padded/packed for the wire) ---------

struct FreqStageMsg {
    uint8_t  schema_version;   // == FREQ_STAGE_SCHEMA_VERSION
    uint8_t  flags;            // FREQ_STAGE_FLAG_*
    uint32_t primary_start_hz;
    uint32_t primary_stop_hz;
    uint8_t  primary_count;
    uint8_t  primary_sync;
    uint32_t db_start_hz;
    uint32_t db_stop_hz;
    uint8_t  db_count;
    uint8_t  db_sync;
    uint32_t epoch_nonce;
};

struct FreqStageAck {
    uint8_t  schema_version;
    uint8_t  status;           // FreqStageAckStatus
    uint32_t epoch_nonce;      // echoes the STAGE being acked
};

struct FreqAbort {
    uint8_t  schema_version;
    uint8_t  reserved;         // zero; future flags
};

// Wire sizes including trailing CRC16.
#define FREQ_STAGE_WIRE_LEN  28
#define FREQ_ACK_WIRE_LEN     8
#define FREQ_ABORT_WIRE_LEN   4

// ---- Codec ---------------------------------------------------------------

// CRC-16/CCITT (polynomial 0x1021, init 0xFFFF). Exposed for tests.
uint16_t freqStageCrc16(const uint8_t *data, size_t len);

// Encode. Returns number of bytes written (== WIRE_LEN on success) or 0 on
// null-buffer input. Caller guarantees buf has at least WIRE_LEN bytes.
size_t encodeFreqStage(const FreqStageMsg *msg, uint8_t *buf, size_t bufLen);
size_t encodeFreqAck  (const FreqStageAck *msg, uint8_t *buf, size_t bufLen);
size_t encodeFreqAbort(const FreqAbort    *msg, uint8_t *buf, size_t bufLen);

// Decode. Returns true and fills `out` iff len == WIRE_LEN, CRC matches, and
// schema_version == FREQ_STAGE_SCHEMA_VERSION. False means don't trust the
// payload; caller should reply with FREQ_ACK_BAD_CRC or FREQ_ACK_BAD_VERSION.
// decodeFreqStageStatus() gives a finer-grained result for callers that want
// to distinguish CRC vs version failure in the ACK they send back.
bool decodeFreqStage(const uint8_t *buf, size_t len, FreqStageMsg *out);
bool decodeFreqAck  (const uint8_t *buf, size_t len, FreqStageAck *out);
bool decodeFreqAbort(const uint8_t *buf, size_t len, FreqAbort    *out);

FreqStageAckStatus decodeFreqStageStatus(const uint8_t *buf, size_t len, FreqStageMsg *out);

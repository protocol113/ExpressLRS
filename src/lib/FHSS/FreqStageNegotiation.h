#pragma once

// runtime-freq-v2: TX-side orchestration for FREQ_STAGE / FREQ_ABORT and the
// RX-side decode glue that bridges FreqStageMsg (wire) to FHSSbuildConfig +
// FHSSstageConfig (state machine). The codec itself lives in FreqStageMsg.h;
// this module is where the MSP handlers in RxTxEndpoint hand off to.

#include <stdint.h>
#include "FHSS.h"
#include "FreqStageMsg.h"

// --- Epoch lead-time ------------------------------------------------------

// Return the number of OtaNonces into the future at which a stage should
// take effect. Derived from the current telemetry denom because STAGE is
// delivered over the Stubborn uplink + ACK over the Stubborn downlink —
// both gated by telemetry cadence. A fixed constant is too short at
// high tlm ratios (1:128 @ 500 Hz ≈ 1.3 s best-case delivery).
uint32_t FreqStageComputeLeadNonces(uint8_t tlmDenom);

// --- TX-side send helpers -------------------------------------------------
// Available on both TARGET_TX and unit-test builds (production-gated from
// the RxTxEndpoint handler). Serializes + queues via crsfRouter.AddMspMessage
// so delivery rides Stubborn for reliable retransmission. Callers are
// expected to have validated link-up before invoking.

// Populate a FreqStageMsg from a built FHSSFreqConfig + current nonce.
// Returns the composed message with epoch_nonce already set.
FreqStageMsg FreqStageBuildMsgFromConfig(const FHSSFreqConfig *cfg, uint32_t currentNonce, uint8_t tlmDenom);

#if !defined(UNIT_TEST)

// Queue a STAGE MSP to the RX and stage locally with requireAck=true.
// Caller is responsible for validating that the link is up before calling
// (Lua Apply path enforces this at the UX layer in PR 5). tlmDenom is
// passed rather than read from globals so this library stays free of
// device-specific includes.
bool FreqStageSendStage(const FHSSFreqConfig *cfg, uint32_t currentNonce, uint8_t tlmDenom);

// Queue an ABORT MSP (cancels a pending stage on the far side).
bool FreqStageSendAbort(void);

#endif // !UNIT_TEST

// --- RX-side decode glue --------------------------------------------------

// Decode a raw STAGE payload and, on success, build the config into the
// STAGED pool slot and call FHSSstageConfig with requireAck=false.
// Returns the ACK status the handler should echo back. On any non-OK
// status, no state machine mutation occurs.
//
// uidSeed: the UID-derived seed used to build the hop sequence (must match
//          what TX used so sequences are identical on both sides).
// currentNonce: only used for duplicate-detection against last accepted
//               epoch; pass the RX's current OtaNonce at call time.
FreqStageAckStatus FreqStageRxHandleStage(const uint8_t *payload,
                                          uint32_t      payloadLen,
                                          uint32_t      uidSeed,
                                          uint32_t      currentNonce,
                                          FreqStageMsg *outMsg);

// Reset the RX-side duplicate-detection cache. Call from bind-enter paths
// so a stale last-accepted-epoch doesn't incorrectly reject a fresh stage
// after rebind.
void FreqStageRxResetDuplicateCache(void);

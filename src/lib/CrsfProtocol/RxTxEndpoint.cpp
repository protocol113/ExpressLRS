#include "RxTxEndpoint.h"

#if !defined(UNIT_TEST)

#include "CRSFRouter.h"
#include "rxtx_intf.h"
#include "config.h"
#include "logging.h"
#include "FHSS.h"
#include "FreqStageMsg.h"
#include "FreqStageNegotiation.h"
#include "OTA.h"
#include "common.h"

bool RxTxEndpoint::handleRxTxMessage(const crsf_header_t *message)
{
    const auto extMessage = (crsf_ext_header_t *)message;

    if (message->type == CRSF_FRAMETYPE_MSP_REQ && extMessage->payload[2] == MSP_ELRS_RXTX_CONFIG)
    {
        handleMspGetRxTxConfig(extMessage);
        return true;
    }
    if (message->type == CRSF_FRAMETYPE_MSP_WRITE && extMessage->payload[2] == MSP_ELRS_RXTX_CONFIG)
    {
        handleMspSetRxTxConfig(extMessage);
        return true;
    }

    return false;
}

/**
 * Handles REQ(get) of MSP_ELRS_RXTX_CONFIG command
 */
void RxTxEndpoint::handleMspGetRxTxConfig(crsf_ext_header_t *extMessage)
{
    switch ((MSP_ELRS_RXTX_CONFIG_SUBCMD)extMessage->payload[3])
    {
        case MSP_ELRS_RXTX_CONFIG_SUBCMD::UID:
            {
                mspPacket_t msp;
                msp.reset();
                msp.makeResponse();
                msp.function = MSP_ELRS_RXTX_CONFIG;
                msp.addByte((uint8_t)MSP_ELRS_RXTX_CONFIG_SUBCMD::UID);
                msp.addByte(UID[0]); msp.addByte(UID[1]); msp.addByte(UID[2]);
                msp.addByte(UID[3]); msp.addByte(UID[4]); msp.addByte(UID[5]);
                crsfRouter.AddMspMessage(&msp, extMessage->orig_addr, getDeviceId());
                break;
            }

        default:
            break;
    }
}

/**
 * Handles WRITE(set) of MSP_ELRS_RXTX_CONFIG command
 */
void RxTxEndpoint::handleMspSetRxTxConfig(crsf_ext_header_t *extMessage)
{
    // Encapsulated MSP header is (0x30, mspPayloadSize, command)
    // Subtract one from mspPayloadSize for the subcommand in payload[3]
    auto payloadLen = extMessage->payload[1] - 1;
    auto mspPayload = &extMessage->payload[4];

    switch ((MSP_ELRS_RXTX_CONFIG_SUBCMD)extMessage->payload[3])
    {
        case MSP_ELRS_RXTX_CONFIG_SUBCMD::UID:
            if (payloadLen > 5)
            {
                //DBGLN("Set UID");
                config.SetUID(mspPayload);
                scheduleRebootTime(200);
            }
            break;

        case MSP_ELRS_RXTX_CONFIG_SUBCMD::BIND_PHRASE:
            // 0 len payload supported to clear binding
            #if defined(DEBUG_LOG)
            mspPayload[payloadLen] = 0; // will overwrite CRC
            DBGLN("Set bindphrase '%s'", (char *)mspPayload);
            #endif
            config.SetBindPhrase(mspPayload, payloadLen);
            scheduleRebootTime(200);
            break;

        case MSP_ELRS_RXTX_CONFIG_SUBCMD::MODEL_ID:
            #if defined(TARGET_RX)
            if (payloadLen > 0)
            {
                DBGLN("Set ModelId=%u", extMessage->payload[4]);
                config.SetModelId(extMessage->payload[4]);
            }
            #endif
            break;

        // runtime-freq-v2: the RX handles STAGE (build + stage + ack back).
        // TX handles STAGE_ACK (notify the FHSS ack gate). ABORT is bi-directional.
        case MSP_ELRS_RXTX_CONFIG_SUBCMD::FREQ_STAGE:
            #if defined(TARGET_RX)
            {
                FreqStageMsg stageMsg{};
                FreqStageAckStatus status = FreqStageRxHandleStage(
                    mspPayload, payloadLen,
                    OtaGetUidSeed(),
                    OtaNonce,
                    &stageMsg);

                // Echo ACK with either OK + the epoch we accepted, or the
                // error code. TX uses this to open its ACK gate on the
                // stored staged epoch.
                FreqStageAck ack{};
                ack.schema_version = FREQ_STAGE_SCHEMA_VERSION;
                ack.status         = (uint8_t)status;
                ack.epoch_nonce    = stageMsg.epoch_nonce;
                uint8_t ackBuf[FREQ_ACK_WIRE_LEN];
                if (encodeFreqAck(&ack, ackBuf, sizeof(ackBuf)) == FREQ_ACK_WIRE_LEN)
                {
                    mspPacket_t out;
                    out.reset();
                    out.makeResponse();
                    out.function = MSP_ELRS_RXTX_CONFIG;
                    out.addByte((uint8_t)MSP_ELRS_RXTX_CONFIG_SUBCMD::FREQ_STAGE_ACK);
                    for (uint8_t i = 0; i < FREQ_ACK_WIRE_LEN; i++) out.addByte(ackBuf[i]);
                    crsfRouter.AddMspMessage(&out, extMessage->orig_addr, getDeviceId());
                }
            }
            #endif
            break;

        case MSP_ELRS_RXTX_CONFIG_SUBCMD::FREQ_STAGE_ACK:
            #if defined(TARGET_TX)
            {
                FreqStageAck ack{};
                if (decodeFreqAck(mspPayload, payloadLen, &ack))
                {
                    if (ack.status == FREQ_ACK_OK)
                    {
                        FHSSnotifyAckReceived(ack.epoch_nonce);
                    }
                    else
                    {
                        // RX rejected the stage — abort locally so we don't
                        // sit staged forever or swap to a config RX won't follow.
                        DBGLN("FREQ_STAGE rejected by RX, status=%u", ack.status);
                        FHSSabortStagedConfig();
                    }
                }
            }
            #endif
            break;

        case MSP_ELRS_RXTX_CONFIG_SUBCMD::FREQ_ABORT:
            {
                FreqAbort abort{};
                if (decodeFreqAbort(mspPayload, payloadLen, &abort))
                {
                    FHSSabortStagedConfig();
                }
            }
            break;

        default:
            break;
    }
}

#endif /* !UNIT_TEST */
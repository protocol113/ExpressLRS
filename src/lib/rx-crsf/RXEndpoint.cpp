#include "RXEndpoint.h"

#if !defined(UNIT_TEST)
#include "config.h"
#include "devMSPVTX.h"
#include "devVTXSPI.h"
#include "CRSFRouter.h"
#include "freqTable.h"
#include "FHSS.h"
#include "LinkCrypto.h"
#include "logging.h"
#include "msptypes.h"
#include "options.h"
#include <string.h>

extern void reset_into_bootloader();
extern void EnterBindingModeSafely();
extern link_crypto_ctx_t g_linkCryptoRx;
extern CRSFRouter crsfRouter;

static uint32_t readU32(const uint8_t *data)
{
    return (uint32_t)data[0]
        | ((uint32_t)data[1] << 8)
        | ((uint32_t)data[2] << 16)
        | ((uint32_t)data[3] << 24);
}

static void copyRuntimeLabel(char *target, const uint8_t *source)
{
    memcpy(target, source, RUNTIME_FREQ_LABEL_LEN + 1);
    target[RUNTIME_FREQ_LABEL_LEN] = '\0';
}

static void sendCryptoAccept(const uint8_t rxNonce[LINK_CRYPTO_NONCE_LEN])
{
    uint8_t buffer[CRSF_EXT_FRAME_SIZE(2 + 1 + LINK_CRYPTO_NONCE_LEN) + CRSF_FRAME_NOT_COUNTED_BYTES] = {0};
    auto *command = reinterpret_cast<crsf_ext_header_t *>(buffer);
    uint8_t *payload = buffer + sizeof(crsf_ext_header_t);
    payload[0] = CRSF_COMMAND_SUBCMD_ELRS;
    payload[1] = CRSF_COMMAND_SUBCMD_LINK_CRYPTO_ACCEPT;
    payload[2] = LINK_CRYPTO_VERSION;
    memcpy(&payload[3], rxNonce, LINK_CRYPTO_NONCE_LEN);
    crsfRouter.SetExtendedHeaderAndCrc(command, CRSF_FRAMETYPE_COMMAND, CRSF_EXT_FRAME_SIZE(2 + 1 + LINK_CRYPTO_NONCE_LEN), CRSF_ADDRESS_CRSF_TRANSMITTER, CRSF_ADDRESS_CRSF_RECEIVER);
    crsfRouter.deliverMessageTo(CRSF_ADDRESS_CRSF_TRANSMITTER, reinterpret_cast<crsf_header_t *>(buffer));
}

RXEndpoint::RXEndpoint()
    : CRSFEndpoint(CRSF_ADDRESS_CRSF_RECEIVER)
{
}

/**
 * Handle any non-CRSF commands that we receive
 * @param message
 * @return
 */
bool RXEndpoint::handleRaw(const crsf_header_t *message)
{
    if (message->sync_byte == CRSF_ADDRESS_CRSF_RECEIVER && message->frame_size >= 4 && message->type == CRSF_FRAMETYPE_COMMAND)
    {
        uint8_t *payload = (uint8_t *)message + sizeof(crsf_header_t);
        // Non CRSF, dest=b src=l -> reboot to bootloader
        if (payload[0] == 'b' && payload[1] == 'l')
        {
            reset_into_bootloader();
            return true;
        }
        if (payload[0] == 'b' && payload[1] == 'd')
        {
            EnterBindingModeSafely();
            return true;
        }
        if (payload[0] == 'm' && payload[1] == 'm')
        {
            config.SetModelId(payload[2]);
            return true;
        }
    }
    return false;
}

void RXEndpoint::handleMessage(const crsf_header_t *message)
{
    const auto extMessage = (crsf_ext_header_t *)message;

    if (message->type == CRSF_FRAMETYPE_COMMAND && extMessage->payload[0] == CRSF_COMMAND_SUBCMD_RX && extMessage->payload[1] == CRSF_COMMAND_SUBCMD_RX_BIND)
    {
        EnterBindingModeSafely();
    }
    else if (message->type == CRSF_FRAMETYPE_COMMAND
        && extMessage->payload[0] == CRSF_COMMAND_SUBCMD_ELRS
        && extMessage->payload[1] == CRSF_COMMAND_SUBCMD_LINK_CRYPTO_PROPOSE
        && extMessage->payload[2] == LINK_CRYPTO_VERSION)
    {
        uint8_t rxNonce[LINK_CRYPTO_NONCE_LEN];
        LinkCryptoMakeNonce(rxNonce, micros(), uidMacSeedGet() ^ millis() ^ OtaNonce);
        if (LinkCryptoHandleProposal(&g_linkCryptoRx, &extMessage->payload[3], rxNonce))
        {
            sendCryptoAccept(rxNonce);
        }
    }
    else if (message->type == CRSF_FRAMETYPE_COMMAND && extMessage->payload[0] == CRSF_COMMAND_SUBCMD_RX && extMessage->payload[1] == CRSF_COMMAND_SUBCMD_RX_RUNTIME_FREQ)
    {
        const uint8_t *payload = &extMessage->payload[2];
        firmwareOptions.runtime_freq_enabled = payload[0] != 0;
        firmwareOptions.runtime_freq_preset = payload[1];
        firmwareOptions.runtime_freq_count = payload[2];
        firmwareOptions.runtime_freq_start = readU32(&payload[3]);
        firmwareOptions.runtime_freq_stop = readU32(&payload[7]);
        copyRuntimeLabel(firmwareOptions.runtime_freq_label, &payload[11]);
        payload += 11 + RUNTIME_FREQ_LABEL_LEN + 1;

        firmwareOptions.runtime_high_freq_enabled = payload[0] != 0;
        firmwareOptions.runtime_high_freq_preset = payload[1];
        firmwareOptions.runtime_high_freq_count = payload[2];
        firmwareOptions.runtime_high_freq_start = readU32(&payload[3]);
        firmwareOptions.runtime_high_freq_stop = readU32(&payload[7]);
        copyRuntimeLabel(firmwareOptions.runtime_high_freq_label, &payload[11]);

        if (firmwareOptions.runtime_freq_enabled && !FHSSruntimeFreqValid())
        {
            DBGLN("Rejected invalid runtime freq payload");
            return;
        }
        if (firmwareOptions.runtime_high_freq_enabled && !FHSShighRuntimeFreqValid())
        {
            DBGLN("Rejected invalid runtime high freq payload");
            return;
        }

        saveOptions();
        FHSSactivatePendingRuntimeFrequencies();
        FHSSrandomiseFHSSsequence(uidMacSeedGet());
        EnterBindingModeSafely();
    }
    else if (message->type == CRSF_FRAMETYPE_MSP_WRITE && extMessage->payload[2] == MSP_SET_RX_CONFIG && extMessage->payload[3] == MSP_ELRS_MODEL_ID)
    {
        DBGLN("Set ModelId=%u", extMessage->payload[4]);
        config.SetModelId(extMessage->payload[4]);
    }
#if defined(PLATFORM_ESP32)
    else if (message->type == CRSF_FRAMETYPE_MSP_RESP)
    {
        mspVtxProcessPacket((uint8_t *)message);
    }
    else if (OPT_HAS_VTX_SPI && message->type == CRSF_FRAMETYPE_MSP_WRITE && extMessage->payload[2] == MSP_SET_VTX_CONFIG)
    {
        vtxSPIFrequency = getFreqByIdx(extMessage->payload[3]);
        if (extMessage->payload[1] >= 4) // If packet has 4 bytes it also contains power idx and pitmode.
        {
            vtxSPIPowerIdx = extMessage->payload[5];
            vtxSPIPitmode = extMessage->payload[6];
        }
        devicesTriggerEvent(EVENT_VTX_CHANGE);
    }
#endif
    else if (message->type == CRSF_FRAMETYPE_DEVICE_PING ||
             message->type == CRSF_FRAMETYPE_PARAMETER_READ ||
             message->type == CRSF_FRAMETYPE_PARAMETER_WRITE)
    {
        parameterUpdateReq(
            extMessage->orig_addr,
            false,
            extMessage->type,
            extMessage->payload,
            extMessage->frame_size - CRSF_FRAME_LENGTH_EXT_TYPE_CRC
        );
    }
}
#endif

#include "RXEndpoint.h"

#if !defined(UNIT_TEST)
#include "config.h"
#include "devMSPVTX.h"
#include "devVTXSPI.h"
#include "freqTable.h"
#include "FHSS.h"
#include "logging.h"
#include "msptypes.h"
#include "options.h"
#include <string.h>

extern void reset_into_bootloader();
extern void EnterBindingModeSafely();

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

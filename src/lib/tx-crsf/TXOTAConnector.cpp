#include "TXOTAConnector.h"

#include "common.h"
#include "stubborn_sender.h"

extern StubbornSender DataUlSender;

TXOTAConnector::TXOTAConnector()
{
    // add the devices that we know are reachable via this connector
    addDevice(CRSF_ADDRESS_CRSF_RECEIVER);
    addDevice(CRSF_ADDRESS_FLIGHT_CONTROLLER);
}

void TXOTAConnector::pumpSender()
{
    static bool transferActive = false;
    // sending is done and we need to update our flag
    if (transferActive)
    {
        // unlock buffer for msp messages
        unlockMessage();
        transferActive = false;
    }
    // we are not sending so look for next msp package
    if (!transferActive)
    {
        // if we have a new msp package start sending
        if (currentTransmissionLength > 0)
        {
            DataUlSender.SetDataToTransmit(currentTransmissionBuffer, currentTransmissionLength);
            transferActive = true;
        }
    }
}

void TXOTAConnector::resetOutputQueue()
{
    outputQueue.flush();
    currentTransmissionLength = 0;
}

void TXOTAConnector::unlockMessage()
{
    // the current msp message is sent so restore the next buffered write
    if (outputQueue.size() > 0)
    {
        outputQueue.lock();
        currentTransmissionLength = outputQueue.pop();
        outputQueue.popBytes(currentTransmissionBuffer, currentTransmissionLength);
        outputQueue.unlock();
    }
    else
    {
        // no msp message is ready to send currently
        currentTransmissionLength = 0;
    }
}

void TXOTAConnector::forwardMessage(const crsf_header_t *message)
{
    // runtime-freq-v2: allow uplink MSP queueing in any normal link state,
    // not just "connected". The original gate was surprising: on dual-band
    // Nomad hardware the downlink telemetry rides chip 2, so if chip 2 is
    // misaligned (e.g. during a half-complete high-band swap) the TX flips
    // connectionState to "disconnected" even though uplink chip 1 is still
    // transmitting fine. Under the old gate, Apply-a-new-preset MSPs got
    // silently dropped right at the doorway — the uplink STAGE never
    // reached RX, state diverged, bind-loss ensued.
    //
    // MODE_STATES is the enum sentinel — everything below it (connected,
    // tentative, awaitingModelId, disconnected) is a normal radio-active
    // state where the OTA packet loop is running and uplink can carry
    // MSP. Special modes (noCrossfire, bleJoystick, wifiUpdate, etc.)
    // above MODE_STATES aren't transmitting and should still drop.
    if (connectionState < MODE_STATES)
    {
        const uint8_t length = message->frame_size + 2;
        if (length > ELRS_DATA_UL_BUFFER)
        {
            return;
        }

        // store next msp message
        const auto data = (uint8_t *)message;
        if (currentTransmissionLength == 0)
        {
            for (uint8_t i = 0; i < length; i++)
            {
                currentTransmissionBuffer[i] = data[i];
            }
            currentTransmissionLength = length;
        }
        // store all write-requests since an update does send multiple writes
        else
        {
            outputQueue.lock();
            if (outputQueue.ensure(length + 1))
            {
                outputQueue.push(length);
                outputQueue.pushBytes((const uint8_t *)data, length);
            }
            outputQueue.unlock();
        }
    }
}

#include "TXModuleEndpoint.h"

#include "rxtx_intf.h"
#include "CRSFHandset.h"
#include "CRSFRouter.h"
#include "FHSS.h"
#include "OTA.h"
#include "POWERMGNT.h"
#include "config.h"
#include "helpers.h"
#include "deferred.h"
#include "crsf_protocol.h"
#include "msptypes.h"
#include <stdio.h>

#define STR_LUA_ALLAUX         "AUX1;AUX2;AUX3;AUX4;AUX5;AUX6;AUX7;AUX8;AUX9;AUX10"

#define STR_LUA_ALLAUX_UPDOWN  "AUX1" LUASYM_ARROW_UP ";AUX1" LUASYM_ARROW_DN ";AUX2" LUASYM_ARROW_UP ";AUX2" LUASYM_ARROW_DN \
                               ";AUX3" LUASYM_ARROW_UP ";AUX3" LUASYM_ARROW_DN ";AUX4" LUASYM_ARROW_UP ";AUX4" LUASYM_ARROW_DN \
                               ";AUX5" LUASYM_ARROW_UP ";AUX5" LUASYM_ARROW_DN ";AUX6" LUASYM_ARROW_UP ";AUX6" LUASYM_ARROW_DN \
                               ";AUX7" LUASYM_ARROW_UP ";AUX7" LUASYM_ARROW_DN ";AUX8" LUASYM_ARROW_UP ";AUX8" LUASYM_ARROW_DN \
                               ";AUX9" LUASYM_ARROW_UP ";AUX9" LUASYM_ARROW_DN ";AUX10" LUASYM_ARROW_UP ";AUX10" LUASYM_ARROW_DN

#if defined(RADIO_SX127X)
#define STR_LUA_PACKETRATES \
    "D50Hz(-112dBm);25Hz(-123dBm);50Hz(-120dBm);100Hz(-117dBm);100Hz Full(-112dBm);200Hz(-112dBm)"
#elif defined(RADIO_LR1121)
#define STR_LUA_PACKETRATES \
    "100Hz Full(-112dBm);150Hz(-112dBm);" \
    "50Hz(-115dBm);100Hz Full(-112dBm);150Hz(-112dBm);250Hz(-108dBm);333Hz Full(-105dBm);500Hz(-105dBm);" \
    "DK250(-103dBm);DK500(-103dBm);K1000(-103dBm);" \
    "D50Hz(-112dBm);25Hz(-123dBm);50Hz(-120dBm);100Hz(-117dBm);100Hz Full(-112dBm);200Hz(-112dBm);200Hz Full(-111dBm);250Hz(-111dBm);" \
    "K1000 Full(-101dBm)"
#elif defined(RADIO_SX128X)
#define STR_LUA_PACKETRATES \
    "50Hz(-115dBm);100Hz Full(-112dBm);150Hz(-112dBm);250Hz(-108dBm);333Hz Full(-105dBm);500Hz(-105dBm);" \
    "D250(-104dBm);D500(-104dBm);F500(-104dBm);F1000(-104dBm)"
#else
#error Invalid radio configuration!
#endif

#define HAS_RADIO (GPIO_PIN_SCK != UNDEF_PIN)

extern char backpackVersion[];

#if defined(Regulatory_Domain_EU_CE_2400)
#if defined(RADIO_LR1121)
char strPowerLevels[] = "10/10;25/25;25/50;25/100;25/250;25/500;25/1000;25/2000;MatchTX ";
#else
char strPowerLevels[] = "10;25;50;100;250;500;1000;2000;MatchTX ";
#endif
#else
char strPowerLevels[] = "10;25;50;100;250;500;1000;2000;MatchTX ";
#endif
static char pwrFolderDynamicName[] = "TX Power (1000 Dynamic)";
static char vtxFolderDynamicName[] = "VTX Admin (OFF:C:1 Aux11 )";
static char modelMatchUnit[] = " (ID: 00)";
static char tlmBandwidth[] = " (xxxxxbps)";
static constexpr char folderNameSeparator[2] = {' ',':'};
static constexpr char tlmRatios[] = "Std;Off;1:128;1:64;1:32;1:16;1:8;1:4;1:2;Race";
static constexpr char tlmRatiosMav[] = ";;;;;;;;1:2;";
static constexpr char switchmodeOpts4ch[] = "Wide;Hybrid";
static constexpr char switchmodeOpts4chMav[] = ";Hybrid";
static constexpr char switchmodeOpts8ch[] = "8ch;16ch Rate/2;12ch Mixed";
static constexpr char switchmodeOpts8chMav[] = ";16ch Rate/2;";
static constexpr char antennamodeOpts[] = "Gemini;Ant 1;Ant 2;Switch";
static constexpr char antennamodeOptsDualBand[] = "Gemini;;;";
static constexpr char linkModeOpts[] = "Normal;MAVLink";
static constexpr char luastrDvrAux[] = "Off;" STR_LUA_ALLAUX_UPDOWN;
static constexpr char luastrDvrDelay[] = "0s;5s;15s;30s;45s;1min;2min";
static constexpr char luastrHeadTrackingEnable[] = "Off;On;" STR_LUA_ALLAUX_UPDOWN;
static constexpr char luastrHeadTrackingStart[] = "EdgeTX;" STR_LUA_ALLAUX;
static constexpr char luastrOffOn[] = "Off;On";
static char luastrPacketRates[] = STR_LUA_PACKETRATES;

#if defined(RADIO_LR1121)
static char luastrRFBands[32];
static enum RFMode : uint8_t
{
    RF_MODE_900 = 0,
    RF_MODE_2G4 = 1,
    RF_MODE_DUAL = 2,
} rfMode;

static selectionParameter luaRFBand = {
    {"RF Band", CRSF_TEXT_SELECTION},
    0, // value
    luastrRFBands,
    STR_EMPTYSPACE
};
#endif

static selectionParameter luaAirRate = {
    {"Packet Rate", CRSF_TEXT_SELECTION},
    0, // value
    luastrPacketRates,
    STR_EMPTYSPACE
};

static selectionParameter luaTlmRate = {
    {"Telem Ratio", CRSF_TEXT_SELECTION},
    0, // value
    tlmRatios,
    tlmBandwidth
};

//----------------------------POWER------------------
static folderParameter luaPowerFolder = {
    {"TX Power", CRSF_FOLDER},pwrFolderDynamicName
};

static selectionParameter luaPower = {
    {"Max Power", CRSF_TEXT_SELECTION},
    0, // value
    strPowerLevels,
    "mW"
};

static selectionParameter luaDynamicPower = {
    {"Dynamic", CRSF_TEXT_SELECTION},
    0, // value
    "Off;Dyn;AUX9;AUX10;AUX11;AUX12",
    STR_EMPTYSPACE
};

static selectionParameter luaFanThreshold = {
    {"Fan Thresh", CRSF_TEXT_SELECTION},
    0, // value
    "10mW;25mW;50mW;100mW;250mW;500mW;1000mW;2000mW;Never",
    STR_EMPTYSPACE // units embedded so it won't display "NevermW"
};

#if defined(Regulatory_Domain_EU_CE_2400)
static stringParameter luaCELimit = {
#if defined(RADIO_LR1121)
    {"25/100mW 868M/2G4 CE LIMIT", CRSF_INFO},
#else
    {"100mW 2G4 CE LIMIT", CRSF_INFO},
#endif
    STR_EMPTYSPACE
};
#endif

//----------------------------POWER------------------

static selectionParameter luaSwitch = {
    {"Switch Mode", CRSF_TEXT_SELECTION},
    0, // value
    switchmodeOpts4ch,
    STR_EMPTYSPACE
};

static selectionParameter luaAntenna = {
    {"Antenna Mode", CRSF_TEXT_SELECTION},
    0, // value
    antennamodeOpts,
    STR_EMPTYSPACE
};

static selectionParameter luaLinkMode = {
    {"Link Mode", CRSF_TEXT_SELECTION},
    0, // value
    linkModeOpts,
    STR_EMPTYSPACE
};

static selectionParameter luaModelMatch = {
    {"Model Match", CRSF_TEXT_SELECTION},
    0, // value
    luastrOffOn,
    modelMatchUnit
};

static commandParameter luaBind = {
    {"Bind", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};

static stringParameter luaInfo = {
    {"Bad/Good", (crsf_value_type_e)(CRSF_INFO | CRSF_FIELD_ELRS_HIDDEN)},
    STR_EMPTYSPACE
};

static stringParameter luaELRSversion = {
    {version_domain, CRSF_INFO},
    commit
};

//---------------------------- WiFi -----------------------------
static folderParameter luaWiFiFolder = {
    {"WiFi Connectivity", CRSF_FOLDER}
};

static commandParameter luaWebUpdate = {
    {"Enable WiFi", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};

static commandParameter luaRxWebUpdate = {
    {"Enable Rx WiFi", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};

static commandParameter luaTxBackpackUpdate = {
    {"Enable Backpack WiFi", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};

static commandParameter luaVRxBackpackUpdate = {
    {"Enable VRx WiFi", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};
//---------------------------- WiFi -----------------------------

#if defined(PLATFORM_ESP32)
static commandParameter luaBLEJoystick = {
    {"BLE Joystick", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};
#endif

//----------------------------VTX ADMINISTRATOR------------------
static folderParameter luaVtxFolder = {
    {"VTX Administrator", CRSF_FOLDER},vtxFolderDynamicName
};

static selectionParameter luaVtxBand = {
    {"Band", CRSF_TEXT_SELECTION},
    0, // value
    "Off;A;B;E;F;R;L",
    STR_EMPTYSPACE
};

static int8Parameter luaVtxChannel = {
    {"Channel", CRSF_UINT8},
    0, // value
    1, // min
    8, // max
    STR_EMPTYSPACE
};

static selectionParameter luaVtxPwr = {
    {"Pwr Lvl", CRSF_TEXT_SELECTION},
    0, // value
    "-;1;2;3;4;5;6;7;8",
    STR_EMPTYSPACE
};

static selectionParameter luaVtxPit = {
    {"Pitmode", CRSF_TEXT_SELECTION},
    0, // value
    "Off;On;" STR_LUA_ALLAUX_UPDOWN,
    STR_EMPTYSPACE
};

static commandParameter luaVtxSend = {
    {"Send VTx", CRSF_COMMAND},
    lcsIdle, // step
    STR_EMPTYSPACE
};
//----------------------------VTX ADMINISTRATOR------------------

//---------------------------- BACKPACK ------------------
static folderParameter luaBackpackFolder = {
    {"Backpack", CRSF_FOLDER},
};

static selectionParameter luaBackpackEnable = {
    {"Backpack", CRSF_TEXT_SELECTION},
    0, // value
    luastrOffOn,
    STR_EMPTYSPACE};

static selectionParameter luaDvrAux = {
    {"DVR Rec", CRSF_TEXT_SELECTION},
    0, // value
    luastrDvrAux,
    STR_EMPTYSPACE};

static selectionParameter luaDvrStartDelay = {
    {"DVR Srt Dly", CRSF_TEXT_SELECTION},
    0, // value
    luastrDvrDelay,
    STR_EMPTYSPACE};

static selectionParameter luaDvrStopDelay = {
    {"DVR Stp Dly", CRSF_TEXT_SELECTION},
    0, // value
    luastrDvrDelay,
    STR_EMPTYSPACE};

static selectionParameter luaHeadTrackingEnableChannel = {
    {"HT Enable", CRSF_TEXT_SELECTION},
    0, // value
    luastrHeadTrackingEnable,
    STR_EMPTYSPACE};

static selectionParameter luaHeadTrackingStartChannel = {
    {"HT Start Channel", CRSF_TEXT_SELECTION},
    0, // value
    luastrHeadTrackingStart,
    STR_EMPTYSPACE};

static selectionParameter luaBackpackTelemetry = {
    {"Telemetry", CRSF_TEXT_SELECTION},
    0, // value
    "Off;ESPNOW;WiFi",
    STR_EMPTYSPACE};

static stringParameter luaBackpackVersion = {
    {"Version", CRSF_INFO},
    backpackVersion};

//---------------------------- BACKPACK ------------------

#if defined(RADIO_LR1121)
typedef struct {
    const char *label;
    uint32_t startHz;
    uint32_t stopHz;
    uint8_t channelCount;
} runtime_freq_preset_t;

#if (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define CRSF_U32_INIT(value) (uint32_t)(value)
#else
#define CRSF_U32_INIT(value) (uint32_t)__builtin_bswap32((uint32_t)(value))
#endif

static constexpr runtime_freq_preset_t runtimeFreqPresets[] = {
    {"Off", 0U, 0U, 0U},
    {"FCC915", 903500000U, 926900000U, 40U},
    {"AU915", 915500000U, 926900000U, 20U},
    {"EU868", 863275000U, 869575000U, 13U},
    {"TrainA", 840000000U, 860000000U, 32U},
    {"Custom", 903500000U, 926900000U, 40U},
};

static constexpr uint8_t RUNTIME_FREQ_PRESET_CUSTOM = ARRAY_SIZE(runtimeFreqPresets) - 1;
static constexpr char runtimeFreqPresetOptions[] = "Off;FCC915;AU915;EU868;TrainA;Custom";
#if defined(Regulatory_Domain_EU_CE_2400)
static constexpr char runtimeHighFreqPresetOptions[] = "Off;CE_LBT;SBand;Train24;Custom";
static constexpr const char *runtimeHighDefaultLabel = "CE_LBT";
#else
static constexpr char runtimeHighFreqPresetOptions[] = "Off;ISM2G4;SBand;Train24;Custom";
static constexpr const char *runtimeHighDefaultLabel = "ISM2G4";
#endif
static constexpr runtime_freq_preset_t runtimeHighFreqPresets[] = {
    {"Off", 0U, 0U, 0U},
    {runtimeHighDefaultLabel, 2400400000U, 2479400000U, 80U},
    {"SBand", 2300000000U, 2399000000U, 40U},
    {"Train24", 2000000000U, 2100000000U, 40U},
    {"Custom", 2400400000U, 2479400000U, 80U},
};
static constexpr uint8_t RUNTIME_HIGH_FREQ_PRESET_CUSTOM = ARRAY_SIZE(runtimeHighFreqPresets) - 1;
static char runtimeFreqStatus[40] = "Active: stock";
static char runtimeFreqCustomLabel[RUNTIME_FREQ_LABEL_LEN + 1] = "CUSTOM";
static char runtimeHighFreqCustomLabel[RUNTIME_FREQ_LABEL_LEN + 1] = "CUSTOM2G4";

static folderParameter luaRuntimeFreqFolder = {
    {"Runtime Freq", CRSF_FOLDER},
};

static selectionParameter luaRuntimeFreqPreset = {
    {"SubG Preset", CRSF_TEXT_SELECTION},
    0,
    runtimeFreqPresetOptions,
    STR_EMPTYSPACE
};

static selectionParameter luaRuntimeFreqEnable = {
    {"SubG Ovr", CRSF_TEXT_SELECTION},
    0,
    "Off;On",
    STR_EMPTYSPACE
};

static selectionParameter luaRuntimeFreqAutoCount = {
    {"SubG AutoCh", CRSF_TEXT_SELECTION},
    0,
    "Off;On",
    STR_EMPTYSPACE
};

static floatParameter luaRuntimeFreqStartMHz = {
    {"SubG Start", CRSF_FLOAT},
    {CRSF_U32_INIT(9035), CRSF_U32_INIT(7000), CRSF_U32_INIT(9600), CRSF_U32_INIT(9035), 1, CRSF_U32_INIT(5)},
    "MHz"
};

static floatParameter luaRuntimeFreqStopMHz = {
    {"SubG Stop", CRSF_FLOAT},
    {CRSF_U32_INIT(9269), CRSF_U32_INIT(7000), CRSF_U32_INIT(9600), CRSF_U32_INIT(9269), 1, CRSF_U32_INIT(5)},
    "MHz"
};

static int8Parameter luaRuntimeFreqCount = {
    {"SubG Chans", CRSF_UINT8},
    {.u = {40, 4, 80}},
    STR_EMPTYSPACE
};

static selectionParameter luaRuntimeHighFreqPreset = {
    {"High Preset", CRSF_TEXT_SELECTION},
    0,
    runtimeHighFreqPresetOptions,
    STR_EMPTYSPACE
};

static selectionParameter luaRuntimeHighFreqEnable = {
    {"High Ovr", CRSF_TEXT_SELECTION},
    0,
    "Off;On",
    STR_EMPTYSPACE
};

static selectionParameter luaRuntimeHighFreqAutoCount = {
    {"High AutoCh", CRSF_TEXT_SELECTION},
    0,
    "Off;On",
    STR_EMPTYSPACE
};

static floatParameter luaRuntimeHighFreqStartMHz = {
    {"High Start", CRSF_FLOAT},
    {CRSF_U32_INIT(24004), CRSF_U32_INIT(19000), CRSF_U32_INIT(25000), CRSF_U32_INIT(24004), 1, CRSF_U32_INIT(5)},
    "MHz"
};

static floatParameter luaRuntimeHighFreqStopMHz = {
    {"High Stop", CRSF_FLOAT},
    {CRSF_U32_INIT(24794), CRSF_U32_INIT(19000), CRSF_U32_INIT(25000), CRSF_U32_INIT(24794), 1, CRSF_U32_INIT(5)},
    "MHz"
};

static int8Parameter luaRuntimeHighFreqCount = {
    {"High Chans", CRSF_UINT8},
    {.u = {80, 4, 80}},
    STR_EMPTYSPACE
};

static commandParameter luaRuntimeFreqApply = {
    {"Apply & Rebind", CRSF_COMMAND},
    lcsIdle,
    STR_EMPTYSPACE
};

static stringParameter luaRuntimeFreqInfo = {
    {"X-Band uses SubG + High", CRSF_INFO},
    runtimeFreqStatus
};

static void formatRuntimeCustomLabel(char *target, char prefix, uint32_t startHz, uint32_t stopHz)
{
    const uint32_t startMHz10 = startHz / 100000U;
    const uint32_t stopMHz10 = stopHz / 100000U;
    snprintf(target, RUNTIME_FREQ_LABEL_LEN + 1, "%c%lu-%lu", prefix, (unsigned long)startMHz10, (unsigned long)stopMHz10);
}

static uint8_t calculateRuntimeChannelCount(uint32_t startHz, uint32_t stopHz, uint32_t targetSpacingHz)
{
    if (stopHz <= startHz || targetSpacingHz == 0U)
    {
        return 4U;
    }

    const uint32_t widthHz = stopHz - startHz;
    uint32_t channelCount = ((widthHz + (targetSpacingHz / 2U)) / targetSpacingHz) + 1U;
    if (channelCount < 4U)
    {
        channelCount = 4U;
    }
    else if (channelCount > 80U)
    {
        channelCount = 80U;
    }

    return (uint8_t)channelCount;
}

static void updateRuntimeFreqCountFromWidth()
{
    const uint32_t startHz = be32toh(luaRuntimeFreqStartMHz.properties.value) * 100000U;
    const uint32_t stopHz = be32toh(luaRuntimeFreqStopMHz.properties.value) * 100000U;
    luaRuntimeFreqCount.properties.u.value = calculateRuntimeChannelCount(startHz, stopHz, 600000U);
}

static void updateRuntimeHighFreqCountFromWidth()
{
    const uint32_t startHz = be32toh(luaRuntimeHighFreqStartMHz.properties.value) * 100000U;
    const uint32_t stopHz = be32toh(luaRuntimeHighFreqStopMHz.properties.value) * 100000U;
    luaRuntimeHighFreqCount.properties.u.value = calculateRuntimeChannelCount(startHz, stopHz, 1000000U);
}

static const char *resolveRuntimeCustomLabel(
    uint8_t preset,
    uint8_t customPreset,
    const runtime_freq_preset_t *presets,
    uint32_t startHz,
    uint32_t stopHz,
    char prefix,
    char *customLabel)
{
    if (preset < customPreset)
    {
        return presets[preset].label;
    }

    formatRuntimeCustomLabel(customLabel, prefix, startHz, stopHz);
    return customLabel;
}

static void updateRuntimeFreqStatus()
{
    const bool subgPending = FHSSruntimeFreqPending();
    const bool highPending = FHSShighRuntimeFreqPending();
    const char *subgLabel = firmwareOptions.runtime_freq_enabled
        ? (firmwareOptions.runtime_freq_label[0] ? firmwareOptions.runtime_freq_label : "CUSTOM")
        : "stock";
    const char *highLabel = firmwareOptions.runtime_high_freq_enabled
        ? (firmwareOptions.runtime_high_freq_label[0] ? firmwareOptions.runtime_high_freq_label : "HIGH")
        : "stock";

    if (subgPending || highPending)
    {
        snprintf(runtimeFreqStatus, sizeof(runtimeFreqStatus), "Pending: %s / %s", subgLabel, highLabel);
    }
    else if (firmwareOptions.runtime_freq_enabled || firmwareOptions.runtime_high_freq_enabled)
    {
        snprintf(runtimeFreqStatus, sizeof(runtimeFreqStatus), "Active: %s / %s", subgLabel, highLabel);
    }
    else
    {
        strlcpy(runtimeFreqStatus, "Active: stock", sizeof(runtimeFreqStatus));
    }
}

static void appendU32(mspPacket_t *packet, uint32_t value)
{
    packet->addByte(value & 0xFFU);
    packet->addByte((value >> 8) & 0xFFU);
    packet->addByte((value >> 16) & 0xFFU);
    packet->addByte((value >> 24) & 0xFFU);
}

static uint8_t appendRuntimeFreqPayload(uint8_t *payload, uint8_t enabled, uint8_t preset, uint8_t count, uint32_t start, uint32_t stop, const char *label)
{
    uint8_t offset = 0;
    payload[offset++] = enabled;
    payload[offset++] = preset;
    payload[offset++] = count;
    payload[offset++] = start & 0xFFU;
    payload[offset++] = (start >> 8) & 0xFFU;
    payload[offset++] = (start >> 16) & 0xFFU;
    payload[offset++] = (start >> 24) & 0xFFU;
    payload[offset++] = stop & 0xFFU;
    payload[offset++] = (stop >> 8) & 0xFFU;
    payload[offset++] = (stop >> 16) & 0xFFU;
    payload[offset++] = (stop >> 24) & 0xFFU;
    for (uint8_t i = 0; i < RUNTIME_FREQ_LABEL_LEN + 1; ++i)
    {
        payload[offset++] = label[i];
    }
    return offset;
}

static void sendRuntimeFreqConfigToRx()
{
    uint8_t frame[CRSF_MAX_PACKET_LEN] = {0};
    auto *command = reinterpret_cast<crsf_ext_header_t *>(frame);
    uint8_t *payload = command->payload;
    payload[0] = CRSF_COMMAND_SUBCMD_RX;
    payload[1] = CRSF_COMMAND_SUBCMD_RX_RUNTIME_FREQ;
    uint8_t offset = 2;
    offset += appendRuntimeFreqPayload(
        &payload[offset],
        firmwareOptions.runtime_freq_enabled ? 1U : 0U,
        firmwareOptions.runtime_freq_preset,
        firmwareOptions.runtime_freq_count,
        firmwareOptions.runtime_freq_start,
        firmwareOptions.runtime_freq_stop,
        firmwareOptions.runtime_freq_label);
    offset += appendRuntimeFreqPayload(
        &payload[offset],
        firmwareOptions.runtime_high_freq_enabled ? 1U : 0U,
        firmwareOptions.runtime_high_freq_preset,
        firmwareOptions.runtime_high_freq_count,
        firmwareOptions.runtime_high_freq_start,
        firmwareOptions.runtime_high_freq_stop,
        firmwareOptions.runtime_high_freq_label);
    crsfRouter.SetExtendedHeaderAndCrc(command, CRSF_FRAMETYPE_COMMAND, CRSF_EXT_FRAME_SIZE(offset), CRSF_ADDRESS_CRSF_RECEIVER, CRSF_ADDRESS_CRSF_TRANSMITTER);
    crsfRouter.deliverMessageTo(CRSF_ADDRESS_CRSF_RECEIVER, reinterpret_cast<crsf_header_t *>(frame));
}

static void applyRuntimeFreqPreset(uint8_t preset)
{
    if (preset >= ARRAY_SIZE(runtimeFreqPresets))
    {
        preset = 0;
    }

    firmwareOptions.runtime_freq_preset = preset;
    if (preset == 0)
    {
        FHSSdisableRuntimeFrequency();
        return;
    }

    const runtime_freq_preset_t &runtimePreset = runtimeFreqPresets[preset];
    FHSSsetRuntimeFrequency(
        runtimePreset.startHz,
        runtimePreset.stopHz,
        runtimePreset.channelCount,
        preset,
        runtimePreset.label);
}

static void setRuntimeFreqFromLuaFields()
{
    const uint32_t startHz = be32toh(luaRuntimeFreqStartMHz.properties.value) * 100000U;
    const uint32_t stopHz = be32toh(luaRuntimeFreqStopMHz.properties.value) * 100000U;
    firmwareOptions.runtime_freq_auto_count = luaRuntimeFreqAutoCount.value != 0;
    if (luaRuntimeFreqAutoCount.value != 0)
    {
        updateRuntimeFreqCountFromWidth();
    }
    const uint8_t channelCount = luaRuntimeFreqCount.properties.u.value;
    if (luaRuntimeFreqEnable.value == 0)
    {
        FHSSdisableRuntimeFrequency();
        return;
    }

    FHSSsetRuntimeFrequency(
        startHz,
        stopHz,
        channelCount,
        luaRuntimeFreqPreset.value,
        resolveRuntimeCustomLabel(
            luaRuntimeFreqPreset.value,
            RUNTIME_FREQ_PRESET_CUSTOM,
            runtimeFreqPresets,
            startHz,
            stopHz,
            'C',
            runtimeFreqCustomLabel));
}

static void applyRuntimeHighFreqPreset(uint8_t preset)
{
    if (preset >= ARRAY_SIZE(runtimeHighFreqPresets))
    {
        preset = 0;
    }

    firmwareOptions.runtime_high_freq_preset = preset;
    if (preset == 0)
    {
        FHSSdisableHighRuntimeFrequency();
        return;
    }

    const runtime_freq_preset_t &runtimePreset = runtimeHighFreqPresets[preset];
    FHSSsetHighRuntimeFrequency(
        runtimePreset.startHz,
        runtimePreset.stopHz,
        runtimePreset.channelCount,
        preset,
        runtimePreset.label);
}

static void setRuntimeHighFreqFromLuaFields()
{
    const uint32_t startHz = be32toh(luaRuntimeHighFreqStartMHz.properties.value) * 100000U;
    const uint32_t stopHz = be32toh(luaRuntimeHighFreqStopMHz.properties.value) * 100000U;
    firmwareOptions.runtime_high_freq_auto_count = luaRuntimeHighFreqAutoCount.value != 0;
    if (luaRuntimeHighFreqAutoCount.value != 0)
    {
        updateRuntimeHighFreqCountFromWidth();
    }
    const uint8_t channelCount = luaRuntimeHighFreqCount.properties.u.value;
    if (luaRuntimeHighFreqEnable.value == 0)
    {
        FHSSdisableHighRuntimeFrequency();
        return;
    }

    FHSSsetHighRuntimeFrequency(
        startHz,
        stopHz,
        channelCount,
        luaRuntimeHighFreqPreset.value,
        resolveRuntimeCustomLabel(
            luaRuntimeHighFreqPreset.value,
            RUNTIME_HIGH_FREQ_PRESET_CUSTOM,
            runtimeHighFreqPresets,
            startHz,
            stopHz,
            'H',
            runtimeHighFreqCustomLabel));
}

static void applyRuntimeFrequencyAndRebind()
{
    setRuntimeFreqFromLuaFields();
    setRuntimeHighFreqFromLuaFields();
    saveOptions();
    FHSSactivatePendingRuntimeFrequencies();
    FHSSrandomiseFHSSsequence(uidMacSeedGet());

    if (connectionState == connected)
    {
        sendRuntimeFreqConfigToRx();
        deferExecutionMillis(250, []() {
            FHSSrandomiseFHSSsequence(uidMacSeedGet());
            EnterBindingModeSafely();
        });
    }
    else
    {
        EnterBindingModeSafely();
    }
}
#endif


extern TxConfig config;
extern void VtxTriggerSend();
extern void ResetPower();
extern uint8_t adjustPacketRateForBaud(uint8_t rate);
extern uint8_t adjustSwitchModeForAirRate(OtaSwitchMode_e eSwitchMode, uint8_t packetSize);
extern bool RxWiFiReadyToSend;
extern bool BackpackTelemReadyToSend;
extern bool TxBackpackWiFiReadyToSend;
extern bool VRxBackpackWiFiReadyToSend;
extern unsigned long rebootTime;
extern void setWifiUpdateMode();
extern void EnterBindingModeSafely();

void TXModuleEndpoint::supressCriticalErrors()
{
    // clear the critical error bits of the warning flags
    luaWarningFlags &= 0b00011111;
}

/***
 * @brief: Update the luaBadGoodString with the current bad/good count
 * This item is hidden on our Lua and only displayed in other systems that don't poll our status
 ****/
void TXModuleEndpoint::devicePingCalled()
{
    supressCriticalErrors();
    utoa(CRSFHandset::BadPktsCountResult, luaBadGoodString, 10);
    strcat(luaBadGoodString, "/");
    utoa(CRSFHandset::GoodPktsCountResult, luaBadGoodString + strlen(luaBadGoodString), 10);
}

void TXModuleEndpoint::setWarningFlag(const warningFlags flag, const bool value)
{
  if (value)
  {
    luaWarningFlags |= 1 << (uint8_t)flag;
  }
  else
  {
    luaWarningFlags &= ~(1 << (uint8_t)flag);
  }
}

void TXModuleEndpoint::sendELRSstatus(const crsf_addr_e origin)
{
  constexpr const char *messages[] = { //higher order = higher priority
    "",                   //status2 = connected status
    "",                   //status1, reserved for future use
    "Model Mismatch",     //warning3, model mismatch
    "[ ! Armed ! ]",      //warning2, AUX1 high / armed
    "",           //warning1, reserved for future use
    "Not while connected",  //critical warning3, trying to change a protected value while connected
    "Baud rate too low",  //critical warning2, changing packet rate and baud rate too low
    ""   //critical warning1, reserved for future use
  };
  auto warningInfo = "";

  for (int i = 7; i >= 0; i--)
  {
      if (luaWarningFlags & (1 << i))
      {
          warningInfo = messages[i];
          break;
      }
  }
  const uint8_t payloadSize = sizeof(elrsStatusParameter) + strlen(warningInfo) + 1;
  uint8_t buffer[sizeof(crsf_ext_header_t) + payloadSize + 1];
  const auto params = (elrsStatusParameter *)&buffer[sizeof(crsf_ext_header_t)];

  setWarningFlag(LUA_FLAG_MODEL_MATCH, connectionState == connected && connectionHasModelMatch == false);
  setWarningFlag(LUA_FLAG_CONNECTED, connectionState == connected);
  setWarningFlag(LUA_FLAG_ISARMED, handset->IsArmed());

  params->pktsBad = CRSFHandset::BadPktsCountResult;
  params->pktsGood = htobe16(CRSFHandset::GoodPktsCountResult);
  params->flags = luaWarningFlags;
  // to support sending a params.msg, buffer should be extended by the strlen of the message
  // and copied into params->msg (with trailing null)
  strcpy(params->msg, warningInfo);
  crsfRouter.SetExtendedHeaderAndCrc((crsf_ext_header_t *)buffer, CRSF_FRAMETYPE_ELRS_STATUS, CRSF_EXT_FRAME_SIZE(payloadSize), origin, CRSF_ADDRESS_CRSF_TRANSMITTER);
  crsfRouter.processMessage(nullptr, (crsf_header_t *)buffer);
}

void TXModuleEndpoint::updateModelID() {
  itoa(modelId, modelMatchUnit+6, 10);
  strcat(modelMatchUnit, ")");
}

void TXModuleEndpoint::updateTlmBandwidth()
{
  const auto eRatio = (expresslrs_tlm_ratio_e)config.GetTlm();
  // TLM_RATIO_STD / TLM_RATIO_DISARMED
  if (eRatio == TLM_RATIO_STD || eRatio == TLM_RATIO_DISARMED)
  {
    // For Standard ratio, display the ratio instead of bps
    strcpy(tlmBandwidth, " (1:");
    const uint8_t ratioDiv = TLMratioEnumToValue(ExpressLRS_currAirRate_Modparams->TLMinterval);
    itoa(ratioDiv, &tlmBandwidth[4], 10);
    strcat(tlmBandwidth, ")");
  }

  // TLM_RATIO_NO_TLM
  else if (eRatio == TLM_RATIO_NO_TLM)
  {
    tlmBandwidth[0] = '\0';
  }

  // All normal ratios
  else
  {
    tlmBandwidth[0] = ' ';

    const uint16_t hz = 1000000 / ExpressLRS_currAirRate_Modparams->interval;
    const uint8_t ratiodiv = TLMratioEnumToValue(eRatio);
    const uint8_t burst = TLMBurstMaxForRateRatio(hz, ratiodiv);
    const uint8_t bytesPerCall = OtaIsFullRes ? ELRS8_DATA_DL_BYTES_PER_CALL : ELRS4_DATA_DL_BYTES_PER_CALL;
    uint32_t bandwidthValue = bytesPerCall * 8U * burst * hz / ratiodiv / (burst + 1);
    if (OtaIsFullRes)
    {
      // Due to fullres also packing telemetry into the LinkStats packet, there is at least
      // N bytes more data for every rate except 100Hz 1:128, and 2*N bytes more for many
      // rates. The calculation is a more complex though, so just approximate some of the
      // extra bandwidth
      bandwidthValue += 8U * (ELRS8_DATA_DL_BYTES_PER_CALL - sizeof(OTA_LinkStats_s));
    }

    utoa(bandwidthValue, &tlmBandwidth[2], 10);
    strcat(tlmBandwidth, "bps)");
  }
}

void TXModuleEndpoint::updateBackpackOpts()
{
  if (config.GetBackpackDisable())
  {
    // If backpack is disabled, set all the Backpack select options to "Disabled"
    LUA_FIELD_HIDE(luaDvrAux);
    LUA_FIELD_HIDE(luaDvrStartDelay);
    LUA_FIELD_HIDE(luaDvrStopDelay);
    LUA_FIELD_HIDE(luaHeadTrackingEnableChannel);
    LUA_FIELD_HIDE(luaHeadTrackingStartChannel);
    LUA_FIELD_HIDE(luaBackpackTelemetry);
    LUA_FIELD_HIDE(luaBackpackVersion);
  }
  else
  {
    LUA_FIELD_SHOW(luaDvrAux);
    LUA_FIELD_SHOW(luaDvrStartDelay);
    LUA_FIELD_SHOW(luaDvrStopDelay);
    LUA_FIELD_SHOW(luaHeadTrackingEnableChannel);
    LUA_FIELD_SHOW(luaHeadTrackingStartChannel);
    LUA_FIELD_SHOW(luaBackpackTelemetry);
    LUA_FIELD_SHOW(luaBackpackVersion);
  }
}

static void setBleJoystickMode()
{
  setConnectionState(bleJoystick);
}

void TXModuleEndpoint::handleWifiBle(propertiesCommon *item, uint8_t arg)
{
  commandParameter *cmd = (commandParameter *)item;
  void (*setTargetState)();
  connectionState_e targetState;
  const char *textConfirm;
  const char *textRunning;
  if ((void *)item == (void *)&luaWebUpdate)
  {
    setTargetState = &setWifiUpdateMode;
    textConfirm = "Enter WiFi Update?";
    textRunning = "WiFi Running...";
    targetState = wifiUpdate;
  }
  else
  {
    setTargetState = &setBleJoystickMode;
    textConfirm = "Start BLE Joystick?";
    textRunning = "Joystick Running...";
    targetState = bleJoystick;
  }

  switch ((commandStep_e)arg)
  {
    case lcsClick:
      if (connectionState == connected)
      {
        sendCommandResponse(cmd, lcsAskConfirm, textConfirm);
        return;
      }
      // fallthrough (clicking while not connected goes right to exectute)

    case lcsConfirmed:
      sendCommandResponse(cmd, lcsExecuting, textRunning);
      setTargetState();
      break;

    case lcsCancel:
      sendCommandResponse(cmd, lcsIdle, STR_EMPTYSPACE);
      if (connectionState == targetState)
      {
        rebootTime = millis() + 400;
      }
      break;

    default: // LUACMDSTEP_NONE on load, LUACMDSTEP_EXECUTING (our lua) or LUACMDSTEP_QUERY (Crossfire Config)
      sendCommandResponse(cmd, cmd->step, cmd->info);
      break;
  }
}

void TXModuleEndpoint::handleSimpleSendCmd(propertiesCommon *item, uint8_t arg)
{
  const char *msg = "Sending...";
  static uint32_t lastLcsPoll;
  if (arg < lcsCancel)
  {
    lastLcsPoll = millis();
    if ((void *)item == (void *)&luaBind)
    {
      msg = "Binding...";
      EnterBindingModeSafely();
    }
    else if ((void *)item == (void *)&luaVtxSend)
    {
      VtxTriggerSend();
    }
    else if ((void *)item == (void *)&luaRxWebUpdate)
    {
      RxWiFiReadyToSend = true;
    }
    else if ((void *)item == (void *)&luaTxBackpackUpdate && OPT_USE_TX_BACKPACK)
    {
      TxBackpackWiFiReadyToSend = true;
    }
    else if ((void *)item == (void *)&luaVRxBackpackUpdate && OPT_USE_TX_BACKPACK)
    {
      VRxBackpackWiFiReadyToSend = true;
    }
    sendCommandResponse((commandParameter *)item, lcsExecuting, msg);
  } /* if doExecute */
  else if(arg == lcsCancel || ((millis() - lastLcsPoll)> 2000))
  {
    sendCommandResponse((commandParameter *)item, lcsIdle, STR_EMPTYSPACE);
  }
}

static void updateFolderName_TxPower()
{
  const uint8_t txPwrDyn = config.GetDynamicPower() ? config.GetBoostChannel() + 1 : 0;
  uint8_t pwrFolderLabelOffset = 10; // start writing after "TX Power ("

  // Power Level
  pwrFolderLabelOffset += findSelectionLabel(&luaPower, &pwrFolderDynamicName[pwrFolderLabelOffset], config.GetPower() - MinPower);

  // Dynamic Power
  if (txPwrDyn)
  {
    pwrFolderDynamicName[pwrFolderLabelOffset++] = folderNameSeparator[0];
    pwrFolderLabelOffset += findSelectionLabel(&luaDynamicPower, &pwrFolderDynamicName[pwrFolderLabelOffset], txPwrDyn);
  }

  pwrFolderDynamicName[pwrFolderLabelOffset++] = ')';
  pwrFolderDynamicName[pwrFolderLabelOffset] = '\0';
}

static void updateFolderName_VtxAdmin()
{
  const uint8_t vtxBand = config.GetVtxBand();
  if (vtxBand)
  {
    luaVtxFolder.dyn_name = vtxFolderDynamicName;
    uint8_t vtxFolderLabelOffset = 11; // start writing after "VTX Admin ("

    // Band
    vtxFolderLabelOffset += findSelectionLabel(&luaVtxBand, &vtxFolderDynamicName[vtxFolderLabelOffset], vtxBand);
    vtxFolderDynamicName[vtxFolderLabelOffset++] = folderNameSeparator[1];

    // Channel
    vtxFolderDynamicName[vtxFolderLabelOffset++] = '1' + config.GetVtxChannel();

    // VTX Power
    const uint8_t vtxPwr = config.GetVtxPower();
    //if power is no-change (-), don't show, also hide pitmode
    if (vtxPwr)
    {
      vtxFolderDynamicName[vtxFolderLabelOffset++] = folderNameSeparator[1];
      vtxFolderLabelOffset += findSelectionLabel(&luaVtxPwr, &vtxFolderDynamicName[vtxFolderLabelOffset], vtxPwr);

      // Pit Mode
      const uint8_t vtxPit = config.GetVtxPitmode();
      //if pitmode is off, don't show
      //show pitmode AuxSwitch or show P if not OFF
      if (vtxPit != 0)
      {
        if (vtxPit != 1)
        {
          vtxFolderDynamicName[vtxFolderLabelOffset++] = folderNameSeparator[1];
          vtxFolderLabelOffset += findSelectionLabel(&luaVtxPit, &vtxFolderDynamicName[vtxFolderLabelOffset], vtxPit);
        }
        else
        {
          vtxFolderDynamicName[vtxFolderLabelOffset++] = folderNameSeparator[1];
          vtxFolderDynamicName[vtxFolderLabelOffset++] = 'P';
        }
      }
    }
    vtxFolderDynamicName[vtxFolderLabelOffset++] = ')';
    vtxFolderDynamicName[vtxFolderLabelOffset] = '\0';
  }
  else
  {
    //don't show vtx settings if band is OFF
    luaVtxFolder.dyn_name = NULL;
  }
}

void TXModuleEndpoint::SetPacketRateIdx(uint8_t idx, bool forceChange)
{
  if (idx >= RATE_MAX)
    return;

  uint8_t actualRate = adjustPacketRateForBaud(idx);
  // No change, don't do anything
  if (actualRate == ExpressLRS_currAirRate_Modparams->index)
    return;

  const auto newModParams = get_elrs_airRateConfig(actualRate);
  uint8_t newSwitchMode = adjustSwitchModeForAirRate((OtaSwitchMode_e)config.GetSwitchMode(), newModParams->PayloadLength);
  // Force Gemini when using dual band modes.
  uint8_t newAntennaMode = (newModParams->radio_type == RADIO_TYPE_LR1121_LORA_DUAL) ? TX_RADIO_MODE_GEMINI : config.GetAntennaMode();
  // If the switch mode is going to change, block the change while connected
  bool isDisconnected = connectionState == disconnected;
  // Don't allow the switch mode to change if the TX is in mavlink mode
  // Wide switch mode is not compatible with mavlink, and the switch mode is
  // autoconfigured when entering mavlink mode
  bool isMavlinkMode = config.GetLinkMode() == TX_MAVLINK_MODE;
  if (forceChange || (newSwitchMode == OtaSwitchModeCurrent) || (isDisconnected && !isMavlinkMode))
  {
    // This must be deferred because this can be called from any thread.
    // Deferring it forces it to run in the main loop, which otherwise
    // would cause a race condition with the syncspam needing to get out
    // before the rate change
    deferExecutionMillis(10, [actualRate, newSwitchMode, newAntennaMode]() {
      config.SetRate(actualRate);
      config.SetSwitchMode(newSwitchMode);
      config.SetAntennaMode(newAntennaMode);
      SetSyncSpam();
    });
    setWarningFlag(LUA_FLAG_ERROR_BAUDRATE, actualRate != idx);
    // No need to set OtaSerializers, the rate is changing so all of that will be reconfigured
  } else {
    setWarningFlag(LUA_FLAG_ERROR_CONNECTED, true);
  }
}

void TXModuleEndpoint::SetSwitchMode(uint8_t idx)
{
  // Only allow changing switch mode when disconnected since we need to guarantee
  // the pack and unpack functions are matched
  bool isDisconnected = connectionState == disconnected;
  // Don't allow the switch mode to change if the TX is in mavlink mode
  // Wide switchmode is not compatible with mavlink, and the switchmode is
  // auto-configured when entering mavlink mode
  bool isMavlinkMode = config.GetLinkMode() == TX_MAVLINK_MODE;
  if (isDisconnected && !isMavlinkMode)
  {
    config.SetSwitchMode(idx);
    OtaUpdateSerializers((OtaSwitchMode_e)idx, ExpressLRS_currAirRate_Modparams->PayloadLength);
  }
  else if (!isMavlinkMode) // No need to display warning as no switch change can be made while in Mavlink mode.
  {
    setWarningFlag(LUA_FLAG_ERROR_CONNECTED, true);
  }
}

void TXModuleEndpoint::SetAntennaMode(uint8_t idx)
{
  // Force Gemini when using dual band modes.
  uint8_t newAntennaMode = get_elrs_airRateConfig(config.GetRate())->radio_type == RADIO_TYPE_LR1121_LORA_DUAL ? TX_RADIO_MODE_GEMINI : idx;
  config.SetAntennaMode(newAntennaMode);
}

void TXModuleEndpoint::SetTlmRatio(uint8_t idx)
{
  const auto eRatio = (expresslrs_tlm_ratio_e)idx;
  if (eRatio <= TLM_RATIO_DISARMED)
  {
    const bool isMavlinkMode = config.GetLinkMode() == TX_MAVLINK_MODE;
    // Don't allow TLM ratio changes if using AIRPORT or Mavlink
    if (!firmwareOptions.is_airport && !isMavlinkMode)
    {
      config.SetTlm(eRatio);
      // Update the telemetry ratio immediately, rather than wait the agonizing 5 seconds for the next sync
      SetSyncSpam();
    }
  }
}

void TXModuleEndpoint::SetPowerMax(uint8_t idx)
{
  config.SetPower(idx);
  if (!config.IsModified())
  {
      ResetPower();
  }
}

void TXModuleEndpoint::SetDynamicPower(uint8_t idx)
{
  config.SetDynamicPower(idx > 0);
  config.SetBoostChannel((idx - 1) > 0 ? idx - 1 : 0);
}

/***
 * @brief: Update the dynamic strings used for folder names and labels
 ***/
void TXModuleEndpoint::updateFolderNames()
{
  updateFolderName_TxPower();
  updateFolderName_VtxAdmin();

  // These aren't folder names, just string labels slapped in the units field generally
  updateTlmBandwidth();
  updateBackpackOpts();
}

static void recalculatePacketRateOptions(int minInterval)
{
    const char *allRates = STR_LUA_PACKETRATES;
    const char *pos = allRates;
    luastrPacketRates[0] = 0;
    for (int i=0 ; i < RATE_MAX ; i++)
    {
        uint8_t rate = i;
        rate = RATE_MAX - 1 - rate;
        bool rateAllowed = (get_elrs_airRateConfig(rate)->interval * get_elrs_airRateConfig(rate)->numOfSends) >= minInterval;

#if defined(RADIO_LR1121)
        // Skip unsupported modes for hardware with only a single LR1121 or with a single RF path
        rateAllowed &= isSupportedRFRate(rate);
        if (rateAllowed)
        {
            const auto radio_type = get_elrs_airRateConfig(rate)->radio_type;
            // Always show 900MHz rates (including 25Hz) regardless of RF mode
            // For other modes, filter based on current RF mode selection
            bool is900Rate = (radio_type == RADIO_TYPE_LR1121_GFSK_900 || radio_type == RADIO_TYPE_LR1121_LORA_900);
            if (rfMode == RF_MODE_900)
            {
                rateAllowed = is900Rate;
            }
            if (rfMode == RF_MODE_2G4)
            {
                // Show 2.4GHz rates AND 900MHz rates (so 25Hz is always available)
                rateAllowed = (radio_type == RADIO_TYPE_LR1121_GFSK_2G4 || radio_type == RADIO_TYPE_LR1121_LORA_2G4) || is900Rate;
            }
            if (rfMode == RF_MODE_DUAL)
            {
                // Show dual-band rates AND 900MHz rates (so 25Hz is always available)
                rateAllowed = (radio_type == RADIO_TYPE_LR1121_LORA_DUAL) || is900Rate;
            }
        }
#endif
        const char *semi = strchrnul(pos, ';');
        if (rateAllowed)
        {
            strncat(luastrPacketRates, pos, semi - pos);
        }
        pos = semi;
        if (*semi == ';')
        {
            strcat(luastrPacketRates, ";");
            pos = semi+1;
        }
    }

    // trim off trailing semicolons (assumes luastrPacketRates has at least 1 non-semicolon)
    for (auto lastPos = strlen(luastrPacketRates)-1; luastrPacketRates[lastPos] == ';'; lastPos--)
    {
        luastrPacketRates[lastPos] = '\0';
    }
}

void TXModuleEndpoint::registerParameters()
{
  setStringValue(&luaInfo, luaBadGoodString);

  auto wifiBleCallback = [&](propertiesCommon *item, const uint8_t arg) { handleWifiBle(item, arg); };
  auto sendCallback = [&](propertiesCommon *item, const uint8_t arg) { handleSimpleSendCmd(item, arg); };

  if (HAS_RADIO) {
#if defined(RADIO_LR1121)
    // Only allow selection of the band if both bands have power values defined
    if (POWER_OUTPUT_VALUES_COUNT != 0 && POWER_OUTPUT_VALUES_DUAL_COUNT != 0)
    {
      // Copy the frequency part out of the domain to the display string
      char *bands = luastrRFBands;
      for (const char *domain = FHSSconfig->domain; *domain ; domain++)
      {
        if (isdigit(*domain))
        {
          *bands++ = *domain;
        }
      }
      *bands = '\0';
      strlcat(luastrRFBands, "MHz;2.4GHz", sizeof(luastrRFBands));
      // Only double LR1121 supports Dual Band modes
      if (GPIO_PIN_NSS_2 != UNDEF_PIN)
      {
        strlcat(luastrRFBands, ";X-Band", sizeof(luastrRFBands));
      }

      registerParameter(&luaRFBand, [this](propertiesCommon *item, uint8_t arg) {
        if (arg != rfMode)
        {
          // Choose the fastest supported packet rate in this RF band.
          rfMode = static_cast<RFMode>(arg);
          for (int i=0; i < RATE_MAX ; i++)
          {
            if (isSupportedRFRate(i))
            {
              const auto radio_type = get_elrs_airRateConfig(i)->radio_type;
              if (rfMode == RF_MODE_900 && (radio_type == RADIO_TYPE_LR1121_GFSK_900 || radio_type == RADIO_TYPE_LR1121_LORA_900))
              {
                SetPacketRateIdx(i, true);
                break;
              }
              if (rfMode == RF_MODE_2G4 && (radio_type == RADIO_TYPE_LR1121_GFSK_2G4 || radio_type == RADIO_TYPE_LR1121_LORA_2G4))
              {
                SetPacketRateIdx(i, true);
                break;
              }
              if (rfMode == RF_MODE_DUAL && radio_type == RADIO_TYPE_LR1121_LORA_DUAL)
              {
                SetPacketRateIdx(i, true);
                break;
              }
            }
          }
          recalculatePacketRateOptions(handset->getMinPacketInterval());
        }
      });
    }
#endif
    registerParameter(&luaAirRate, [this](propertiesCommon *item, uint8_t arg) {
      uint8_t selectedRate = RATE_MAX - 1 - arg;
      SetPacketRateIdx(selectedRate, true);
    });
    registerParameter(&luaTlmRate, [this](propertiesCommon *item, uint8_t arg) {
      SetTlmRatio(arg);
    });
    if (!firmwareOptions.is_airport)
    {
      registerParameter(&luaSwitch, [this](propertiesCommon *item, uint8_t arg) {
        SetSwitchMode(arg);
      });
    }
    if (isDualRadio())
    {
      registerParameter(&luaAntenna, [this](propertiesCommon *item, uint8_t arg) {
        SetAntennaMode(arg);
      });
    }
    registerParameter(&luaLinkMode, [this](propertiesCommon *item, uint8_t arg) {
      // Only allow changing when disconnected since we need to guarantee
      // the switch pack and unpack functions are matched on the tx and rx.
      bool isDisconnected = connectionState == disconnected;
      if (isDisconnected)
      {
        config.SetLinkMode(arg);
      }
      else
      {
        setWarningFlag(LUA_FLAG_ERROR_CONNECTED, true);
      }
    });
    if (!firmwareOptions.is_airport)
    {
      registerParameter(&luaModelMatch, [this](propertiesCommon *item, uint8_t arg) {
        bool newModelMatch = arg;
        config.SetModelMatch(newModelMatch);
        if (connectionState == connected)
        {
          mspPacket_t msp;
          msp.reset();
          msp.makeCommand();
          msp.function = MSP_SET_RX_CONFIG;
          msp.addByte(MSP_ELRS_MODEL_ID);
          msp.addByte(newModelMatch ? modelId : 0xff);
          crsfRouter.AddMspMessage(&msp, CRSF_ADDRESS_CRSF_RECEIVER, CRSF_ADDRESS_CRSF_TRANSMITTER);
        }
        updateModelID();
      });
    }

    // POWER folder
    registerParameter(&luaPowerFolder);
    filterOptions(&luaPower, POWERMGNT::getMinPower(), POWERMGNT::getMaxPower(), strPowerLevels);
    registerParameter(&luaPower, [this](propertiesCommon *item, uint8_t arg) {
      SetPowerMax(constrain(arg + POWERMGNT::getMinPower(), POWERMGNT::getMinPower(), POWERMGNT::getMaxPower()));
    }, luaPowerFolder.common.id);
    registerParameter(&luaDynamicPower, [this](propertiesCommon *item, uint8_t arg) {
      SetDynamicPower(arg);
    }, luaPowerFolder.common.id);
  }
  if (GPIO_PIN_FAN_EN != UNDEF_PIN || GPIO_PIN_FAN_PWM != UNDEF_PIN) {
    registerParameter(&luaFanThreshold, [](propertiesCommon *item, uint8_t arg){
      config.SetPowerFanThreshold(arg);
    }, luaPowerFolder.common.id);
  }
#if defined(Regulatory_Domain_EU_CE_2400)
  if (HAS_RADIO) {
    registerParameter(&luaCELimit, NULL, luaPowerFolder.common.id);
  }
#endif
  if ((HAS_RADIO || OPT_USE_TX_BACKPACK) && !firmwareOptions.is_airport) {
    // VTX folder
    registerParameter(&luaVtxFolder);
    registerParameter(&luaVtxBand, [](propertiesCommon *item, uint8_t arg) {
      config.SetVtxBand(arg);
    }, luaVtxFolder.common.id);
    registerParameter(&luaVtxChannel, [](propertiesCommon *item, uint8_t arg) {
      config.SetVtxChannel(arg - 1);
    }, luaVtxFolder.common.id);
    registerParameter(&luaVtxPwr, [](propertiesCommon *item, uint8_t arg) {
      config.SetVtxPower(arg);
    }, luaVtxFolder.common.id);
    registerParameter(&luaVtxPit, [](propertiesCommon *item, uint8_t arg) {
      config.SetVtxPitmode(arg);
    }, luaVtxFolder.common.id);
    registerParameter(&luaVtxSend, sendCallback, luaVtxFolder.common.id);
  }

  // WIFI folder
  registerParameter(&luaWiFiFolder);
  registerParameter(&luaWebUpdate, wifiBleCallback, luaWiFiFolder.common.id);
  if (HAS_RADIO) {
    registerParameter(&luaRxWebUpdate, sendCallback, luaWiFiFolder.common.id);

    if (OPT_USE_TX_BACKPACK) {
      registerParameter(&luaTxBackpackUpdate, sendCallback, luaWiFiFolder.common.id);
      registerParameter(&luaVRxBackpackUpdate, sendCallback, luaWiFiFolder.common.id);
      // Backpack folder
      registerParameter(&luaBackpackFolder);
      if (GPIO_PIN_BACKPACK_EN != UNDEF_PIN)
      {
        registerParameter(
            &luaBackpackEnable, [](propertiesCommon *item, uint8_t arg) {
                // option is Off/On (enable) and config storage is On/Off (disable)
                config.SetBackpackDisable(arg == 0);
            }, luaBackpackFolder.common.id);
      }
      registerParameter(
          &luaDvrAux, [](propertiesCommon *item, uint8_t arg) {
              if (config.GetBackpackDisable() == false)
                config.SetDvrAux(arg);
          },
          luaBackpackFolder.common.id);
      registerParameter(
          &luaDvrStartDelay, [](propertiesCommon *item, uint8_t arg) {
              if (config.GetBackpackDisable() == false)
                config.SetDvrStartDelay(arg);
          },
          luaBackpackFolder.common.id);
      registerParameter(
          &luaDvrStopDelay, [](propertiesCommon *item, uint8_t arg) {
            if (config.GetBackpackDisable() == false)
              config.SetDvrStopDelay(arg);
          },
          luaBackpackFolder.common.id);
      registerParameter(
          &luaHeadTrackingEnableChannel, [](propertiesCommon *item, uint8_t arg) {
              config.SetPTREnableChannel(arg);
          },
          luaBackpackFolder.common.id);
      registerParameter(
          &luaHeadTrackingStartChannel, [](propertiesCommon *item, uint8_t arg) {
              config.SetPTRStartChannel(arg);
          },
          luaBackpackFolder.common.id);
      registerParameter(
            &luaBackpackTelemetry, [](propertiesCommon *item, uint8_t arg) {
                config.SetBackpackTlmMode(arg);
                BackpackTelemReadyToSend = true;
            }, luaBackpackFolder.common.id);

      registerParameter(&luaBackpackVersion, nullptr, luaBackpackFolder.common.id);
    }
  }

  #if defined(PLATFORM_ESP32)
  registerParameter(&luaBLEJoystick, wifiBleCallback);
  #endif

#if defined(RADIO_LR1121)
  if (HAS_RADIO)
  {
    registerParameter(&luaRuntimeFreqFolder);
    registerParameter(&luaRuntimeFreqPreset, [](propertiesCommon *item, uint8_t arg) {
      (void)item;
      if (arg < ARRAY_SIZE(runtimeFreqPresets))
      {
        luaRuntimeFreqPreset.value = arg;
        if (arg < RUNTIME_FREQ_PRESET_CUSTOM && arg != 0)
        {
          applyRuntimeFreqPreset(arg);
          setFloatValue(&luaRuntimeFreqStartMHz, runtimeFreqPresets[arg].startHz / 100000U);
          setFloatValue(&luaRuntimeFreqStopMHz, runtimeFreqPresets[arg].stopHz / 100000U);
          luaRuntimeFreqCount.properties.u.value = runtimeFreqPresets[arg].channelCount;
          luaRuntimeFreqEnable.value = 1;
          luaRuntimeFreqAutoCount.value = 1;
          firmwareOptions.runtime_freq_auto_count = true;
        }
        if (arg == 0)
        {
          applyRuntimeFreqPreset(arg);
          luaRuntimeFreqEnable.value = 0;
        }
      }
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeFreqEnable, [](propertiesCommon *item, uint8_t arg) {
      (void)item;
      luaRuntimeFreqEnable.value = arg;
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeFreqAutoCount, [](propertiesCommon *item, uint8_t arg) {
      (void)item;
      luaRuntimeFreqAutoCount.value = arg;
      firmwareOptions.runtime_freq_auto_count = arg != 0;
      if (arg != 0)
      {
        updateRuntimeFreqCountFromWidth();
        luaRuntimeFreqEnable.value = 1;
      }
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeFreqStartMHz, [](propertiesCommon *item, uint8_t arg) {
      (void)item;
      (void)arg;
      luaRuntimeFreqPreset.value = RUNTIME_FREQ_PRESET_CUSTOM;
      luaRuntimeFreqEnable.value = 1;
      if (luaRuntimeFreqAutoCount.value != 0)
      {
        updateRuntimeFreqCountFromWidth();
      }
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeFreqStopMHz, [](propertiesCommon *item, uint8_t arg) {
      (void)item;
      (void)arg;
      luaRuntimeFreqPreset.value = RUNTIME_FREQ_PRESET_CUSTOM;
      luaRuntimeFreqEnable.value = 1;
      if (luaRuntimeFreqAutoCount.value != 0)
      {
        updateRuntimeFreqCountFromWidth();
      }
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeFreqCount, [](propertiesCommon *item, uint8_t arg) {
      (void)item;
      (void)arg;
      luaRuntimeFreqPreset.value = RUNTIME_FREQ_PRESET_CUSTOM;
      luaRuntimeFreqEnable.value = 1;
      luaRuntimeFreqAutoCount.value = 0;
      firmwareOptions.runtime_freq_auto_count = false;
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeHighFreqPreset, [](propertiesCommon *item, uint8_t arg) {
      (void)item;
      if (arg < ARRAY_SIZE(runtimeHighFreqPresets))
      {
        luaRuntimeHighFreqPreset.value = arg;
        if (arg < RUNTIME_HIGH_FREQ_PRESET_CUSTOM && arg != 0)
        {
          applyRuntimeHighFreqPreset(arg);
          setFloatValue(&luaRuntimeHighFreqStartMHz, runtimeHighFreqPresets[arg].startHz / 100000U);
          setFloatValue(&luaRuntimeHighFreqStopMHz, runtimeHighFreqPresets[arg].stopHz / 100000U);
          luaRuntimeHighFreqCount.properties.u.value = runtimeHighFreqPresets[arg].channelCount;
          luaRuntimeHighFreqEnable.value = 1;
          luaRuntimeHighFreqAutoCount.value = 1;
          firmwareOptions.runtime_high_freq_auto_count = true;
        }
        if (arg == 0)
        {
          applyRuntimeHighFreqPreset(arg);
          luaRuntimeHighFreqEnable.value = 0;
        }
      }
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeHighFreqEnable, [](propertiesCommon *item, uint8_t arg) {
      (void)item;
      luaRuntimeHighFreqEnable.value = arg;
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeHighFreqAutoCount, [](propertiesCommon *item, uint8_t arg) {
      (void)item;
      luaRuntimeHighFreqAutoCount.value = arg;
      firmwareOptions.runtime_high_freq_auto_count = arg != 0;
      if (arg != 0)
      {
        updateRuntimeHighFreqCountFromWidth();
        luaRuntimeHighFreqEnable.value = 1;
      }
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeHighFreqStartMHz, [](propertiesCommon *item, uint8_t arg) {
      (void)item;
      (void)arg;
      luaRuntimeHighFreqPreset.value = RUNTIME_HIGH_FREQ_PRESET_CUSTOM;
      luaRuntimeHighFreqEnable.value = 1;
      if (luaRuntimeHighFreqAutoCount.value != 0)
      {
        updateRuntimeHighFreqCountFromWidth();
      }
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeHighFreqStopMHz, [](propertiesCommon *item, uint8_t arg) {
      (void)item;
      (void)arg;
      luaRuntimeHighFreqPreset.value = RUNTIME_HIGH_FREQ_PRESET_CUSTOM;
      luaRuntimeHighFreqEnable.value = 1;
      if (luaRuntimeHighFreqAutoCount.value != 0)
      {
        updateRuntimeHighFreqCountFromWidth();
      }
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeHighFreqCount, [](propertiesCommon *item, uint8_t arg) {
      (void)item;
      (void)arg;
      luaRuntimeHighFreqPreset.value = RUNTIME_HIGH_FREQ_PRESET_CUSTOM;
      luaRuntimeHighFreqEnable.value = 1;
      luaRuntimeHighFreqAutoCount.value = 0;
      firmwareOptions.runtime_high_freq_auto_count = false;
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeFreqApply, [this](propertiesCommon *item, uint8_t arg) {
      commandParameter *cmd = (commandParameter *)item;
      switch ((commandStep_e)arg)
      {
        case lcsClick:
          if (luaRuntimeFreqEnable.value != 0)
          {
            setRuntimeFreqFromLuaFields();
            if (!FHSSruntimeFreqValid())
            {
              this->sendCommandResponse(cmd, lcsIdle, "Invalid sub-GHz range");
              return;
            }
          }
          if (luaRuntimeHighFreqEnable.value != 0)
          {
            setRuntimeHighFreqFromLuaFields();
            if (!FHSShighRuntimeFreqValid())
            {
              this->sendCommandResponse(cmd, lcsIdle, "Invalid high-band range");
              return;
            }
          }
          if (connectionState == connected)
          {
            this->sendCommandResponse(cmd, lcsAskConfirm, "Push to RX and rebind?");
          }
          else
          {
            applyRuntimeFrequencyAndRebind();
            this->sendCommandResponse(cmd, lcsExecuting, "Applying...");
          }
          break;
        case lcsConfirmed:
          applyRuntimeFrequencyAndRebind();
          this->sendCommandResponse(cmd, lcsExecuting, "Applying...");
          break;
        case lcsCancel:
          this->sendCommandResponse(cmd, lcsIdle, STR_EMPTYSPACE);
          break;
        default:
          this->sendCommandResponse(cmd, cmd->step, cmd->info);
          break;
      }
    }, luaRuntimeFreqFolder.common.id);
    registerParameter(&luaRuntimeFreqInfo, nullptr, luaRuntimeFreqFolder.common.id);
  }
#endif

  if (HAS_RADIO) {
    registerParameter(&luaBind, sendCallback);
  }

  registerParameter(&luaInfo);
  registerParameter(&luaELRSversion);
}

void TXModuleEndpoint::updateParameters()
{
  static bool runtimeFreqAutoApplyInProgress = false;
  bool isMavlinkMode = config.GetLinkMode() == TX_MAVLINK_MODE;
  uint8_t currentRate = adjustPacketRateForBaud(config.GetRate());
#if defined(RADIO_LR1121)
  // calculate RFMode from current packet-rate
  switch (get_elrs_airRateConfig(currentRate)->radio_type)
  {
    case RADIO_TYPE_LR1121_LORA_900:
    case RADIO_TYPE_LR1121_GFSK_900:
      rfMode = RF_MODE_900;
      break;
    case RADIO_TYPE_LR1121_LORA_DUAL:
      rfMode = RF_MODE_DUAL;
      break;
    default:
      rfMode = RF_MODE_2G4;
      break;
  }
  setTextSelectionValue(&luaRFBand, rfMode);
#endif
  recalculatePacketRateOptions(handset->getMinPacketInterval());
  setTextSelectionValue(&luaAirRate, RATE_MAX - 1 - currentRate);

  setTextSelectionValue(&luaTlmRate, config.GetTlm());
  luaTlmRate.options = isMavlinkMode ? tlmRatiosMav : tlmRatios;

  luaAntenna.options = get_elrs_airRateConfig(config.GetRate())->radio_type == RADIO_TYPE_LR1121_LORA_DUAL ? antennamodeOptsDualBand : antennamodeOpts;

  setTextSelectionValue(&luaSwitch, config.GetSwitchMode());
  if (isMavlinkMode)
  {
    luaSwitch.options = OtaIsFullRes ? switchmodeOpts8chMav : switchmodeOpts4chMav;
  }
  else
  {
    luaSwitch.options = OtaIsFullRes ? switchmodeOpts8ch : switchmodeOpts4ch;
  }

  if (isDualRadio())
  {
    setTextSelectionValue(&luaAntenna, config.GetAntennaMode());
  }
  setTextSelectionValue(&luaLinkMode, config.GetLinkMode());
  updateModelID();
  setTextSelectionValue(&luaModelMatch, (uint8_t)config.GetModelMatch());
  setTextSelectionValue(&luaPower, config.GetPower() - MinPower);
  if (GPIO_PIN_FAN_EN != UNDEF_PIN || GPIO_PIN_FAN_PWM != UNDEF_PIN)
  {
    setTextSelectionValue(&luaFanThreshold, config.GetPowerFanThreshold());
  }

  uint8_t dynamic = config.GetDynamicPower() ? config.GetBoostChannel() + 1 : 0;
  setTextSelectionValue(&luaDynamicPower, dynamic);

  setTextSelectionValue(&luaVtxBand, config.GetVtxBand());
  setUint8Value(&luaVtxChannel, config.GetVtxChannel() + 1);
  setTextSelectionValue(&luaVtxPwr, config.GetVtxPower());
  // Pit mode can only be sent as part of the power byte
  LUA_FIELD_VISIBLE(luaVtxPit, config.GetVtxPower() != 0);
  setTextSelectionValue(&luaVtxPit, config.GetVtxPitmode());
  if (OPT_USE_TX_BACKPACK)
  {
    setTextSelectionValue(&luaBackpackEnable, config.GetBackpackDisable() ? 0 : 1);
    setTextSelectionValue(&luaDvrAux, config.GetBackpackDisable() ? 0 : config.GetDvrAux());
    setTextSelectionValue(&luaDvrStartDelay, config.GetBackpackDisable() ? 0 : config.GetDvrStartDelay());
    setTextSelectionValue(&luaDvrStopDelay, config.GetBackpackDisable() ? 0 : config.GetDvrStopDelay());
    setTextSelectionValue(&luaHeadTrackingEnableChannel, config.GetBackpackDisable() ? 0 : config.GetPTREnableChannel());
    setTextSelectionValue(&luaHeadTrackingStartChannel, config.GetBackpackDisable() ? 0 : config.GetPTRStartChannel());
    setTextSelectionValue(&luaBackpackTelemetry, config.GetBackpackDisable() ? 0 : config.GetBackpackTlmMode());
    setStringValue(&luaBackpackVersion, backpackVersion);
  }

#if defined(RADIO_LR1121)
  if (!runtimeFreqAutoApplyInProgress
      && connectionState == connected
      && (FHSSruntimeFreqPending() || FHSShighRuntimeFreqPending()))
  {
    runtimeFreqAutoApplyInProgress = true;
    sendRuntimeFreqConfigToRx();
    FHSSactivatePendingRuntimeFrequencies();
    FHSSrandomiseFHSSsequence(uidMacSeedGet());
    deferExecutionMillis(250, []() {
      EnterBindingModeSafely();
    });
  }
  if (connectionState != connected)
  {
    runtimeFreqAutoApplyInProgress = false;
  }
  setTextSelectionValue(&luaRuntimeFreqEnable, firmwareOptions.runtime_freq_enabled ? 1U : 0U);
  setTextSelectionValue(&luaRuntimeFreqPreset, firmwareOptions.runtime_freq_preset < ARRAY_SIZE(runtimeFreqPresets) ? firmwareOptions.runtime_freq_preset : 0U);
  setFloatValue(&luaRuntimeFreqStartMHz, firmwareOptions.runtime_freq_start / 100000U);
  setFloatValue(&luaRuntimeFreqStopMHz, firmwareOptions.runtime_freq_stop / 100000U);
  luaRuntimeFreqCount.properties.u.value = firmwareOptions.runtime_freq_count;
  setTextSelectionValue(&luaRuntimeFreqAutoCount, firmwareOptions.runtime_freq_auto_count ? 1U : 0U);
  setTextSelectionValue(&luaRuntimeHighFreqEnable, firmwareOptions.runtime_high_freq_enabled ? 1U : 0U);
  setTextSelectionValue(&luaRuntimeHighFreqPreset, firmwareOptions.runtime_high_freq_preset < ARRAY_SIZE(runtimeHighFreqPresets) ? firmwareOptions.runtime_high_freq_preset : 0U);
  setFloatValue(&luaRuntimeHighFreqStartMHz, firmwareOptions.runtime_high_freq_start / 100000U);
  setFloatValue(&luaRuntimeHighFreqStopMHz, firmwareOptions.runtime_high_freq_stop / 100000U);
  luaRuntimeHighFreqCount.properties.u.value = firmwareOptions.runtime_high_freq_count;
  setTextSelectionValue(&luaRuntimeHighFreqAutoCount, firmwareOptions.runtime_high_freq_auto_count ? 1U : 0U);
  updateRuntimeFreqStatus();
#endif

  updateFolderNames();
}

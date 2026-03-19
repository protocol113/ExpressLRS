# Custom Frequency Configuration Implementation

## Overview

This document describes the **runtime custom frequency configuration** feature for ExpressLRS. This allows you to configure custom frequency ranges directly via the Lua script on your radio, without needing to recompile firmware.

## Quick Start - Using Custom Frequencies via Lua

### Accessing the Custom Frequency Menu

1. Power on your TX module and radio
2. Open the ExpressLRS Lua script on your radio
3. Navigate to **"Custom Freq"** folder

### Menu Options

| Option | Description |
|--------|-------------|
| **Freq Preset** | Quick presets: Off, 900 ISM, 868 EU, 915 US, 850 Custom, Custom |
| **Start Freq** | Start frequency in 0.1 MHz units (e.g., 9000 = 900.0 MHz) |
| **Stop Freq** | Stop frequency in 0.1 MHz units (e.g., 9300 = 930.0 MHz) |
| **Channels** | Number of FHSS channels (4-80) |
| **Apply & Rebind** | Apply changes and enter binding mode |

### Using Presets

Select a preset from **Freq Preset**:
- **Off** - Use default compiled domain
- **900 ISM** - 900-930 MHz, 40 channels
- **868 EU** - 863-870 MHz, 13 channels  
- **915 US** - 902-928 MHz, 40 channels
- **850 Custom** - 840-860 MHz, 40 channels (outside normal bands)
- **Custom** - Use your manually entered values

### Custom Frequency Configuration

1. Set **Freq Preset** to "Custom" (or it auto-selects when you edit values)
2. Set **Start Freq** (e.g., 8500 for 850.0 MHz)
3. Set **Stop Freq** (e.g., 8600 for 860.0 MHz)
4. Set **Channels** (recommended: bandwidth_MHz / 0.5 for good spacing)
5. Select **Apply & Rebind** to activate

### Important Notes

- **Both TX and RX must use the same frequency configuration** - rebind after changes
- Changes persist across power cycles
- For sub-GHz radios (SX1276/LR1121) only

---

## RX WiFi Configuration

You can also configure custom frequencies on receivers via the WiFi web interface. This is useful for configuring bound receivers that aren't currently connected to the TX.

### Accessing RX WiFi Configuration

1. Put the RX into WiFi mode (3 quick power cycles, or via button if available)
2. Connect to the RX's WiFi AP (default: "ExpressLRS RX")
3. Navigate to `http://10.0.0.1` in your browser
4. Go to the **Runtime Options** page

### Custom Frequency Settings

Under the **Custom Frequency (Advanced)** section:

| Field | Description |
|-------|-------------|
| **Enable Custom Frequency** | Toggle to enable/disable custom frequency mode |
| **Start Frequency (Hz)** | Start frequency in Hz (e.g., 900000000 for 900 MHz) |
| **Stop Frequency (Hz)** | Stop frequency in Hz (e.g., 930000000 for 930 MHz) |
| **Channel Count** | Number of FHSS channels (4-80) |

### Common Presets (enter values manually)

| Preset | Start (Hz) | Stop (Hz) | Channels |
|--------|------------|-----------|----------|
| 900 ISM | 900000000 | 930000000 | 40 |
| 868 EU | 863000000 | 870000000 | 13 |
| 915 US | 902000000 | 928000000 | 40 |
| 850 Custom | 840000000 | 860000000 | 40 |

### Workflow for Configuring TX + RX

1. **Configure TX first** via Lua script (set preset or custom values)
2. **Apply & Rebind** on TX to enter binding mode
3. **Put RX into WiFi mode** (don't bind yet)
4. **Configure RX** via WiFi with matching frequency settings
5. **Save and reboot** the RX
6. **Bind** TX and RX - they will now use the custom frequencies

---

## Technical Implementation (Original Plan)

The following describes the compile-time implementation plan for adding toggleable custom frequency configuration to the ExpressLRS Configurator. The goal is to add a checkbox that enables custom frequency input fields, allowing users to specify their own frequency ranges for red teaming and other specialized use cases.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    ExpressLRS Configurator                       │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │  [x] Enable Custom Frequencies                          │    │
│  │                                                         │    │
│  │  Sub-GHz Band:                                          │    │
│  │    Min Freq: [840000000] Hz                             │    │
│  │    Max Freq: [860000000] Hz                             │    │
│  │    Channels: [40]                                       │    │
│  │    Sync Freq: [850000000] Hz                            │    │
│  │    Name: [CUST850]                                      │    │
│  │                                                         │    │
│  │  Dual-Band (LR1121 S-Band):                             │    │
│  │    Min Freq: [2050000000] Hz                            │    │
│  │    Max Freq: [2100000000] Hz                            │    │
│  │    Channels: [40]                                       │    │
│  │    Sync Freq: [2075000000] Hz                           │    │
│  │    Name: [SBAND21]                                      │    │
│  └─────────────────────────────────────────────────────────┘    │
│                              │                                   │
│                              ▼                                   │
│              Generates user_defines.txt with:                    │
│              -DUSE_CUSTOM_FREQS                                  │
│              -DCUSTOM_FREQ_MIN_0=840000000                       │
│              -DCUSTOM_FREQ_MAX_0=860000000                       │
│              etc.                                                │
└─────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                    ExpressLRS Firmware                           │
│                                                                  │
│  FHSS.cpp reads preprocessor defines and configures domains[]   │
└─────────────────────────────────────────────────────────────────┘
```

## Files to Modify

### Part 1: ExpressLRS Configurator (Electron/React App)

Clone from: `https://github.com/ExpressLRS/ExpressLRS-Configurator`

#### 1. Find the main build options component

Location: `src/ui/views/` or similar (explore the repo structure)

Look for files containing:
- Regulatory domain dropdown
- Binding phrase input
- Other build options

#### 2. Add Custom Frequency Checkbox and Fields

Add a new section to the build options UI:

```tsx
// Pseudo-code - adapt to actual component structure

interface CustomFrequencyConfig {
  enabled: boolean;
  subGhz: {
    minFreq: number;
    maxFreq: number;
    channels: number;
    syncFreq: number;
    name: string;
  };
  dualBand: {
    minFreq: number;
    maxFreq: number;
    channels: number;
    syncFreq: number;
    name: string;
  };
}

// In the component:
const [customFreqEnabled, setCustomFreqEnabled] = useState(false);
const [customFreqConfig, setCustomFreqConfig] = useState<CustomFrequencyConfig>({
  enabled: false,
  subGhz: {
    minFreq: 840000000,
    maxFreq: 860000000,
    channels: 40,
    syncFreq: 850000000,
    name: 'CUST850'
  },
  dualBand: {
    minFreq: 2050000000,
    maxFreq: 2100000000,
    channels: 40,
    syncFreq: 2075000000,
    name: 'SBAND21'
  }
});

// JSX:
<FormGroup>
  <FormControlLabel
    control={
      <Checkbox
        checked={customFreqEnabled}
        onChange={(e) => setCustomFreqEnabled(e.target.checked)}
      />
    }
    label="Enable Custom Frequencies (Advanced)"
  />
</FormGroup>

{customFreqEnabled && (
  <Box sx={{ ml: 2, mt: 2 }}>
    <Typography variant="subtitle1">Sub-GHz Band Configuration</Typography>
    <TextField
      label="Min Frequency (Hz)"
      type="number"
      value={customFreqConfig.subGhz.minFreq}
      onChange={(e) => updateSubGhzConfig('minFreq', parseInt(e.target.value))}
    />
    <TextField
      label="Max Frequency (Hz)"
      type="number"
      value={customFreqConfig.subGhz.maxFreq}
      onChange={(e) => updateSubGhzConfig('maxFreq', parseInt(e.target.value))}
    />
    <TextField
      label="Channel Count"
      type="number"
      value={customFreqConfig.subGhz.channels}
      onChange={(e) => updateSubGhzConfig('channels', parseInt(e.target.value))}
    />
    <TextField
      label="Sync Frequency (Hz)"
      type="number"
      value={customFreqConfig.subGhz.syncFreq}
      onChange={(e) => updateSubGhzConfig('syncFreq', parseInt(e.target.value))}
    />
    <TextField
      label="Domain Name"
      value={customFreqConfig.subGhz.name}
      onChange={(e) => updateSubGhzConfig('name', e.target.value)}
    />
    
    <Typography variant="subtitle1" sx={{ mt: 2 }}>Dual-Band Configuration (LR1121 S-Band)</Typography>
    {/* Similar fields for dual-band */}
  </Box>
)}
```

#### 3. Modify Build Command Generation

Find where the configurator generates the build command or user_defines.txt.

Add logic to include custom frequency defines:

```typescript
function generateBuildFlags(config: BuildConfig): string[] {
  const flags: string[] = [];
  
  // ... existing flags ...
  
  if (config.customFreq?.enabled) {
    flags.push('-DUSE_CUSTOM_FREQS');
    
    // Sub-GHz
    flags.push(`-DCUSTOM_FREQ_MIN_0=${config.customFreq.subGhz.minFreq}`);
    flags.push(`-DCUSTOM_FREQ_MAX_0=${config.customFreq.subGhz.maxFreq}`);
    flags.push(`-DCUSTOM_FREQ_CHANNELS_0=${config.customFreq.subGhz.channels}`);
    flags.push(`-DCUSTOM_FREQ_SYNC_0=${config.customFreq.subGhz.syncFreq}`);
    flags.push(`-DCUSTOM_FREQ_NAME_0="${config.customFreq.subGhz.name}"`);
    
    // Dual-Band (for LR1121)
    flags.push(`-DCUSTOM_FREQ_MIN_DUAL=${config.customFreq.dualBand.minFreq}`);
    flags.push(`-DCUSTOM_FREQ_MAX_DUAL=${config.customFreq.dualBand.maxFreq}`);
    flags.push(`-DCUSTOM_FREQ_CHANNELS_DUAL=${config.customFreq.dualBand.channels}`);
    flags.push(`-DCUSTOM_FREQ_SYNC_DUAL=${config.customFreq.dualBand.syncFreq}`);
    flags.push(`-DCUSTOM_FREQ_NAME_DUAL="${config.customFreq.dualBand.name}"`);
  }
  
  return flags;
}
```

### Part 2: ExpressLRS Firmware

Location: `/Users/justinburnett/Desktop/ExpressLRS/src/`

#### 1. Modify FHSS.cpp

File: `src/lib/FHSS/FHSS.cpp`

Replace the domains array with conditional compilation:

```cpp
#if defined(RADIO_SX127X) || defined(RADIO_LR1121)

#if defined(RADIO_LR1121)
#include "LR1121Driver.h"
#else
#include "SX127xDriver.h"
#endif

// Custom frequency support via build flags
#ifdef USE_CUSTOM_FREQS

// Sub-GHz defaults (can be overridden)
#ifndef CUSTOM_FREQ_MIN_0
#define CUSTOM_FREQ_MIN_0 840000000
#endif
#ifndef CUSTOM_FREQ_MAX_0
#define CUSTOM_FREQ_MAX_0 860000000
#endif
#ifndef CUSTOM_FREQ_CHANNELS_0
#define CUSTOM_FREQ_CHANNELS_0 40
#endif
#ifndef CUSTOM_FREQ_SYNC_0
#define CUSTOM_FREQ_SYNC_0 850000000
#endif
#ifndef CUSTOM_FREQ_NAME_0
#define CUSTOM_FREQ_NAME_0 "CUSTOM0"
#endif

// Second sub-GHz domain defaults
#ifndef CUSTOM_FREQ_MIN_1
#define CUSTOM_FREQ_MIN_1 862000000
#endif
#ifndef CUSTOM_FREQ_MAX_1
#define CUSTOM_FREQ_MAX_1 1020000000
#endif
#ifndef CUSTOM_FREQ_CHANNELS_1
#define CUSTOM_FREQ_CHANNELS_1 80
#endif
#ifndef CUSTOM_FREQ_SYNC_1
#define CUSTOM_FREQ_SYNC_1 915000000
#endif
#ifndef CUSTOM_FREQ_NAME_1
#define CUSTOM_FREQ_NAME_1 "CUSTOM1"
#endif

// Dual-band defaults (LR1121 S-Band)
#ifndef CUSTOM_FREQ_MIN_DUAL
#define CUSTOM_FREQ_MIN_DUAL 2050000000
#endif
#ifndef CUSTOM_FREQ_MAX_DUAL
#define CUSTOM_FREQ_MAX_DUAL 2100000000
#endif
#ifndef CUSTOM_FREQ_CHANNELS_DUAL
#define CUSTOM_FREQ_CHANNELS_DUAL 40
#endif
#ifndef CUSTOM_FREQ_SYNC_DUAL
#define CUSTOM_FREQ_SYNC_DUAL 2075000000
#endif
#ifndef CUSTOM_FREQ_NAME_DUAL
#define CUSTOM_FREQ_NAME_DUAL "SBAND21"
#endif

const fhss_config_t domains[] = {
    {CUSTOM_FREQ_NAME_0, FREQ_HZ_TO_REG_VAL(CUSTOM_FREQ_MIN_0), FREQ_HZ_TO_REG_VAL(CUSTOM_FREQ_MAX_0), CUSTOM_FREQ_CHANNELS_0, CUSTOM_FREQ_SYNC_0},
    {CUSTOM_FREQ_NAME_1, FREQ_HZ_TO_REG_VAL(CUSTOM_FREQ_MIN_1), FREQ_HZ_TO_REG_VAL(CUSTOM_FREQ_MAX_1), CUSTOM_FREQ_CHANNELS_1, CUSTOM_FREQ_SYNC_1},
    {"EU868",  FREQ_HZ_TO_REG_VAL(863275000), FREQ_HZ_TO_REG_VAL(869575000), 13, 868000000},
    {"IN866",  FREQ_HZ_TO_REG_VAL(865375000), FREQ_HZ_TO_REG_VAL(866950000), 4, 866000000},
    {"AU433",  FREQ_HZ_TO_REG_VAL(433420000), FREQ_HZ_TO_REG_VAL(434420000), 3, 434000000},
    {"EU433",  FREQ_HZ_TO_REG_VAL(433100000), FREQ_HZ_TO_REG_VAL(434450000), 3, 434000000},
    {"US433",  FREQ_HZ_TO_REG_VAL(433250000), FREQ_HZ_TO_REG_VAL(438000000), 8, 434000000},
    {"US433W", FREQ_HZ_TO_REG_VAL(423500000), FREQ_HZ_TO_REG_VAL(438000000), 20, 434000000},
};

#if defined(RADIO_LR1121)
const fhss_config_t domainsDualBand[] = {
    {
    #if defined(Regulatory_Domain_EU_CE_2400)
        "CE_LBT",
    #else
        CUSTOM_FREQ_NAME_DUAL,
    #endif
    FREQ_HZ_TO_REG_VAL(CUSTOM_FREQ_MIN_DUAL), FREQ_HZ_TO_REG_VAL(CUSTOM_FREQ_MAX_DUAL), CUSTOM_FREQ_CHANNELS_DUAL, CUSTOM_FREQ_SYNC_DUAL}
};
#endif

#else // !USE_CUSTOM_FREQS - Original hardcoded values

const fhss_config_t domains[] = {
    {"AU915",  FREQ_HZ_TO_REG_VAL(915500000), FREQ_HZ_TO_REG_VAL(926900000), 20, 921000000},
    {"FCC915", FREQ_HZ_TO_REG_VAL(903500000), FREQ_HZ_TO_REG_VAL(926900000), 40, 915000000},
    {"EU868",  FREQ_HZ_TO_REG_VAL(863275000), FREQ_HZ_TO_REG_VAL(869575000), 13, 868000000},
    {"IN866",  FREQ_HZ_TO_REG_VAL(865375000), FREQ_HZ_TO_REG_VAL(866950000), 4, 866000000},
    {"AU433",  FREQ_HZ_TO_REG_VAL(433420000), FREQ_HZ_TO_REG_VAL(434420000), 3, 434000000},
    {"EU433",  FREQ_HZ_TO_REG_VAL(433100000), FREQ_HZ_TO_REG_VAL(434450000), 3, 434000000},
    {"US433",  FREQ_HZ_TO_REG_VAL(433250000), FREQ_HZ_TO_REG_VAL(438000000), 8, 434000000},
    {"US433W", FREQ_HZ_TO_REG_VAL(423500000), FREQ_HZ_TO_REG_VAL(438000000), 20, 434000000},
};

#if defined(RADIO_LR1121)
const fhss_config_t domainsDualBand[] = {
    {
    #if defined(Regulatory_Domain_EU_CE_2400)
        "CE_LBT",
    #else
        "ISM2G4",
    #endif
    FREQ_HZ_TO_REG_VAL(2400400000), FREQ_HZ_TO_REG_VAL(2479400000), 80, 2440000000}
};
#endif

#endif // USE_CUSTOM_FREQS
```

#### 2. Update WebUI Domain Names

Files to update:
- `src/html/src/pages/tx-options-panel.js`
- `src/html/src/pages/rx-options-panel.js`

These files contain the domain dropdown. For dynamic names, you'd need to either:
1. Keep them static (simpler)
2. Generate them at build time (more complex)

For simplicity, keep the WebUI showing generic names like "Custom 0", "Custom 1" when custom frequencies are enabled.

### Part 3: Python Build Script Updates

File: `src/python/build_flags.py`

Add processing for custom frequency defines:

```python
# Add after line 68 in process_build_flag function

# Custom frequency support
if "USE_CUSTOM_FREQS" in define:
    json_flags['custom-freqs-enabled'] = True

if "CUSTOM_FREQ_MIN_0=" in define:
    parts = re.search(r"(.*)=(\d+)$", define)
    if parts:
        json_flags['custom-freq-min-0'] = int(parts.group(2))

if "CUSTOM_FREQ_MAX_0=" in define:
    parts = re.search(r"(.*)=(\d+)$", define)
    if parts:
        json_flags['custom-freq-max-0'] = int(parts.group(2))

# ... repeat for other custom frequency defines ...
```

## Implementation Steps

### Step 1: Clone and Set Up Configurator

```bash
cd /Users/justinburnett/Desktop
git clone https://github.com/ExpressLRS/ExpressLRS-Configurator.git
cd ExpressLRS-Configurator
npm install
```

### Step 2: Explore Configurator Structure

Key directories to examine:
- `src/ui/` - React UI components
- `src/api/` - Backend API calls
- `src/library/` - Build logic

Look for:
- Where regulatory domain dropdown is defined
- How build flags are generated
- How the build command is executed

### Step 3: Add UI Components

1. Create a new component for custom frequency input
2. Add checkbox to enable/disable the feature
3. Add input fields for each frequency parameter
4. Style to match existing UI

### Step 4: Integrate with Build System

1. Modify the build flag generation
2. Pass custom frequency values as -D defines
3. Test with LOCAL build mode

### Step 5: Update Firmware

1. Apply the FHSS.cpp changes from Part 2 above
2. Test compilation with custom defines
3. Verify frequencies are correctly applied

### Step 6: Test End-to-End

1. Build configurator: `npm run build`
2. Run configurator: `npm start`
3. Enable custom frequencies
4. Build firmware with custom values
5. Verify output frequencies

## Frequency Range Guidelines

### SX1270/LR1121 Sub-GHz (Band 1)
- **Supported Range:** 862 MHz - 1020 MHz
- **Recommended Channels:** 40-80 for wide bands, 20-40 for narrow bands
- **Sync Frequency:** Should be within the band, preferably center

### LR1121 S-Band
- **Supported Range:** 1900 MHz - 2100 MHz
- **Recommended Channels:** 40-80
- **Note:** Hardware PA must support this band

### Channel Count Guidelines
- **Bandwidth < 10 MHz:** 8-13 channels
- **Bandwidth 10-25 MHz:** 20-40 channels
- **Bandwidth > 25 MHz:** 40-80 channels

## Testing Checklist

- [ ] Checkbox enables/disables custom frequency fields
- [ ] Input validation for frequency ranges
- [ ] Build flags correctly generated
- [ ] Firmware compiles with custom defines
- [ ] Domain names appear correctly in WebUI
- [ ] Frequencies verified with spectrum analyzer

## Notes

- Keep TX and RX firmware in sync (same custom frequencies)
- Test link establishment after frequency changes
- Document any frequency ranges used for your red teaming scenarios
- Consider creating preset configurations for common use cases

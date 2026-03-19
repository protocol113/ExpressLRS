In# ExpressLRS Stealth Mode Implementation Guide

## Overview

This document describes techniques to make ExpressLRS RF traffic harder to detect and fingerprint by adversaries using SDR (Software Defined Radio) equipment. These modifications are intended for specialized applications like red teaming, security research, and covert operations.

**⚠️ WARNING**: Modifying RF parameters may violate local regulations. Ensure compliance with applicable laws before deployment.

---

## RF Fingerprinting Vectors

An adversary with SDR equipment can identify standard ELRS traffic through several characteristics:

| Vector | Standard ELRS | Detection Method |
|--------|---------------|------------------|
| Sync Word | `0x12` (LoRa default) | Pattern matching |
| Packet Size | Exactly 8 or 13 bytes | Length analysis |
| Timing | Perfect fixed intervals | Periodicity detection |
| FHSS Pattern | Deterministic sequence | Sequence prediction |
| Preamble | Fixed 8-12 symbols | Waveform analysis |
| Modulation | Known SF/BW/CR combos | Parameter detection |

---

## Countermeasure 1: Custom Sync Word (High Impact, Easy)

### What It Does
The LoRa sync word is transmitted **in the clear** at the start of every packet. Standard ELRS uses `0x12`, which is the default LoRa sync word. Changing this immediately breaks compatibility with standard LoRa scanners.

### Current Implementation
```cpp
// src/lib/SX127xDriver/SX127xRegs.h
#define SX127X_SYNC_WORD 0x12  // Default LoRa sync word
```

### Stealth Implementation

#### Option A: UID-Derived Sync Word (Recommended)
Derive the sync word from the binding phrase, so TX and RX automatically match:

```cpp
// In radio initialization, after UID is set:
uint8_t stealthSyncWord = (UID[0] ^ UID[1] ^ UID[2]) | 0x01;  // Ensure non-zero

// Validate against allowed sync words (SX127x has restrictions)
while (!SyncWordOk(stealthSyncWord)) {
    stealthSyncWord++;
}

Radio.SetSyncWord(stealthSyncWord);
```

#### Option B: User-Configurable Sync Word
Add to Lua menu and WiFi config:
- Store in `firmwareOptions` or `TxConfig`
- Range: See `SX127x_AllowedSyncwords[]` for valid values
- Must match on TX and RX

### Files to Modify
- `src/lib/SX127xDriver/SX127x.cpp` - `SetSyncWord()` function
- `src/lib/SX1280Driver/SX1280.cpp` - FLRC sync word handling
- `src/lib/OPTIONS/options.h` - Add `customSyncWord` field
- `src/lib/config/config.h` - Add to TxConfig if Lua-configurable

### Allowed Sync Words (SX127x)
```cpp
const uint8_t SX127x_AllowedSyncwords[105] = {
    0, 5, 6, 7, 11, 12, 13, 15, 18, 21, 23, 26, 29, 30, 31, 33, 34,
    37, 38, 39, 40, 42, 44, 50, 51, 54, 55, 57, 58, 59, 61, 63, 65,
    67, 68, 71, 77, 78, 79, 80, 82, 84, 86, 89, 92, 94, 96, 97, 99,
    101, 102, 105, 106, 109, 111, 113, 115, 117, 118, 119, 121, 122,
    124, 126, 127, 129, 130, 138, 143, 161, 170, 172, 173, 175, 180,
    181, 182, 187, 190, 191, 192, 193, 196, 199, 201, 204, 205, 208,
    209, 212, 213, 219, 220, 221, 223, 227, 229, 235, 239, 240, 242,
    243, 246, 247, 255
};
```

---

## Countermeasure 2: Deterministic Timing Jitter (Medium Impact, Medium Difficulty)

### The Challenge
ELRS packets are sent at **exact** intervals (e.g., every 2000µs for 500Hz). This regularity is a strong fingerprint. However, random jitter would break the RX's Phase Frequency Detector (PFD) synchronization.

### Solution: Pseudo-Random Deterministic Jitter
Both TX and RX compute the **same** jitter value from shared state:

```cpp
// Shared function - both TX and RX use this
int16_t getTimingJitter(uint8_t nonce, uint8_t fhssIndex, uint8_t *UID) {
    // Seed with values both sides know
    uint32_t seed = ((uint32_t)nonce << 16) | ((uint32_t)fhssIndex << 8) | UID[5];
    
    // Simple LCG hash
    seed = seed * 1103515245 + 12345;
    
    // Return jitter in range [-JITTER_MAX, +JITTER_MAX] microseconds
    #define JITTER_MAX 100
    return (int16_t)((seed % (2 * JITTER_MAX + 1)) - JITTER_MAX);
}
```

### TX Implementation
```cpp
// src/src/tx_main.cpp - in timerCallback() or SendRCdataToRF()

void ICACHE_RAM_ATTR SendRCdataToRF() {
    // ... existing code ...
    
    #ifdef STEALTH_TIMING_JITTER
    int16_t jitter = getTimingJitter(OtaNonce, FHSSptr, UID);
    if (jitter > 0) {
        delayMicroseconds(jitter);
    }
    // Note: negative jitter handled by sending slightly early
    // which requires timer adjustment
    #endif
    
    Radio.TXnb((uint8_t*)&otaPkt, ...);
}
```

### RX Implementation
```cpp
// src/src/rx_main.cpp - in ProcessRFPacket() or HWtimerCallbackTock()

void ICACHE_RAM_ATTR HWtimerCallbackTock() {
    #ifdef STEALTH_TIMING_JITTER
    int16_t expectedJitter = getTimingJitter(OtaNonce, FHSSptr, UID);
    PFDloop.intEvent(micros() + expectedJitter);
    #else
    PFDloop.intEvent(micros());
    #endif
    
    // ... rest of tock handling ...
}
```

### Shared State Available
| Variable | Description | Synced Via |
|----------|-------------|------------|
| `OtaNonce` | Packet counter | SYNC packets |
| `FHSSptr` | Current hop index | SYNC packets |
| `UID[0-5]` | Binding phrase hash | Compile time / binding |

### Timing Diagram
```
Without Jitter (detectable pattern):
TX: |--2000--|--2000--|--2000--|--2000--|--2000--|
    ↓        ↓        ↓        ↓        ↓

With Deterministic Jitter (obscured pattern):
TX: |--2050--|--1970--|--2080--|--1990--|--2020--|
RX: |--2050--|--1970--|--2080--|--1990--|--2020--|  ← Matches!
    (both compute same jitter from shared nonce/fhss/UID)
```

---

## Countermeasure 3: Variable Packet Padding (High Impact, Hard)

### What It Does
Standard ELRS packets are exactly 8 bytes (4ch) or 13 bytes (8ch). Adding random padding makes length analysis ineffective.

### Implementation Concept
```cpp
// OTA packet with optional padding
typedef struct {
    OTA_Packet_s packet;      // Standard 8 or 13 bytes
    uint8_t padding[4];       // 0-4 bytes of random padding
    uint8_t paddingLen: 3;    // How many padding bytes (in header)
} OTA_Packet_Padded_s;
```

### Challenges
- Increases airtime (reduces max packet rate)
- Requires OTA protocol changes (breaks compatibility)
- Padding length must be communicated to RX
- CRC must cover padding

### Recommendation
Only implement if other countermeasures are insufficient. Consider using a build flag:
```cpp
#ifdef STEALTH_PACKET_PADDING
    // Variable length packets
#else
    // Standard fixed length
#endif
```

---

## Countermeasure 4: Custom Frequency Bands (Implemented)

### Current Status: ✅ Implemented
See `CUSTOM_FREQUENCY_IMPLEMENTATION.md` for details.

### Stealth Benefits
- Operate outside standard ISM bands (where scanners focus)
- Use non-standard frequency spacing
- Avoid known ELRS frequency domains

### Configuration
**TX (Lua Script):**
- Navigate to "Custom Freq" folder
- Set custom start/stop frequencies
- Apply & Rebind

**RX (WiFi):**
- Connect to RX WiFi AP
- Set matching frequencies in Runtime Options
- Save & Reboot

---

## Countermeasure 5: Variable Preamble Length (Low Impact, Easy)

### What It Does
Standard ELRS uses fixed preamble lengths (8-12 symbols). Varying this slightly makes waveform fingerprinting harder.

### Implementation
```cpp
// In SetRFLinkRate() or Radio.Config()
uint8_t basePreamble = ModParams->PreambleLen;

#ifdef STEALTH_VARIABLE_PREAMBLE
    // Add 0-4 symbols based on nonce
    uint8_t extraPreamble = (OtaNonce ^ UID[4]) & 0x03;
    Radio.SetPreambleLength(basePreamble + extraPreamble);
#else
    Radio.SetPreambleLength(basePreamble);
#endif
```

### Note
Preamble changes are less effective than sync word changes because the preamble is just repeated symbols (for clock recovery), not a unique identifier.

---

## Countermeasure 6: Non-Standard Modulation (Medium Impact, Medium Difficulty)

### What It Does
Use SF/BW/CR combinations that don't match known ELRS profiles.

### Standard ELRS Parameters
| Rate | SF | BW | CR |
|------|----|----|-----|
| 500Hz | 6 | 500kHz | 4/5 |
| 250Hz | 7 | 500kHz | 4/5 |
| 150Hz | 8 | 500kHz | 4/5 |
| 50Hz | 9 | 500kHz | 4/5 |

### Stealth Options
- Use SF5 (if radio supports)
- Use 250kHz or 125kHz bandwidth
- Use CR 4/6, 4/7, or 4/8

### Trade-offs
- Different parameters affect range, airtime, and interference rejection
- Must be set identically on TX and RX
- May require custom rate definitions in `common.cpp`

---

## Implementation Priority

| Countermeasure | Impact | Difficulty | Recommended |
|----------------|--------|------------|-------------|
| Custom Sync Word | ⭐⭐⭐⭐⭐ | Easy | ✅ First priority |
| Custom Frequencies | ⭐⭐⭐⭐ | Done | ✅ Already implemented |
| Timing Jitter | ⭐⭐⭐ | Medium | ✅ Second priority |
| Variable Preamble | ⭐⭐ | Easy | Optional |
| Non-Standard Modulation | ⭐⭐⭐ | Medium | Optional |
| Packet Padding | ⭐⭐⭐⭐ | Hard | Future work |

---

## Detection Comparison

### Standard ELRS (Easily Detected)
```
SDR Analysis:
├── Sync Word: 0x12 ← "Standard LoRa"
├── Packet Size: 8 bytes ← "ELRS 4-channel"
├── Timing: 2000µs ± 0 ← "500Hz ELRS"
├── Frequencies: 903-927 MHz ← "FCC915 domain"
├── Modulation: SF6/BW500 ← "ELRS 500Hz rate"
└── Verdict: "ExpressLRS 500Hz FCC915"
```

### Stealth ELRS (Difficult to Identify)
```
SDR Analysis:
├── Sync Word: 0xA7 ← "Unknown protocol"
├── Packet Size: 8-12 bytes ← "Variable length"
├── Timing: 1900-2100µs ← "Irregular interval"
├── Frequencies: 850-870 MHz ← "Non-standard band"
├── Modulation: SF7/BW250 ← "Unknown profile"
└── Verdict: "Unidentified spread spectrum"
```

---

## Build Flags

Add to `platformio.ini` or build defines:

```ini
[env:stealth]
build_flags = 
    -DSTEALTH_SYNC_WORD          ; Enable custom sync word
    -DSTEALTH_TIMING_JITTER      ; Enable timing jitter
    -DSTEALTH_VARIABLE_PREAMBLE  ; Enable variable preamble
    -DJITTER_MAX=100             ; Max jitter in microseconds
```

---

## Testing Stealth Mode

### With SDR (Recommended)
1. Use SDR# or GQRX to monitor the frequency band
2. Compare waterfall patterns between standard and stealth modes
3. Verify timing irregularity with signal analysis tools

### Without SDR
1. Verify TX and RX connect successfully
2. Check link quality and latency are acceptable
3. Confirm settings persist across reboots

---

## Legal Considerations

- **Frequency Selection**: Ensure custom frequencies are legal in your jurisdiction
- **Power Limits**: Stealth mode does not exempt you from power regulations
- **Interference**: Avoid frequencies used by safety-critical systems
- **Documentation**: Keep records of your configuration for compliance

---

## Future Work

- [ ] Implement UID-derived sync word
- [ ] Add timing jitter with build flag
- [ ] Create "Stealth Mode" preset in Lua menu
- [ ] Add sync word configuration to WiFi UI
- [ ] Investigate FHSS sequence randomization
- [ ] Research additional fingerprinting vectors

# ESP32-S3 AI Voice Assistant

**Hardware:** ESP32-S3-N16R8 Touch AMOLED 1.8" (368×448 QSPI display)  
**Platform:** PlatformIO (Arduino framework)  
**Status:** ✅ Test Mode | ✅ Recording & Upload | 🚧 TTS Playback  

Modular AI voice assistant with LVGL native UI, audio recording/playback, **raw TLS HTTPS client**, offline queue system, and comprehensive test mode.

---

## 🚀 Quick Start

### Build & Upload (Development)
```bash
cd C:\ESP32-S3-Touch-AMOLED-1.8\ESP-IDF-v5.3.2\LVGL_Native_PlatformIO
pio run -t upload && pio device monitor
```

**Build Times:**  
- First build: 2-3 min  
- Incremental: **15-30 sec** ⚡

### Flash Pre-Built Firmware (Distribution)

For distributing firmware to other devices without requiring PlatformIO:

**Option 1: Build + Flash (All-in-One)**
```bash
build_and_flash.bat
```
- Builds the project
- Generates SPIFFS filesystem image
- Flashes everything automatically

**Option 2: Flash Pre-Built Binaries**
```bash
flash_complete_firmware.bat
```
- Flashes pre-built binaries from `.pio/build/esp32-s3-n16r8/`
- Requires prior build (run `pio run` first)

**What Gets Flashed:**
- **Bootloader** (0x0000) - ESP32-S3 bootloader
- **Partition Table** (0x8000) - Custom partition layout
- **Application** (0x10000) - Main firmware (3MB)
- **SPIFFS** (0x610000) - Audio files (`boot.wav`, `click_sound.wav`)

**⚠️ NVS (Settings) Preservation:**
- **NVS partition (0x9000-0xE000) is NOT erased**
- WiFi credentials, server config, and audio settings are **preserved**
- Safe to reflash for firmware updates without losing configuration

**Requirements:**
- Python 3.7+ (auto-downloads `esptool` if needed)
- USB driver (CP210x or CH340)
- **No PlatformIO needed** for flashing

**Note:** This is the **correct** way to distribute ESP32-S3 firmware. Simply sharing `firmware.bin` will fail because PlatformIO requires bootloader and partition table at specific addresses.

### Create Distribution Package

To create a ready-to-share package for others:

```bash
create_distribution_package.bat
```

This creates a `dist/ESP32_Voice_Assistant_[DATE]/` folder containing:
- All binary files (bootloader, partitions, firmware, SPIFFS)
- Simplified `flash.bat` script
- `README.txt` with installation instructions
- File manifest

Compress the folder to `.zip` and share. Recipients only need Python installed.

### First Boot (Test Mode Entry)
- Device automatically enters Test Mode if no WiFi configured
- OR hold **bottom button (IO Exp Pin 4)** for 3+ seconds during boot
- Connect to AP: **"ESP_Assistant"**
- Browser auto-opens to configuration portal (192.168.4.1)

---

## 💻 Hardware Configuration

| Component | Interface | Address/Pins | Description |
|-----------|-----------|--------------|-------------|
| **Display** | QSPI | SH8601, 368×448 | AMOLED with rounded corners |
| **Touch** | I2C | 0x38 (FT3168) | Capacitive touch panel |
| **Audio** | I2S | ES8311 | Full-duplex codec (mic + speaker) |
| **SD Card** | SDMMC 1-bit | CLK=2, CMD=1, D0=3 | POSIX file system |
| **PMIC** | I2C | 0x34 (AXP2101) | Battery + charging monitor |
| **RTC** | I2C | 0x51 (PCF85063) | Real-time clock |
| **IMU** | I2C | 0x6B (QMI8658) | 6-axis accelerometer/gyro |
| **Buttons** | GPIO / I2C | GPIO0, IO4@0x20 | Top button / Bottom button |
| **Flash** | QSPI | 16MB | QIO mode |
| **PSRAM** | OPI | 8MB | Octal SPI @ 40MHz |

### Button Logic (CRITICAL)
**Bottom Button (IO Expander Pin 4)** has REVERSED logic:
- `HIGH` (1) = Button **PRESSED** (pulled to ground)
- `LOW` (0) = Button **RELEASED** (pulled up)

**Test mode entry:** Hold bottom button during boot (reads HIGH for 3+ seconds)

---

## 🧪 Test Mode Features

### Web Interface Pages

#### 1. **Status Dashboard** (`/`)
- System information (uptime, memory, PSRAM)
- Hardware status (SD card, battery, WiFi signal)
- Quick navigation to all test pages

#### 2. **WiFi Configuration** (`/wifi`)
- Configure SSID and password
- Credentials saved to NVS
- Auto-reboot after save

#### 3. **Server Configuration** (`/serverconfig`)
- Set backend server URL
- Configure device authentication secret (HMAC-SHA256)
- **SSL Certificate Management** (Advanced)
  - Upload custom Root CA certificate without reflashing firmware
  - Stored in NVS (up to 4KB)
  - Falls back to hardcoded default if not set
  - **How to obtain new certificates:**
    1. Visit your server domain in browser (on your PC, not the ESP32)
    2. Click padlock icon → "Certificate is valid"
    3. Export entire certificate chain (PEM format)
    4. Copy/paste entire chain into web form textarea
  - Useful when server SSL certs renew (e.g., Let's Encrypt every 90 days)
  - **Note:** Test mode has no internet access, so you can't test the connection. Just save the config and test after rebooting to normal mode.
- All settings saved to NVS

#### 4. **Audio Test** (`/audiotest`)
- **Record:** 5-second test recording (saves as `test_audio.wav`)
- **Play:** Playback test audio with mono→stereo conversion
- **Stop:** End playback
- Audio streaming with I2S DMA (1KB chunks, dynamic buffer allocation)

#### 5. **Audio Settings** (`/audiosettings`)
- **Volume:** 0-100 slider (speaker output)
- **Mic Gain:** 0-7 slider (microphone sensitivity)
- Settings saved to NVS

#### 6. **File Browser** (`/filebrowser`)
- Browse SD card directories
- **Upload** files to any directory
- **Download** files (streamed via HTTP)
- **Delete** files with confirmation
- Table layout with fixed column widths
- POSIX filesystem operations (`opendir`/`readdir`/`stat`)

#### 7. **Button Test** (`/buttontest`)
- Real-time button press counter
- Tests top button (GPIO0) and bottom button (IO Exp Pin 4)

#### 8. **SD Card Test** (`/sdtest`)
- Test write/read operations
- Speed benchmarks
- File system information

---

## 📂 Project Structure

```
LVGL_Native_PlatformIO/
├── platformio.ini              # Build configuration
├── boards/
│   └── esp32-s3-n16r8.json     # Custom board definition
├── src/
│   ├── main.cpp                # Main orchestrator
│   ├── hardware_pins.h         # Pin definitions
│   ├── lv_conf.h               # LVGL configuration
│   │
│   ├── test_mode.cpp/h         # Test mode web server
│   ├── nvs_manager.cpp/h       # NVS storage
│   ├── sd_manager.cpp/h        # SD card operations
│   ├── battery_manager.cpp/h   # AXP2101 PMIC
│   ├── rtc_manager.cpp/h       # PCF85063 RTC + NTP
│   ├── imu_manager.cpp/h       # QMI8658 sensor
│   │
│   ├── audio/
│   │   ├── audio_manager.cpp/h # ES8311 codec control
│   │   ├── audio_pins.h        # Audio pin mapping
│   │   └── es8311_driver.cpp/h # ES8311 driver
│   │
│   └── display/
│       ├── display_screens.cpp/h   # LVGL screen definitions
│       ├── display_galaxy.cpp/h    # Galaxy animation widget
│       └── display_manager.cpp/h   # Screen state machine
│
└── lib/                        # Libraries (copied from Arduino)
```

---

## 🎙️ Audio System

### Recording
- **Sample Rate:** 16kHz mono
- **Format:** 16-bit PCM WAV
- **Buffer:** 2MB PSRAM (streaming to SD)
- **Chunk Size:** 64KB writes
- **Filename (Test):** `/sdcard/recordings/test_audio.wav` (overwrites)
- **Filename (Normal):** `/sdcard/recordings/rec_YYYY-MM-DD_HH-MM-SS.wav` (RTC timestamp)

### Playback
- **Streaming:** 1KB chunks from SD card
- **Conversion:** Mono → Stereo (duplicate channels)
- **Buffer:** Dynamic allocation (freed after each I2S write)
- **Stack Safety:** No large stack arrays (prevents overflow)

### Audio Manager API
```cpp
bool audio_start_recording(const char* filename = nullptr);  // nullptr = use RTC timestamp
void audio_update_recording();  // Call in loop
bool audio_stop_recording();

bool audio_play_file(const char* filename);
void audio_update_playback();   // Call in loop
bool audio_stop_playback();

void audio_set_volume(uint8_t volume);       // 0-100
void audio_set_mic_gain(uint8_t gain);       // 0-7
```

### Audio Assets (SPIFFS UI Sounds)
Short WAV files for UI feedback stored in SPIFFS (9.9MB partition):

**Files:**
- `boot.wav` - Boot chime (1-2 sec, plays after setup complete)
- `click.wav` - Volume adjustment feedback (50-100ms tick)

**Format:** 16kHz mono 16-bit PCM WAV (matches recording format)

**Usage:**
```cpp
// Add files to data/ folder, then upload:
pio run --target uploadfs

// In code:
audio_play_boot_sound();   // Plays /boot.wav
audio_play_click_sound();  // Plays /click.wav
```

**Playback:** Blocking, streams from SPIFFS → I2S (reuses recording I2S config)

---

## 📁 File System

### SD Card Manager (POSIX)
All file operations use POSIX functions for compatibility:
```cpp
DIR* opendir(const char* path);
struct dirent* readdir(DIR* dir);
struct stat stat(const char* path);
FILE* fopen(const char* path, const char* mode);
int remove(const char* path);
```

### Directory Structure
```
/sdcard/
├── recordings/
│   ├── test_audio.wav           # Test mode recording
│   ├── rec_2025-10-20_01-48-30.wav  # Normal mode (RTC timestamp)
│   └── ...
└── tts/
    └── response_*.wav           # Downloaded TTS audio (future)
```

### File Browser Features
- **Table Layout:** Fixed-width columns (Name: 50%, Size: 20%, Actions: 30%)
- **Upload:** Multipart form data → POSIX `fwrite()`
- **Download:** Chunked streaming (1KB blocks) → HTTP response
- **Delete:** POSIX `remove()` with confirmation
- **Navigation:** Click folders to browse, ".." for parent directory

---

## ⚙️ PlatformIO Configuration

### Critical Settings (`platformio.ini`)
```ini
[env:esp32-s3-n16r8]
platform = espressif32 @ ^6.7.0
board = esp32-s3-n16r8                        # Custom board
framework = arduino

board_build.flash_mode = qio                  # QIO flash
board_build.flash_size = 16MB
board_build.psram = enabled
board_build.arduino.memory_type = qio_opi     # ⚡ CRITICAL: QIO flash + OPI PSRAM
board_build.arduino.memory_freq = 40m         # Safe (80m if stable)
board_build.partitions = custom_partitions.csv

board_build.f_cpu = 240000000L
board_build.f_flash = 80000000L
```

### Why `qio_opi`? (CRITICAL!)
- **N16R8** = **QIO flash** + **OPI PSRAM**
- ✅ `qio_opi` = Correct for this hardware
- ❌ `opi_opi` = Causes "Octal Flash EFUSE not configured" boot loop
- ❌ `opi_qspi` = Wrong format

### Partition Table
```csv
# Name,   Type, SubType, Offset,  Size,   Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x300000,
app1,     app,  ota_1,   0x310000,0x300000,
spiffs,   data, spiffs,  0x610000,0x9F0000,
```
**Total:** 3MB App0 + 3MB App1 + 9.9MB SPIFFS

---

## 🌌 LVGL Display System

### Current Status
✅ **Galaxy Animation** - 500 white stars, 3D elliptical orbits, 100+ FPS  
✅ **Display Manager** - State-based screen controller  
✅ **IDLE Screen** - Galaxy + status bar + power button  
🚧 **Recording Screen** - Progress bars (TODO)  
🚧 **Playing Screen** - VU meter (TODO)  
🚧 **Settings Screen** - Sliders (TODO)

### Galaxy Widget
- **Stars:** 500 white pixels in 3D elliptical orbits
- **Physics:** Kepler's 3rd law (inner orbits faster)
- **Performance:** 100+ FPS @ 3-7% CPU
- **Memory:** 5.2KB for all star data
- **Rendering:** Direct pixel write (bypasses LVGL for speed)

### Display Manager API
```cpp
bool display_manager_init(display_event_cb_t callback);
void display_manager_set_state(display_state_t state);
void display_manager_update_status(const idle_status_t *status);

// States: IDLE, RECORDING, UPLOADING, PLAYING, SETTINGS, SLEEP
```

### Screen Lifecycle Management
**Problem:** LVGL screens allocate memory in internal DRAM (not PSRAM). Without cleanup, multiple screen transitions exhaust internal RAM, causing crashes.

**Solution:** Two-tier screen lifecycle:
- **Persistent screens** (kept alive): IDLE, RECORDING, SLEEP, BOOT
  - Frequently used, worth keeping to avoid recreation overhead
- **Transient screens** (deleted after use): UPLOADING, DOWNLOADING, PLAYING, SETTINGS, etc.
  - Used once per cycle, deleted when transitioning away

**Memory tracking:**
```
[MEM][before_transition] Internal: 125 KB free (largest: 98 KB) | PSRAM: 7 MB free
🧹 Deleting PLAYING screen...
[MEM][after_cleanup] Internal: 138 KB free (largest: 112 KB) | PSRAM: 7 MB free
[MEM][after_create] Internal: 122 KB free (largest: 96 KB) | PSRAM: 7 MB free
```

**Critical:** Always call `lv_timer_handler()` after `lv_obj_del()` to let LVGL process deletions.

### Queue Screen (Brown Themed)
**Feature:** Automatic queue management for offline recordings.

**Flow:**
1. On transition to IDLE, check if `/sdcard/queue/` has files
2. If queue has files → show Queue screen instead of IDLE
3. If queue is empty → show normal IDLE screen

**Queue Screen UI:**
- **Brown/Orange theme** (#FF8C00 title)
- **Text box** - Shows list of queued filenames
- **Process button** (green upload icon) - Upload oldest recording
  - Only visible when WiFi connected
  - Uploads → Downloads → Plays → Moves to `/processed/`
- **Delete button** (red trash icon) - Delete all queue files
  - First click → Confirmation prompt
  - Second click → Delete all
  - Process button → Cancel confirmation

**User Flow:**
```
Record → Upload fails (offline) → Recording saved to /queue/
Boot device later → Queue screen shows
WiFi connected → Process button visible
Click Process → Upload → Download → Play → Move to /processed/
```

**Confirmation Flow:**
```
Click DELETE → "Delete all? Click DELETE to confirm, PROCESS to cancel"
Click DELETE again → All files deleted → Return to IDLE
Click PROCESS → Cancel → Show queue list again
```

**File Management:**
- Queue files: `/sdcard/queue/rec_YYYY-MM-DD_HH-MM-SS.wav`
- Processed files: `/sdcard/processed/rec_YYYY-MM-DD_HH-MM-SS.wav`
- Auto-cleanup: Queued files moved after successful playback

### LVGL Buffer Optimization (PSRAM)
**Problem:** LVGL draw buffers were allocated in internal RAM, consuming precious memory needed for widgets.

**Solution:** Adaptive buffer allocation with PSRAM preference (Oct 27, 2025):

**Three-tier fallback strategy:**
1. **FAST mode** (preferred): 1/4 screen (~80 KB × 2) in PSRAM
   - ~160 KB total in PSRAM
   - Fewer flush calls = smoother rendering
   - Best performance
   
2. **SAFE mode** (fallback): 1/20 screen (~16 KB × 2) in internal RAM
   - ~32 KB total in internal RAM
   - More flush calls but still functional
   
3. **MINIMAL mode** (emergency): Single buffer (~16 KB) in internal RAM
   - Single buffering (slight tearing possible)
   - System still boots

**Why larger buffers improve performance:**
- Larger buffer = LVGL renders more pixels per pass
- Fewer `flush_cb` calls = less overhead
- Less SPI/QSPI setup per chunk
- Smoother animations, higher effective FPS
- Even though PSRAM is slower than internal RAM, reducing flush count wins

**Buffer sizes:**
```
1/4 screen:  368×112 pixels = 41,216 pixels = 82,432 bytes (~80 KB)
1/20 screen: 368×22 pixels  = 8,096 pixels  = 16,192 bytes (~16 KB)
```

**Expected console output:**
```
🎨 LVGL: FAST mode - Buffers 2×80 KB (PSRAM, 1/4 screen)
💾 After LVGL buffers: Internal 228 KB free, PSRAM 7 MB free
```

**Memory impact:**
- FAST mode: Saves ~160 KB internal RAM (vs old internal buffer approach)
- SAFE mode: Uses ~32 KB internal RAM (fallback only)
- PSRAM usage: Only 160 KB out of 8 MB (2%)

This combined with screen lifecycle management prevents widget allocation failures.

### Testing 80 MHz PSRAM (Optional Performance Boost)

**Current:** 40 MHz PSRAM (safe, stable on all boards)  
**Optional:** 80 MHz PSRAM (2× bandwidth, but hardware-dependent)

**⚠️ WARNING:** Not all ESP32-S3 boards support 80 MHz PSRAM. Test carefully!

**How to test:**

1. **Edit `platformio.ini`** - Uncomment the 80 MHz line:
   ```ini
   build_flags =
       -DBOARD_HAS_PSRAM
       ; ... other flags ...
       -DCONFIG_SPIRAM_SPEED_80M      ; Enable 80 MHz PSRAM
   ```

2. **Clean build:**
   ```bash
   pio run -t clean
   pio run -t upload
   ```

3. **Check boot log** - Device should boot normally:
   ```
   💾 RAM: PSRAM 7 MB (7 MB free), Internal 228 KB (228 KB free) ✅
   ```

4. **Optional: Run stress test** - Call `psram_stress_test()` in `setup()`:
   ```cpp
   // After boot sequence (before switching to IDLE)
   psram_stress_test();
   ```

5. **Watch for issues:**
   - ✅ **Pass:** `PSRAM stress test PASSED (256 KB copied in XXX us = XX MB/s)`
   - ❌ **Fail:** Boot loop, Guru Meditation, verify fail, LVGL corruption

**If unstable:** Re-comment the line and flash back to 40 MHz.

**Benefits of 80 MHz:**
- 2× faster PSRAM → QSPI display transfers
- Faster audio buffer streaming
- Better TTS playback performance
- Smoother LVGL rendering (fewer flush bottlenecks)

**When NOT to use:**
- Board has poor PCB routing
- You see random crashes/corruption
- Device won't boot
- Stress test fails

---

## 🔧 Module System

### Independent Managers
Each manager is self-contained with clear initialization and API:

| Manager | Purpose | Key Functions |
|---------|---------|---------------|
| **test_mode** | WiFi AP + web config | `startTestMode()`, `handleTestMode()` |
| **audio_manager** | ES8311 codec control | `audio_start_recording()`, `audio_play_file()` |
| **sd_manager** | POSIX file operations | `openRecordingFile()`, `createDirectory()` |
| **nvs_manager** | Non-volatile storage | `loadWiFiCredentials()`, `saveAudioSettings()` |
| **battery_manager** | AXP2101 PMIC | `getBatteryPercent()`, `isCharging()` |
| **rtc_manager** | PCF85063 + NTP | `getCurrentDateTime()`, `getTimestampForFilename()` |
| **display_manager** | LVGL screen control | `display_manager_set_state()` |

### Module Dependencies
```
main.cpp
  ├─→ test_mode ───→ audio_manager ───→ sd_manager
  │                ├─→ nvs_manager
  │                └─→ battery_manager
  │
  ├─→ display_manager ───→ display_screens ───→ display_galaxy
  │
  └─→ rtc_manager (for timestamps)
```

---

## 🌐 HTTPS Client Module

### Current Implementation
Raw TLS client (`https_client.cpp/h`) using **WiFiClientSecure** for secure server communication.

**Memory Budget:** ~48KB internal DRAM (within ESP32-S3 limits)

### Architecture
- **No HTTPClient wrapper** - Direct TLS connection control
- **Manual HTTP formatting** - Complete control over headers/body
- **Full-duplex pump** - Reads RX while writing TX to prevent deadlock
- **Certificate validation** - Root CA chain for your-server.example.com

### Authentication
HMAC-SHA256 with device ID + ISO8601 timestamp:
```cpp
X-Device-ID: <MAC_ADDRESS>
X-Device-Time: 2025-10-26T16:15:00Z
X-Device-Signature: <HMAC_HEX>
```
**NTP sync required** - Server validates timestamp within ±5 minute window.

### Upload Flow
1. Check PSRAM buffer from recorder (zero-copy if available)
2. Fallback to SD reload for queue retries
3. Build multipart/form-data body in memory
4. TLS connect to server
5. Stream body in chunks with RX draining
6. Parse JSON response, extract TTS URL
7. Download TTS audio (smart buffer management):
   - Files ≤2 MB → PSRAM buffer (fast playback, no SD write)
   - Files >2 MB → Stream to SD card (saves PSRAM for UI)

### API
```cpp
void initHttpsClient();
ServerResponse transcribeAudio(const char* audioFilePath);
ServerResponse transcribeAudioFromBuffer(const uint8_t* data, size_t size, const char* name);
bool downloadAudio(const char* url, const char* savePath);

// Smart buffer management
const uint8_t* getTtsBuffer(size_t* outSize);  // Returns nullptr if file on SD
bool isTtsOnSdCard();                          // Check if last TTS saved to SD
const char* getTtsSdCardPath();                // Get SD path (if isTtsOnSdCard())
size_t getTtsSize();                           // Get size (works for both modes)
```

### Status
- ✅ Upload working with queue/retry
- ✅ HMAC authentication
- ✅ TTS download with smart buffer management (PSRAM/SD)
- ✅ Playback integration (automatic PSRAM/SD detection)
- ✅ Multi-turn conversations (ASK/CONFIRM modes)
- 🚧 Confirmation/cancel endpoints stubbed

### Test Project
**HTTPS_Upload_Download_Test_PIO** - Minimal standalone test for TLS verification.

---

## 🐛 Troubleshooting

### Compilation Errors
**"undefined reference to..."**
- Check `platformio.ini` library dependencies
- Run `pio run --target clean` then rebuild

**"PSRAM not found"**
- Verify `board_build.arduino.memory_type = qio_opi`
- Check `memory_freq = 40m` (not 80m if unstable)

### Runtime Issues
**Boot loop / "Octal Flash EFUSE not configured"**
- Change `memory_type` from `opi_opi` to `qio_opi`

**Test mode web server not accessible**
- Connect to "ESP_Assistant" AP
- Navigate to `192.168.4.1` manually if captive portal fails

**Audio playback causes crash**
- Fixed! Stack overflow from large stereo buffer
- Now uses dynamic allocation with `malloc()`/`free()`

**File browser upload fails**
- Check POSIX `fopen()` permissions
- Ensure SD card mounted at `/sdcard`

**File browser buttons misaligned**
- Fixed! Table layout with fixed column widths

---

## 📦 **Module Architecture**

### **Core Modules** (well-separated, single responsibility)
1. **audio_manager** - Audio recording, playback, codec (ES8311)
2. **battery_manager** - Battery monitoring, charging status (AXP2101)
3. **button_manager** - Button debouncing, event detection
4. **display_manager** - Display state coordination (LVGL screens)
5. **https_client** ⭐ REFACTORED - Raw TLS server communication (HMAC auth, multipart upload, TTS download)
6. **imu_manager** - IMU sensor reading (QMI8658)
7. **nvs_manager** - Non-volatile storage (settings persistence)
8. **rtc_manager** - Real-time clock (PCF85063)
9. **sd_manager** - SD card file operations
10. **sleep_manager** ⭐ NEW - Sleep mode, shake-to-wake, clock updates
11. **settings_manager** ⭐ NEW - Settings UI, NVS integration, hardware control
12. **state_machine** - App state transitions, inactivity tracking
13. **upload_queue** ⭐ NEW - Offline recording queue, retry logic, backoff
14. **wifi_manager** - WiFi connectivity, reconnection
15. **test_mode** - Web-based test interface

### **Display Modules**
- **display_galaxy** - 3D galaxy animation (1000 stars)
- **display_screens** - All LVGL screen implementations
- **test_mode_screen** - Test mode UI

### **main.cpp Responsibilities**
- Hardware initialization (LVGL, display, touch, I2C)
- Module initialization and coordination
- Main event loop (thin, delegates to managers)
- Callbacks (thin adapters, forward to managers)

**Result:** Clean separation of concerns, no business logic in main.cpp

---

## 🎙️ **Voice Command Flow Specification**

### **State Machine (Oct 30, 2025)**

#### **States:**
```cpp
STATE_IDLE              // Waiting for input (default)
STATE_RECORDING         // Recording audio
STATE_UPLOADING         // Sending to server
STATE_DOWNLOADING       // Getting TTS response
STATE_PLAYING           // Playing SAY response (normal)
STATE_PLAYING_ASK       // Playing ASK response (yellow, auto-record after)
STATE_PLAYING_CONFIRM   // Playing CONFIRM response (purple, wait for button)
STATE_QUEUED            // Offline mode with pending recordings
STATE_SETTINGS          // Settings menu
STATE_SLEEP             // Low power sleep
```

---

### **Multi-Turn Conversation Flow:**

#### **Simple SAY Response:**
```
IDLE → RECORDING → UPLOADING → DOWNLOADING → PLAYING (green) → IDLE
```

#### **ASK Flow (Multi-Turn Question):**
```
IDLE → RECORDING → UPLOADING
  ↓ Server: ASK type response
DOWNLOADING → PLAYING_ASK (yellow, mic icon)
  ↓ Playback finishes → AUTO-RECORD next answer
RECORDING → UPLOADING
  ↓ Server: Could be SAY/ASK/CONFIRM
DOWNLOADING → PLAYING_* (continues conversation)
```

#### **CONFIRM Flow (Command Execution):**
```
IDLE → RECORDING → UPLOADING
  ↓ Server: CONFIRM type response
DOWNLOADING → PLAYING_CONFIRM (purple, mic + checkmark icons)
  ↓ Playback finishes → WAIT FOR BUTTON
  ├─ TOP BUTTON (click): Execute command via /execute endpoint
  │    ↓ Download "success" message
  │    └─ Play → IDLE
  └─ BOTTOM BUTTON (click): Abort → IDLE
```

#### **Conversation Continuation:**
Device sends `conversation_id` with each upload to continue the conversation:
```
Upload 1: No conversation_id → Server creates new conversation
         ← Server returns conversation_id
Upload 2: Include conversation_id → Server continues conversation
Upload 3: Include conversation_id → Server maintains context
```

---

### **Correction Flow (During Playback):**

**User wants to correct AI response:**

```
PLAYING (AI speaking)
  ↓ Hold TOP button during playback
  ├─ Auto-decline current response
  ├─ Stop playback
  └─ Start RECORDING (user's correction)
  ↓ Release TOP button
UPLOADING (correction)
  ↓ Upload success
DOWNLOADING
  ↓ Download success
PLAYING (new corrected response)
```

**If user cancels during correction recording:**
```
PLAYING → Hold TOP → RECORDING (correction)
  ↓ Press BOTTOM (cancel)
  ├─ Original playback auto-declined
  ├─ Correction recording discarded
  └─ Continue with correction upload anyway
UPLOADING → DOWNLOADING → PLAYING
```

---

### **Queue Flow (Offline/Upload Failed):**

#### **Entering Queue:**
- Upload fails (no WiFi, server down)
- WiFi disconnects during recording
- Recording in offline mode

#### **Queue Storage:**
```
/sd/recordings/
  ├── queue/              # Unprocessed recordings
  │   ├── 20250120_101530.wav
  │   ├── 20250120_101645.wav
  │   └── ...
  └── processed/          # Successfully processed
      ├── 20250120_101530.wav
      ├── 20250120_101530.txt  # Server response text
      ├── 20250120_101645.wav
      ├── 20250120_101645.txt
      └── ...
```

**Space Management:** Delete oldest processed when < 100-200MB free

#### **Queue Screen 1 - List View:**
- Shows all recordings in `/queue/` folder
- Click recording → Queue Screen 2 (detail view)
- **TOP button (triple-click):** Settings
- **TOP button (hold):** Record new (adds to queue)
- **BOTTOM button:** Ask "Clear queue?" → Confirm → Delete all → IDLE
- **Power button (hold 1s+):** Lock/Sleep → Wake → Return to Queue

#### **Queue Screen 2 - Detail View:**
- **Play button:** Play selected recording
- **Remove button:** Delete from queue → Return to Screen 1

#### **Connection Restored in Queue:**
```
QUEUED (WiFi reconnects)
  ↓ Display "Ready to process" message
  ↓ TOP button becomes active
  ↓ User clicks TOP button
  ↓ Play oldest recording
  ↓ Show "Process this recording?" confirmation
  ├─ TOP (confirm): Upload → Download → Play → Delete from queue → QUEUED (repeat)
  └─ BOTTOM (decline): Delete from queue → QUEUED (repeat)
  ↓ Queue empty
IDLE
```

---

### **Button Behavior by State:**

#### **IDLE:**
- **TOP button:**
  - Single click: (not used)
  - **Triple-click:** Enter SETTINGS
  - **Hold (>500ms):** Start RECORDING
- **BOTTOM button:**
  - Single click: (not used)
  - **Hold (>2s):** Enter SLEEP

#### **RECORDING:**
- **TOP button:** Held down (recording in progress)
  - **Release:** Stop recording → UPLOADING
- **BOTTOM button:**
  - **Click:** Cancel recording → Return to origin state
    - From IDLE → IDLE
    - From PLAYBACK → Continue to UPLOADING (correction still processes!)
    - From QUEUE → QUEUED

#### **UPLOADING:**
- **TOP button:** (not active)
- **BOTTOM button:**
  - **Click:** Cancel → IDLE (or QUEUED if queue not empty)

#### **DOWNLOADING:**
- **TOP button:** (not active)
- **BOTTOM button:**
  - **Click:** Cancel → IDLE (or QUEUED if queue not empty)

#### **PLAYING (SAY mode - green):**
- **TOP button:**
  - **Hold:** Start RECORDING (correction/follow-up)
- **BOTTOM button:**
  - **Click:** Cancel playback → IDLE/QUEUED

#### **PLAYING_ASK (ASK mode - yellow):**
- **Playback finishes:** AUTO-RECORD next answer (no button press needed)
- **TOP button:**
  - **Hold:** Start RECORDING manually (during playback)
- **BOTTOM button:**
  - **Click:** Cancel/abort question → IDLE/QUEUED

#### **PLAYING_CONFIRM (CONFIRM mode - purple):**
- **Playback finishes:** WAIT FOR BUTTON PRESS
- **TOP button:**
  - **Click:** Confirm command → POST /execute → Download response → Play → IDLE
  - **Hold:** Start RECORDING (correction - changes parameters)
- **BOTTOM button:**
  - **Click:** Decline/abort command → IDLE/QUEUED

#### **QUEUED (Screen 1 - List):**
- **TOP button:**
  - **Triple-click:** Enter SETTINGS
  - **Hold:** Start RECORDING (adds to queue)
  - **Click (when connected):** Process oldest recording
- **BOTTOM button:**
  - **Click:** Ask "Clear queue?" → Confirm → Delete all → IDLE

#### **QUEUED (Screen 2 - Detail):**
- **Touch:** Play/Remove buttons

#### **SETTINGS:**
- **TOP button:** Triple-click (from IDLE/QUEUE only)
- **Red X button:** Close → Return to previous state

#### **SLEEP:**
- **Any button:** Wake → Return to previous state (IDLE or QUEUED)
- **Shake detection:** Wake → Return to previous state

---

### **Display Screens:**

```cpp
DISPLAY_STATE_IDLE              // ✅ Galaxy + status bar + buttons
DISPLAY_STATE_RECORDING         // ✅ Recording indicator (context-aware)
DISPLAY_STATE_UPLOADING         // ✅ Progress bar + "Uploading..."
DISPLAY_STATE_DOWNLOADING       // ✅ Progress bar + "Downloading..."
DISPLAY_STATE_PLAYING           // ✅ SAY mode (green text, speaker icon)
DISPLAY_STATE_PLAYING_ASK       // ✅ ASK mode (yellow text, mic icon only)
DISPLAY_STATE_PLAYING_CONFIRM   // ✅ CONFIRM mode (purple text, mic + checkmark icons)
DISPLAY_STATE_QUEUE_LIST        // ✅ List of recordings with playback
DISPLAY_STATE_SETTINGS          // ✅ Brightness/Volume/Mic sliders
DISPLAY_STATE_SLEEP             // ✅ Large clock + shake to wake
```

**Playing Screen Modes:**
- **SAY (Green):** Standard response, normal playback
- **ASK (Yellow):** Question mode, shows mic icon, auto-records after playback
- **CONFIRM (Purple):** Confirmation mode, shows mic + checkmark, waits for button press

---

### **Implementation Phases:**

#### **Phase 1: State Machine Cleanup** ✅ COMPLETE
- ✅ Removed `STATE_PROCESSING` from enum
- ✅ Updated `state_machine_get_name()` 
- ✅ Fixed recording → uploading transition
- ✅ Updated `state_machine_can_record()` to allow PLAYING (corrections)
- ✅ Updated state change callback to handle all states

#### **Phase 2: Upload Flow** ✅ COMPLETE
- ✅ Implemented `UPLOADING` state logic in main loop
- ✅ Uses existing `transcribeAudio()` from https_client
- ✅ Error handling → Queue on failure (stub for Phase 4)
- ✅ Success → transition to `DOWNLOADING`
- ✅ Stores conversation ID in state machine
- ✅ Cancel button during UPLOADING → IDLE (or QUEUED)
- 🚧 Display upload progress screen (placeholder active)

#### **Phase 3: Download & Playback**
- Wire up existing `downloadAudio()` function
- Implement `DOWNLOADING` state
- Create `PLAYING` screen with confirmation UI
- 30-second timeout logic

#### **Phase 4: Queue Manager**
- Create `queue_manager.cpp`
  - `queue_add_recording()`
  - `queue_get_list()`
  - `queue_get_oldest()`
  - `queue_delete()`
  - `queue_clear_all()`
  - `queue_get_count()`
- File management (queue/ vs processed/ folders)
- Space monitoring and cleanup

#### **Phase 5: Queue UI Screens**
- Queue Screen 1: List view with touch
- Queue Screen 2: Detail view (play/remove)
- Connection restored UI ("Ready to process")

#### **Phase 6: Correction Flow**
- Recording during playback
- Auto-decline previous response
- Context-aware recording cancellation

---

### Known Issues / TODO
- ✅ **HTTPS client** - Raw TLS implementation complete (Oct 22, 2025)
- ✅ **Upload flow** - Record → Upload with queue/retry (Oct 22, 2025)
- 🚧 **Download & Playback** - TTS audio download working, playback integration pending
- 🚧 **Display screens** - DOWNLOADING/PLAYING screens (placeholders exist)
- ✅ **Security/Authentication** - HMAC-SHA256 with device ID + timestamp (Oct 22, 2025)
- ✅ **Queue management** - Offline recording queue with backoff retry (Oct 22, 2025)

---

## 🚀 Next Steps

### Phase 3: TTS Download & Playback
1. ✅ **Upload Working** - 38.2 KB/s, queue system, cancel button (Oct 22, 2025)
2. 🚧 **Download Audio** - Implement TTS file download from server
3. 🚧 **Playback** - Play TTS response through speaker
4. 🚧 **Download Screen** - Progress indicator during download

### Security & Performance
1. 📋 **TLS Certificate Validation** - Replace `setInsecure()` with `setCACert(rootCACertificate)`
2. 🚀 **Hardware Crypto** - Re-enable ESP32 crypto accelerator (currently disabled for stability)
3. 🔒 **HMAC Authentication** - Already implemented, using device secret

### Long Term (After Phase 3)
- OTA updates
- Battery optimization / sleep modes
- Touch gestures
- Settings persistence
- Multi-language TTS support
- Queue management UI

---

**Build Time:** 15-30 seconds (incremental) ⚡  
**Session 1:** Oct 19, 2025 11:00 - Oct 20, 2025 02:00 (15 hours)  
**Session 2:** Oct 20, 2025 02:16 - 03:00 (0.75 hours)  
**Session 3:** Oct 20, 2025 21:51 - Oct 21, 2025 23:53 (26.03 hours)  
**Total Hours:** 41.78 hours

**Latest Update (Oct 30, 2025 01:32 UTC):**
- ✅ **Multi-Turn Conversations** - ASK/CONFIRM state flow fully implemented
  - **SAY mode** (green): Normal response playback
  - **ASK mode** (yellow): Auto-records answer after playback
  - **CONFIRM mode** (purple): Waits for button press, executes command via /execute
- ✅ **conversation_id handling** - Device sends as multipart form field, server receives and continues conversations
- ✅ **/execute endpoint** - POST to /execute with command + conversation_id, plays success/error message
- ✅ **Visual indicators** - Color-coded screens (green/yellow/purple), mic icon for ASK, mic+checkmark for CONFIRM
- ✅ **Button controls** - TOP=Confirm (in CONFIRM), BOTTOM=Abort (all states), HOLD=Record (ASK/CONFIRM)
- ✅ **Screen lifecycle fix** - Playing screen text updates after display manager creates screen (prevents null crash)
- ✅ **Excessive logging fixed** - Static flag prevents "awaiting button press" spam
- ✅ **HTTPS Client Replaced** - Raw TLS implementation, ~48KB internal DRAM
- ✅ **Upload flow working** with queue/retry system
- ✅ **TTS download to PSRAM** buffer implemented
- ✅ **Playback timer working** - Shows current/total seconds (e.g., "2 / 5 Seconds")
- ✅ **60-second auto-upload** - Recording automatically uploads after 60 seconds
- ✅ **LVGL memory leak FIXED** - Transient screens deleted, memory logging added
- ✅ **LVGL buffers optimized** - 1/4 screen (160 KB) in PSRAM for best performance
- ✅ **Queue screen implemented** - Brown themed screen for managing queued recordings
- 🚧 Hardware crypto disabled for PSRAM compatibility

**Known Issues (Oct 27, 2025 21:24 UTC):**

**FIXED (Oct 27, 2025 21:30 UTC):**
1. ✅ **Button Timing Issues:** Test mode entry reduced to >1s hold (was 2s), triple-click window increased to 1000ms (was 600ms)
2. ✅ **Queue Screen Status Bar:** Made queue screen persistent (not transient) so status bar maintains correct values
3. ✅ **Galaxy Animation Behind Queue Screen:** Now checks display state (DISPLAY_STATE_IDLE) not just app state
4. ✅ **Wake from Sleep Behavior:** Removed manual display_manager_set_state call, callback now handles queue check properly
5. ✅ **Queue System Overhaul:** Removed auto-retry/backoff system - uploads now manual only via button press

**COMPLETED (Oct 27, 2025 22:52 UTC):**

6. ✅ **Queue Playback Progress Bar:** Thin brown bar below buttons, shows 0-100% progress when playing from queue
7. ✅ **Queue Screen Complete Redesign:**
   - **List UI:** Scrollable list with "Select All Recordings" at top + indexed recordings with full timestamp
   - **Brown Pointer:** ► symbol for selection, starts at row 2 (oldest)
   - **Navigation:** Up/Down arrows with wrapping (All ↔ newest)
   - **Touch Buttons:** ↑ Up, ▶/⏹ Play/Stop, ↓ Down (bottom center)
   - **Physical Buttons:** Top=Upload, Bottom=Delete (with visual indicators on RIGHT side)
   - **Selection Logic:** Play/Upload/Delete use selected item or "All"
   - **Data Extraction:** Full timestamp from filename, duration from WAV header or file size estimate

**COMPLETED (Oct 27, 2025 23:54 UTC):**

8. ✅ **Queue Screen Status Bar:** Exact copy of IDLE screen (battery bar + tip, 4 WiFi bars bottom-aligned)
9. ✅ **Recording Display Format:** Now shows `Index: DD.MM.YYYY/HH:MM:SS/Duration` (e.g., "1: 27.10.2025/23:33:12/4.0s")
10. ✅ **Duration Calculation Fixed:** Falls back to file size estimate if WAV header read fails
11. ✅ **Play/Stop Button Colors:** Play=Green (▶), Stop=Red (⏹) with proper color toggle
12. ✅ **Playback Logging:** Detailed logging for debugging (file selection, progress bar, error causes)
13. ✅ **Swipe Gestures Disabled:** No accidental screen transitions in queue list
14. ✅ **Font Size Increased:** 14pt → 16pt for better readability

**REMAINING ENHANCEMENTS (Lower Priority):**
15. **Playback Audio Output:** Investigate why audio_play_file() fails (file path issue?)
16. **Sequential "All" Operations:** When "All" selected, implement auto-advance for upload/playback
17. **Pointer Tracking:** Move pointer to currently playing file during "All" playback
18. **Single File Delete:** Implement deletion of individual recordings (not just "All")

---

**Bug Fixes:**
1. **Recording >60s now auto-uploads** instead of returning to IDLE
2. **Settings accessible only from IDLE/QUEUED** (prevents freeze during operations)
3. **Playing screen visibility fixed** (was using wrong function to hide)
4. **Playback state cleanup** (prevents lingering references after playback ends)
5. **CRITICAL: Display memory leak fixed** (Oct 27, 2025) - Screens now properly deleted when transitioning states
6. **Screen transition flash fixed** (Oct 27, 2025) - New screen renders BEFORE old screen deletes (no gray flash)
7. **Boot screen persistence fixed** (Oct 27, 2025) - Boot screen now properly deleted after boot completes (was marked persistent, lingering on screen)
8. **Volume feedback sound fixed** (Oct 27, 2025) - Corrected audio asset filename
9. **Queue screen status bar fixed** (Oct 27, 2025) - Battery/WiFi now update in heartbeat loop
10. **Playing screen text centering** (Oct 27, 2025) - Text now centered both horizontally AND vertically
   - Transient screens (UPLOADING, DOWNLOADING, PLAYING, SETTINGS) deleted after use
   - Persistent screens (IDLE, RECORDING, SLEEP) kept for performance
   - Memory logging added to track internal RAM usage
   - Fixes: Crash after 1 full cycle, triple-click freeze, spinner creation failures
11. **Boot screen persistence bug** (Oct 31, 2025) - Boot screen now clears properly on first boot
   - **Problem:** Boot screen remained visible until sleep timeout, even though logs showed transition to IDLE
   - **Root cause:** LVGL screen transitions weren't forcing a complete display refresh on first boot
   - **Solution:** Added forced sleep/wake cycle after boot transition to ensure display refresh
   - Screen briefly enters SLEEP state (100ms) then immediately wakes to IDLE with proper brightness
   - Ensures all LVGL objects are properly rendered and boot screen is completely cleared
12. **Audio response truncation** (Oct 31, 2025) - TTS playback now supports longer responses
   - **Problem:** Server sent 575 KB (17s) but only 256 KB (8s) played - audio cut short
   - **Root cause:** `MAX_REPLY_WAV` buffer limited to 256 KB, truncating longer server responses
   - **Solution:** Increased buffer from 256 KB → 768 KB (~24 seconds @ 16kHz mono)
   - PSRAM buffer now handles full TTS responses without truncation
   - Duration calculation uses actual data size, not truncated buffer size
13. **Smart TTS buffer management** (Oct 31, 2025) - Automatic SD card fallback for large responses
   - **PSRAM buffer:** Increased to 2 MB (~64 seconds @ 16kHz mono)
   - **Automatic fallback:** Files >2 MB stream directly to SD card during download
   - **Transparent playback:** Device automatically plays from PSRAM or SD card
   - **Memory efficiency:** Large responses don't consume PSRAM, preserve memory for UI
   - **Console logging:** Shows source (PSRAM/SD) and file size for debugging
   - **SD card path:** `/sdcard/tts/response_<timestamp>.wav` for large files
14. **Booking confirmation text not displaying** (Nov 3, 2025) - Text box empty after /execute command
   - **Problem:** Booking confirmations showed empty text box, regular SAY responses worked fine
   - **Root cause #1:** `state_machine_reset_conversation()` called immediately, clearing text before display
   - **Root cause #2:** Blocking `downloadAudio()` prevented loop from resetting static flag
   - **Solution:** 
     - Added `pending_conversation_reset` flag to defer reset until AFTER playback
     - Replaced static `playing_text_updated` with versioned flags (`g_playing_text_ver`, `g_playing_text_applied`)
     - Set text immediately in `screen_playing_create()` to cover blocking download timing
   - **Flow:** Execute → Set text + version++ → Download (blocks) → Screen created (text set immediately) → Loop applies if needed
   - Files modified: `main.cpp` (lines 106-110, 288, 1669, 1593, 2040-2074), `display_screens.cpp` (lines 1327-1336)
15. **IMU parallax effect not working** (Nov 3, 2025) - Stars didn't move when device tilted
   - **Problem:** Offset calculated correctly but stars appeared stationary, directions were wrong
   - **Root cause:** Each star had depth-scaled parallax (far stars = less movement), axes were swapped
   - **Old behavior:** `parallax = offset * depth_factor` (different per star, subtle/invisible)
   - **New behavior:** `parallax = offset` (same for ALL stars, global panning effect)
   - **Direction fix:** Swapped accelerometer axes - accel_y was controlling X, accel_x was controlling Y
     - Correct mapping: `tilt_x = -accel_y` (horizontal shift), `tilt_y = accel_x` (vertical shift)
   - **Result:** When device tilts right, ALL stars shift left; tilt down, stars shift down
   - **Max offset:** ±60 pixels (configurable), smoothed with 0.20 damping factor
   - Files modified: `display_galaxy.cpp` lines 120-133, 501-507
16. **SAY screen linger after playback** (Nov 3, 2025) - Response text remains visible for 10 seconds
   - **Feature:** SAY screen now remains visible for 10 seconds after playback finishes (instead of immediately transitioning)
   - **Purpose:** Allows user to read the response text without rushing
   - **User control:** 
     - Press bottom button to cancel and return to IDLE immediately
     - Start recording (hold top button) to cancel and start new recording
   - **Behavior:** Timer auto-resets if user leaves STATE_PLAYING or interacts with device
   - Files modified: `main.cpp` lines 112-113, 369-378, 468-484, 1874-1926
17. **Galaxy animation optimizations** (Nov 4, 2025) - Reduced parallax, adjusted position, lifecycle changes
   - **Parallax adjustment:** Reduced max offset from 60px to 20px (proportional to tilt, more subtle effect)
   - **Visual centering:** Galaxy now renders 10px higher on screen for better visual balance
   - **Lifecycle change:** Animation now created only when entering IDLE, destroyed on any state transition
     - **Purpose:** Frees resources for other screen animations, reduces memory footprint
     - **Benefit:** IDLE screen fully recreated each time (fresh state, no lingering data)
     - **Persistent screens:** RECORDING, SLEEP, QUEUE (frequently reused)
     - **Transient screens:** BOOT, IDLE, UPLOADING, DOWNLOADING, PLAYING, SETTINGS (recreated as needed)
   - **Code cleanup:** Removed unused mode system (RECORDING/UPLOADING/DOWNLOADING/PLAYBACK modes)
     - Removed functions: `galaxy_set_mode()`, `galaxy_get_mode()`, `galaxy_set_audio_amplitude()`
     - Removed functions: `galaxy_add_star()`, `galaxy_get_star_count()`, `galaxy_clear_screen()`
     - Simplified galaxy state struct (removed mode/transition/audio fields)
     - **Result:** ~200 lines of code removed, cleaner API, dedicated IDLE-only animation
   - Files modified: `display_galaxy.h` (simplified API), `display_galaxy.cpp` (removed unused code), `display_manager.cpp` lines 427-437
18. **Recording ring animation** (Nov 4, 2025) - Audio visualizer for RECORDING screen
   - **Feature:** 50px wide ring of orbiting stars around mic icon during recording
   - **Visual:** 200 stars orbiting freely within ring bounds (inner 70px, outer 120px radius)
   - **Audio-reactive visualizer:** Stars expand orbits dramatically based on mic input (0-200px expansion)
     - Added `audio_get_recording_level()` - Returns 0.0-1.0 based on average amplitude
     - Calculates RMS from stereo samples during `audio_update_recording()`
     - Fast smoothing (0.7 factor) for responsive visualizer effect
     - Normalized with 8192 threshold for high sensitivity
     - Updated every frame for real-time visual response
     - **Effect:** Ring visibly pulses and expands 4x more than original design
   - **Implementation:** Direct pixel rendering (same technique as galaxy)
   - **Lifecycle:** Created when entering RECORDING, destroyed on state transition
   - **Performance:** ~400 pixels/frame, 30 FPS, minimal overhead
   - Files created: `display_recording_ring.h`, `display_recording_ring.cpp`
   - Files modified: `display_screens.cpp` (integration), `main.cpp` (rendering + audio link), `audio_manager.cpp/h` (level getter)
19. **UI/UX improvements** (Nov 4, 2025) - Polish and bug fixes
   - **Debug logging cleanup:** Removed all galaxy and recording ring debug output (cleaner serial logs)
   - **Audio playback fix:** Stop audio immediately when canceling SAY screen (was continuing in background)
     - Added 10ms delay + LVGL refresh before state transition to prevent device freeze
   - **Cancel during network ops:** Bottom button now cancels UPLOADING/DOWNLOADING states
     - Sets `g_upload_cancel` flag (checked in upload/download loops and after completion)
     - **Fixed:** Flag now properly reset at start of each operation (was staying set between ops)
     - Immediately aborts operation and returns to IDLE
     - Incomplete downloads are deleted from SD card
     - Upload handler checks flag after `transcribeAudio()` returns
     - Download handler checks flag after `downloadAudio()` returns
   - **Spinner improvements:** UPLOADING/DOWNLOADING spinners now perfectly centered mid-screen
     - **Fixed orphaned spinners:** Create on `parent` not `lv_scr_act()` for proper cleanup
     - Spinners now deleted with their parent screen (no more ghost spinners on recording screen)
     - Position: Absolute center using `LV_ALIGN_CENTER` relative to parent
     - Size: 120px × 120px (large and prominent)
     - Arc width: 10px (thicker, easier to see)
     - **Fixed:** Removed dynamic repositioning in update functions that was moving spinners to text
     - Spinners now stay centered throughout upload/download process
   - **Volume fix:** Removed aggressive normalization that made playback too loud
     - Old formula: `50 + (volume / 2)` made 37% → 68% (too loud!)
     - New formula: Direct mapping (37% → 37%)
     - User volume settings now properly respected after reboot
   - Files modified: `display_galaxy.cpp`, `display_recording_ring.cpp`, `audio_manager.cpp`, `main.cpp`, `https_client.cpp`, `display_screens.cpp`
20. **PSRAM management improvements** (Nov 5, 2025) - Better memory cleanup
   - **PSRAM threshold lowered:** 4MB → 3MB for recording queue
     - Allows recording with ~3.9 MB free PSRAM instead of requiring 4+ MB
     - More flexible for low-memory situations
   - **TTS buffer cleanup:** Automatic PSRAM recovery after playback
     - Added `freeTtsBuffers()` function to free TTS audio buffers
     - Called when playback completes (SAY/ASK/CONFIRM states)
     - Called when user cancels playback (all PLAYING states)
     - Also deletes large TTS files from SD card when present
     - Recovers up to 2MB PSRAM per playback completion
   - **Result:** Better PSRAM availability for new recordings
   - Files modified: `upload_queue.h` (threshold), `https_client.h/cpp` (cleanup function), `main.cpp` (cleanup calls)
21. **Audio recording fix** (Nov 5, 2025) - Restored proper recording quality
   - **Problem:** 10s recording became 5s with garbled audio after adding visualizer
   - **Root cause:** Audio level calculation in I2S read loop was blocking samples
   - **Solution:** Disabled audio visualizer for recording ring animation
     - Commented out audio level calculation in `audio_manager.cpp`
     - Commented out recording ring render and audio feed in `main.cpp`
     - Recording quality and duration now back to normal
   - **Trade-off:** Recording ring stars no longer react to audio (static animation only)
   - Files modified: `audio_manager.cpp`, `main.cpp`
22. **Queue screen fixes** (Nov 5-6, 2025) - Visual polish and button reliability
   - **Visual improvements:**
     - **Mic icon now white** (was green)
     - **Settings icon added** above mic icon on right side
     - **Icon spacing fixed:** Container increased from 50px to 75px height, moved up from y=115 to y=90
     - Icons properly spaced: Settings (2px from top) → Mic (centered) → Upload (2px from bottom)
     - No more overlapping or cut-off icons
   - **Critical freeze fix:** Device no longer freezes when deleting/uploading last queue item
     - **Problem:** After removing last item, state transitioned to IDLE but display stayed on QUEUED screen
     - **Solution:** Added `display_manager_set_state(DISPLAY_STATE_IDLE)` calls when queue becomes empty
     - Applied in 5 locations: delete single, delete all, upload no-TTS, download failure, playback failure
   - **Button reliability fix:** Buttons now respond correctly when queue screen is shown
     - **Problem:** When canceling recording while queue has items, state was IDLE but display was QUEUED
     - Buttons checked app state (IDLE) not display state (QUEUED) → "no action in this state"
     - **Solution:** After canceling recording, if queue has items and origin was IDLE, transition to QUEUED instead
     - Applied in 3 locations: recording cancel, recording start fail, recording stop fail
   - **Queue playback volume fix:** Queue recordings now play at 100% volume (easier to hear)
     - NVS volume setting automatically restored after playback finishes or is stopped
     - Applied in 3 locations: playback complete, stop button, all files played
   - **Auto-upload fix:** Recording from queue screen no longer auto-uploads when WiFi connected
     - Added check: `g_state_before_recording != STATE_QUEUED` to prevent auto-upload
     - Queue recordings now stay in queue as expected
   - **Delete dialog improvements:**
     - Dialog no longer scrollable left/right (added `LV_OBJ_FLAG_SCROLLABLE` clear)
     - Dialog closes BEFORE state transitions (prevents freeze when deleting all files)
     - Device no longer freezes showing confirmation dialog after "Delete All → Yes"
   - **Galaxy animation persistence:** IDLE screen now persistent (never deleted)
     - **Background:** Audio recording ring animation was breaking audio quality (disabled in update 21)
     - To avoid clash, galaxy was deleted when leaving IDLE (causing freeze issues)
     - **Solution:** IDLE screen marked as persistent in display manager
     - Galaxy timer always runs in background (calculating star positions)
     - Rendering only happens during IDLE state (controlled by `main.cpp` line 1610)
     - **Result:** No more screen recreation delays, smoother transitions, prevents freeze bugs
   - **Result:** Queue screen fully functional with proper state management, volume control, and UI behavior
   - Files modified: `screen_queue.cpp` (visual), `main.cpp` (state transitions, volume, dialog), `display_manager.cpp` (IDLE persistence)
23. **Thread-safety fix for LVGL** (Nov 6, 2025) - Comprehensive LVGL thread protection
   - **Problem:** Device still froze at "Creating UI elements..." despite event queue
     - Initial fix (event queue) moved state transition to main loop
     - BUT screen creation itself (`screen_downloading_create()`) still called from HTTP callback context
     - LVGL spinner/label creation was happening on wrong core
   - **Root cause:** LVGL is completely NOT thread-safe
     - WiFi/HTTP runs on core 0, main loop on core 1
     - Any LVGL call from core 0 = crash/freeze
     - Spinner creation (`lv_spinner_create()`) was the freeze point
   - **Comprehensive Solution:** Multi-layer protection
     - **UI Job Dispatcher:** Generic queue for any LVGL work from other tasks
       - `UiJob` struct with function pointer + arg
       - `ui_post(fn, arg)` to queue jobs from any task
       - `g_ui_jobs` queue (16 slots) processed first in `loop()`
     - **UI Task Handle:** Store handle at startup for assertions
       - `g_ui_task = xTaskGetCurrentTaskHandle()` in `setup()`
       - Enables runtime checks that LVGL only called from correct task
     - **Thread Assertions:** Debug macro to crash early on violations
       - `UI_THREAD_ASSERT()` checks current task vs UI task
       - Immediate abort() with error message if mismatch
       - Can be added to all LVGL wrapper functions
     - **Recursive Mutex:** Belt-and-braces protection for LVGL
       - `g_lvgl_mutex` created in `setup()`
       - `lvgl_lock()`/`lvgl_unlock()` around `lv_timer_handler()`
       - Allows safe nested locking if needed temporarily
   - **Implementation:**
     - Job dispatcher processes all queued UI work FIRST in `loop()`
     - HTTP callbacks only post events/jobs, never touch LVGL
     - Screen creation happens entirely on main loop thread
     - Memory integrity checks added before widget creation
   - **LVGL Debug Logging Enabled:**
     - `LV_USE_LOG = 1` - Enable LVGL internal logging
     - `LV_LOG_LEVEL = LV_LOG_LEVEL_WARN` - Show warnings and errors
     - `LV_LOG_PRINTF = 1` - Output logs to Serial via printf
     - `LV_USE_ASSERT_MEM_INTEGRITY = 1` - Check memory integrity after operations
     - `LV_USE_ASSERT_OBJ = 1` - Check object validity and existence
     - **Revealed root cause:** LVGL heap exhausted (580 bytes free, needed for spinner animation)
   - **LVGL Heap Moved to PSRAM (CRITICAL FIX):**
     - **Problem:** LVGL internal heap was only 48KB in DRAM, ran out during screen creation
     - Old: `[LVGL MEM] free=580 bytes` → `lv_anim_start: Out of memory`
     - **Solution:** Use ESP32 heap_caps to allocate LVGL objects from PSRAM
       - `LV_MEM_CUSTOM = 1` - Use custom allocator instead of built-in
       - `LV_MEM_CUSTOM_ALLOC` → `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`
       - `LV_MEM_CUSTOM_FREE` → `heap_caps_free(...)`
       - `LV_MEM_CUSTOM_REALLOC` → `heap_caps_realloc(..., MALLOC_CAP_SPIRAM)`
     - Result: LVGL heap now uses PSRAM (effectively unlimited vs 48KB limit)
     - Startup prints LVGL heap status to verify PSRAM usage
   - **Animation Cleanup Added:**
     - `lv_anim_del(NULL, NULL)` before creating DOWNLOADING screen
     - Prevents animation/timer leaks from previous screens
     - Memory check logged before each screen creation
   - **Next steps to complete:**
     - Add `UI_THREAD_ASSERT()` to all display manager functions
     - Move IMU parallax LVGL calls to `ui_post()`
     - Move status bar updates (WiFi/battery) to `ui_post()`
     - Audit all timers and callbacks for LVGL usage
   - **Heap Corruption Fix - Queue Event Struct:**
     - **Problem:** Device crashed with `CORRUPT HEAP` when destroying `ui_event_t`
     - **Root cause:** FreeRTOS queue does shallow copy of structs
       - `ui_event_t` contained Arduino String objects (heap-allocated)
       - `xQueueSend()` copied the struct, but NOT the string data
       - Both copies pointed to same memory
       - When structs destructed → double-free → heap corruption
     - **Solution:** Use C string pointers instead of String objects
       - Changed: `String ttsUrl` → `char* ttsUrl`
       - Sender: `evt.ttsUrl = strdup(response.ttsUrl.c_str())` (allocate on heap)
       - Receiver: `free(evt.ttsUrl)` after processing (prevent leaks)
     - **Result:** No more double-free corruption, queue safe to use
   - **Result:** LVGL heap moved to PSRAM, animations cleaned up, heap corruption fixed
   - Files modified: `main.cpp` (dispatcher, mutex, heap monitor, queue fix), `display_screens.cpp` (mem checks), `display_manager.cpp` (anim cleanup), `lv_conf.h` (PSRAM heap, logging/asserts)
24. **JSON Parsing Fix - Escaped Quotes** (Nov 6, 2025) - Fixed text truncation at backslash characters
   - **Problem:** Server messages with escaped quotes were truncated on display
     - Example: `"I'm not sure what you mean by \"the result.\" Could you..."` 
     - Displayed as: `I'm not sure what you mean by \`
     - Text cut off at first `\"` (escaped quote)
   - **Root cause:** Naive JSON parsing in `https_client.cpp`
     - Old code: `int q2 = bodyPart.indexOf("\"", q1 + 1);` - finds NEXT quote
     - Doesn't distinguish between escaped `\"` and actual closing quote
     - Server sends: `"message": "text with \"quotes\" inside"`
     - Parser stopped at first `\"` instead of actual end
   - **Solution:** Proper JSON string parsing with escape handling
     - Scan character-by-character, skip escaped sequences
     - When `\` encountered, skip next character (it's escaped)
     - Only stop at unescaped `"` (actual string end)
     - Unescape extracted string: `\"` → `"`, `\\` → `\`, `\n` → newline, `\t` → tab
   - **Implementation:**
     ```cpp
     // Scan for closing quote, skipping escaped chars
     for (int i = q1 + 1; i < bodyPart.length(); i++) {
       if (bodyPart[i] == '\\') {
         i++;  // Skip next character (it's escaped)
         continue;
       }
       if (bodyPart[i] == '"') {
         q2 = i;  // Found actual closing quote
         break;
       }
     }
     ```
   - **Applied to:**
     - `/transcribe` endpoint: message + command fields
     - `/execute` endpoint: message field
   - **Result:** Full text now displays correctly with quotes, no truncation
   - Files modified: `https_client.cpp` (JSON parsing for message/command fields)
25. **Production Log Cleanup** (Nov 6, 2025) - Cleaned up verbose debug logs for production readiness
   - **Removed spam logs:**
     - `audio_update_playback() called 31 times/sec` (repeated every second during playback)
     - HTTP signature HMAC hex string (security: don't expose in logs)
     - First bytes hex dump from downloads (unnecessary)
     - Verbose screen creation steps (container, allocating, registering, etc.)
     - Duplicate label text logs (before/after set, on-create)
     - Redundant UI event queue messages
   - **Simplified logs:**
     - Memory: `💾 [before_transition] 142 KB / 7 MB PSRAM` (removed internal heap details, added icon)
     - Text display: `📝 Text: I'm designed to assist with phone repa...` (truncate at 60 chars)
     - HTTP: `🔐 Device: AA:BB:CC:DD:EE:FF` (removed timestamp and signature)
     - Screen creation: `📥 DOWNLOADING screen...✓` (single line instead of 10)
     - Queue: `📦 Queue: 3 items, 6500 KB PSRAM free` (compact init), `📦 Queueing: 125 KB` (simple size)
   - **Result:** Clean, professional logs focused on important events only
   - Files modified: `audio_manager.cpp`, `display_manager.cpp`, `display_screens.cpp`, `https_client.cpp`, `main.cpp`, `upload_queue.cpp`
26. **Production Refinements** (Nov 7, 2025) - Final polish for production readiness
   - **Transcription logging:** Added transcription output to logs after upload success
     - Shows what the user said: `🎯 Transcription: respond by putting my transcription in quotes`
     - Helps with debugging voice recognition issues
   - **Memory log improvements:**
     - All memory values now in KB (was: KB/MB mixed)
     - Added LVGL heap monitoring: `💾 [tag] Internal: 142 KB, PSRAM: 7000 KB, LVGL: 256 KB`
     - Shows LVGL heap when available, or notes "(LVGL in PSRAM)" when using custom allocator
   - **Recording screen spinner:**
     - Added red spinner next to recording duration (40px, 20px margin)
     - Matches PLAYING screen spinner style (same size/position relative to text)
     - Dynamically repositions when duration text changes (e.g., "Duration: 5/60" vs "Duration: 10/60")
     - Visual feedback that recording is active
   - Files modified: `https_client.cpp`, `display_manager.cpp`, `display_screens.cpp`
27. **Watchdog Timer for Auto-Reboot** (Nov 7, 2025) - Added crash recovery system
   - **Purpose:** Automatically reboot device if system hangs or crashes
   - **Configuration:**
     - 60 second timeout before triggering panic reboot
     - Monitors main loop task (loop task on core 1)
     - Panic mode enabled: triggers full reboot with backtrace
   - **Watchdog reset locations (prevent false triggers):**
     - **Heartbeat:** Every 10 seconds during status log (`💓 State: ...`)
     - **HTTP Upload:** Every 16 KB uploaded during voice recording transmission
     - **HTTP Download:** Every 16 KB downloaded during TTS audio fetch
     - **Recording:** Every second during voice recording (0-60 seconds)
   - **Benefits:**
     - Recovers from WiFi stack hangs
     - Recovers from LVGL deadlocks
     - Recovers from audio driver hangs
     - Prevents permanent device lockup
   - **Logging:**
     - Init: `🐶 Initializing watchdog timer...`
     - Success: `✅ Watchdog enabled: 60s timeout, auto-reboot on hang`
   - Files modified: `main.cpp`, `https_client.cpp`, `display_manager.cpp`
28. **Smooth Brightness Transitions** (Nov 7, 2025) - Added visual polish to state changes
   - **Implementation:** 3-phase brightness dimming during screen transitions
   - **Effect:** Old screen dims → new screen loads → new screen brightens
   - **Duration:** 150ms total (dim phase only, instant restore)
   - **Applied to all state transitions:**
     - BOOT → IDLE (startup)
     - IDLE ↔ RECORDING (button press)
     - RECORDING → UPLOADING → DOWNLOADING → PLAYING (voice flow)
     - PLAYING → IDLE (completion)
     - IDLE → SLEEP (timeout)
     - IDLE → SETTINGS (triple-click)
     - QUEUED → UPLOADING (queue processing)
     - ERROR states (graceful error display)
   - **Technical:**
     ```cpp
     // Save original brightness before transition
     static int g_brightness_before_transition = 0;
     
     // Phase 1: Dim old screen
     g_brightness_before_transition = settings_manager_get_brightness();
     settings_manager_set_brightness(g_brightness_before_transition / 3, false);
     delay(150);  // Wait while old screen is dimmed
     
     // Phase 2: Create and render new screen (while dimmed)
     screen_xxx_create(...);
     lv_timer_handler();  // LVGL renders new screen
     
     // Phase 3: Restore brightness on new screen
     settings_manager_set_brightness(g_brightness_before_transition, false);
     ```
   - **Benefits:**
     - Professional, polished feel
     - Eliminates jarring screen jumps
     - Masks screen construction delays
     - No rendering artifacts (unlike opacity fade)
     - Uses hardware backlight control
     - Proper old→new transition (not premature dim/restore)
   - **Note:** Reverted from opacity fade (caused gray screens) to brightness dimming
   - Files modified: `display_manager.cpp`
29. **Compilation Fixes** (Nov 7, 2025) - Resolved build errors and warnings
   - **Fixed errors:**
     - Removed leftover `g_playing_text_ver` variables (undefined reference)
     - Updated watchdog API to legacy version for Arduino ESP32 core compatibility
       - Changed from `esp_task_wdt_config_t` struct to `esp_task_wdt_init(60, true)`
   - **Fixed warnings:**
     - Removed duplicate `LV_USE_LOG` definition in `platformio.ini`
     - Now controlled only by `lv_conf.h` (set to 1 for warnings/errors)
     - Eliminates "redefined" warnings during compilation
   - **Result:** Clean compilation with no errors or warnings
   - Files modified: `display_screens.cpp`, `main.cpp`, `platformio.ini`, `lv_conf.h`
30. **Improved Conversation Logging** (Nov 7, 2025) - Cleaner, more informative logs
   - **User transcription logging:**
     - Added `💬 User said: [transcription]` before AI response
     - Shows what user actually said (helps debug voice recognition)
     - Appears right before entering PLAYING state
   - **Consolidated text display logs:**
     - BEFORE: 3 separate logs (Enter PLAYING, Text preview, Display text)
     - AFTER: 2 concise logs (`💬 User said` + `📝 AI response`)
     - Removed duplicate/verbose logging from `display_screens.cpp`
     - Full text shown in single log line (no truncation)
   - **Log flow example:**
     ```
     🎯 Transcription: That's all I've got. Thank you.
     💬 User said: That's all I've got. Thank you.
     📝 AI response: You're welcome! If you have any more questions...
     ```
   - Files modified: `main.cpp`, `display_screens.cpp`
31. **Sleep from Queue State** (Nov 7, 2025) - Auto-sleep now works when queue is visible
   - **Previous behavior:** Could only sleep from IDLE state
   - **New behavior:** Can sleep from IDLE or QUEUED states
   - **Requirement:** Still requires 30s of true inactivity (no touch/button/movement)
   - **Activity detection:**
     - Touch inputs tracked by state machine
     - Button presses tracked by state machine
     - Device movement tracked by IMU shake detection
     - All reset the 30s inactivity timer
   - **Benefits:**
     - Device can sleep when queue screen is showing
     - Saves battery when idle with pending uploads
     - Wake from sleep automatically shows queue (if items remain)
   - **Note:** Still prevents sleep during recording, settings, playback, etc.
   - Files modified: `sleep_manager.cpp`
32. **Recording Screen Optimization** (Nov 7, 2025) - Removed spinner to fix audio quality
   - **Problem:** Red spinner animation caused fuzzy/incomplete audio recordings
   - **Root cause:** Spinner animation consumed CPU/GPU resources during recording
   - **Solution:** Removed red spinner from recording screen entirely
   - **What was removed:**
     - Red 40px spinner next to duration label
     - Dynamic repositioning code on duration updates
   - **What remains:**
     - Duration label: "Duration: X/60"
     - Large red mic icon (center)
     - Cancel/Stop buttons
     - Status bar (battery, WiFi)
   - **Benefits:**
     - Clean, crisp audio recordings
     - Eliminated fuzzy/incomplete recording issue
     - Reduced CPU load during recording
     - More resources available for I2S audio capture
   - **Note:** Other screens (UPLOADING, DOWNLOADING, PLAYING) keep their spinners
   - Files modified: `display_screens.cpp`
33. **Critical Bug Fixes** (Nov 7, 2025) - Fixed brightness transitions and queue behavior
   - **Fixed: Downloading screen gray background**
     - Added explicit `LV_OPA_COVER` opacity to DOWNLOADING screen
     - Ensures solid black background (not transparent gray)
   - **Fixed: Recording from queue goes to UPLOADING**
     - **Problem:** When recording from QUEUED state, recording would auto-upload instead of returning to queue
     - **Root cause:** State handling logic checked IDLE first, QUEUED second
     - **Solution:** Reordered if/else to check QUEUED FIRST (takes priority)
     - Now recordings from queue are added to end of queue (not uploaded)
   - **Fixed: Brightness dims/restores before screen changes**
     - **Problem:** IDLE→RECORDING transition had 150ms dim delay (jittery, unresponsive feel)
     - **Root cause:** Brightness transition applied to ALL state changes
     - **Solution:** Skip brightness transition for IDLE/QUEUED→RECORDING
     - Recording now starts instantly when button pressed (critical for voice UX)
   - **Fixed: Jittery brightness adjustments**
     - **Problem:** 150ms delay in `brightness_dim()` caused visible jitter
     - **Root cause:** Fixed delay doesn't account for screen creation time
     - **Solution:** Removed delay - screen creation time provides natural dim duration
     - Brightness changes now smooth and natural
   - **Benefits:**
     - Queue recordings stay in queue (user frustration eliminated!)
     - Recording starts instantly (no visible delay)
     - Smooth brightness transitions (no jitter)
     - Downloading screen renders correctly (no gray)
   - Files modified: `main.cpp`, `display_manager.cpp`, `display_screens.cpp`


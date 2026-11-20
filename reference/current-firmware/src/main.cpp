// ============================================
// LVGL NATIVE TEST - ESP32-S3 Touch AMOLED
// ============================================
// Full hardware initialization for testing LVGL UI
// Hardware: SH8601 QSPI Display + FT3168 Touch + TCA9554 IO Expander
// Purpose: Develop native LVGL screens separately from main project
// Platform: PlatformIO

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <esp_heap_caps.h>
#include <Arduino_DriveBus_Library.h>
#include <ESP_IOExpander_Library.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>

// Hardware Configuration
#include "hardware_pins.h"

// Serial output uses built-in Serial (USB CDC)

// Display Manager (LVGL screens)
#include "display/display_manager.h"
#include "display/display_galaxy.h"
#include "display/display_recording_ring.h"

// Phase 1 Modules: Input & State
#include "button_manager.h"
#include "sleep_manager.h"
#include "upload_queue.h"
#include "test_mode.h"
#include <FS.h>
#include <SD.h>

// Phase 2 Modules: Sensors & Power
#include "battery_manager.h"
#include "rtc_manager.h"
#include "imu_manager.h"

// Phase 3/4 Modules: WiFi & Storage
#include "wifi_manager.h"
#include "sd_manager.h"
#include "nvs_manager.h"
#include "audio/audio_manager.h"
#include "audio/audio_assets.h"  // SPIFFS sound assets (boot chime, UI clicks)
#include "settings_manager.h"
#include "https_client.h"  // ServerResponse, transcribeAudio(), downloadAudio()
#include "upload_queue.h"  // Queue manager

// Phase 5 Modules: Sleep & Settings
#include "sleep_manager.h"
#include "settings_manager.h"

// ============================================
// IO EXPANDER (Required for Display)
// ============================================
#define _EXAMPLE_CHIP_CLASS(name, ...) ESP_IOExpander_##name(__VA_ARGS__)
#define EXAMPLE_CHIP_CLASS(name, ...) _EXAMPLE_CHIP_CLASS(name, ##__VA_ARGS__)
ESP_IOExpander *expander = NULL;

// ============================================
// HARDWARE OBJECTS
// ============================================
Arduino_DataBus *bus = nullptr;
Arduino_GFX *gfx = nullptr;
std::shared_ptr<Arduino_IIC_DriveBus> IIC_Bus;
Arduino_IIC *touch = nullptr;

// Touch interrupt handler (called by ISR)
void touch_interrupt_handler() {
    if (touch) {
        touch->IIC_Interrupt_Flag = true;
    }
}

// LVGL Display Buffer
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;

// LVGL Display Driver
static lv_disp_drv_t disp_drv;

// Boot complete flag (prevents galaxy rendering during boot screen transition)
static bool g_boot_complete = false;

// ============================================
// LVGL THREAD SAFETY - UI Job Dispatcher
// ============================================
// LVGL is NOT thread-safe. All LVGL calls MUST happen on the UI task (main loop, core 1).
// Other tasks (WiFi/HTTP on core 0, timers, etc.) must post UI jobs to this queue.

// Store UI task handle for assertions
static TaskHandle_t g_ui_task = nullptr;

// Generic UI job function pointer
typedef void (*UiJobFn)(void*);

// UI job structure
struct UiJob {
    UiJobFn fn;
    void* arg;
};

// UI job queue (for cross-task LVGL safety)
static QueueHandle_t g_ui_jobs = nullptr;

// Recursive mutex for LVGL (belt-and-braces protection)
static SemaphoreHandle_t g_lvgl_mutex = nullptr;

// Debug assertion - crash immediately if LVGL called from wrong task
#define UI_THREAD_ASSERT() do { \
    if (g_ui_task && xTaskGetCurrentTaskHandle() != g_ui_task) { \
        Serial.println("❌ LVGL called from non-UI task!"); \
        Serial.flush(); \
        abort(); \
    } \
} while(0)

// Post UI job to queue (thread-safe way to call LVGL from other tasks)
inline void ui_post(UiJobFn fn, void* arg = nullptr) {
    if (g_ui_jobs) {
        UiJob job = {fn, arg};
        xQueueSend(g_ui_jobs, &job, portMAX_DELAY);
    }
}

// LVGL mutex lock/unlock (for extra safety)
inline void lvgl_lock() {
    if (g_lvgl_mutex) xSemaphoreTakeRecursive(g_lvgl_mutex, portMAX_DELAY);
}

inline void lvgl_unlock() {
    if (g_lvgl_mutex) xSemaphoreGiveRecursive(g_lvgl_mutex);
}

// Legacy event queue (to be replaced with ui_post eventually)
typedef enum {
    UI_EVENT_UPLOAD_SUCCESS,
    UI_EVENT_UPLOAD_FAILED,
    UI_EVENT_DOWNLOAD_COMPLETE,
    UI_EVENT_DOWNLOAD_FAILED
} ui_event_type_t;

// CRITICAL: Use pointers, not String objects directly!
// FreeRTOS queue does shallow copy - String objects would be double-freed
typedef struct {
    ui_event_type_t type;
    char* ttsUrl;         // Heap-allocated C string
    char* message;        // Heap-allocated C string
    char* command;        // Heap-allocated C string
    int responseType;
    char* errorMessage;   // Heap-allocated C string
    bool userCancelled;
} ui_event_t;

static QueueHandle_t g_ui_event_queue = nullptr;

// Delete confirmation dialog state
static lv_obj_t* g_delete_msgbox = nullptr;
static String g_delete_pending_filename = "";
static bool g_delete_all_pending = false;
static bool g_queue_delete_confirmation = false;

// Sequential playback state for "Select All"
static bool g_auto_advance_playback = false;
static int g_current_playback_index = 0;  // Which file is currently playing (1-based)

// Recording origin tracking
static app_state_t g_state_before_recording = STATE_IDLE;  // Track where recording was started from

// Server response pending data (shared between upload handler and button callbacks)
static String pending_tts_url = "";  // Store TTS URL for DOWNLOADING state
String pending_response_text = "";  // Store server response text for PLAYING state (non-static for screen access)
static String pending_transcription = "";  // Store user's transcription for logging
static ResponseType pending_response_type = RESPONSE_SAY;  // Store response type (SAY/ASK/CONFIRM)
String g_pending_command = "";  // Store command from CONFIRM response for executeCommand() (non-static for button handler)
static bool pending_conversation_reset = false;  // Flag to defer conversation reset until after playback

// Versioned text update flags (replaces static bool playing_text_updated)
uint32_t g_playing_text_ver = 0;       // Version counter - incremented when text changes (non-static for screen access)
uint32_t g_playing_text_applied = 0;   // Last version that was applied to screen (non-static for screen access)

// SAY screen linger timer (shows response for 10s after playback)
static unsigned long say_screen_linger_start = 0;

// ============================================
// LVGL DISPLAY FLUSH CALLBACK
// ============================================
void lvgl_flush_callback(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);
    
    if (gfx) {
        gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
    }
    
    lv_disp_flush_ready(disp);
}

// ============================================
// LVGL TOUCH READ CALLBACK
// ============================================
void lvgl_touch_callback(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
    if (touch && touch->IIC_Interrupt_Flag) {
        touch->IIC_Interrupt_Flag = false;
        
        int32_t touchX = touch->IIC_Read_Device_Value(touch->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_X);
        int32_t touchY = touch->IIC_Read_Device_Value(touch->Arduino_IIC_Touch::Value_Information::TOUCH_COORDINATE_Y);
        
        data->state = LV_INDEV_STATE_PR;
        data->point.x = touchX;
        data->point.y = touchY;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

// ============================================
// LVGL TICK CALLBACK (for animations)
// ============================================
void lvgl_tick_callback(void *arg) {
    lv_tick_inc(2);  // Match 2ms timer interval (factory ROM setting)
}

// ============================================
// STATE MACHINE CALLBACK
// ============================================
void state_change_callback(app_state_t old_state, app_state_t new_state) {
    // State change already logged by state_machine.cpp, just update display
    
    // Note: Galaxy is automatically destroyed when IDLE screen is deleted
    // (display_manager now deletes IDLE screen on state transition)
    
    // Update display based on new state
    switch (new_state) {
        case STATE_IDLE: {
            // Check if queue has files - if yes, show queue screen instead of IDLE
            if (queue_size() > 0) {
                Serial.printf("📋 Queue has %d files - showing queue screen\n", queue_size());
                display_manager_set_state(DISPLAY_STATE_QUEUED);
                
                // Update queue screen with filenames list
                String filenames = queue_get_filenames_list();
                display_manager_update_queue_filenames(filenames.c_str());
                
                // Update WiFi state for button visibility
                display_manager_update_queue_wifi(isWiFiConnected());
            } else {
                display_manager_set_state(DISPLAY_STATE_IDLE);
            }
            break;
        }
        case STATE_RECORDING:
            display_manager_set_state(DISPLAY_STATE_RECORDING);
            break;
        case STATE_UPLOADING:
            display_manager_set_state(DISPLAY_STATE_UPLOADING);
            break;
        case STATE_DOWNLOADING:
            display_manager_set_state(DISPLAY_STATE_DOWNLOADING);
            break;
        case STATE_PLAYING:
            Serial.println("   📺 Display: Switching to PLAYING screen (SAY mode - green)");
            display_manager_set_state(DISPLAY_STATE_PLAYING);
            break;
        case STATE_PLAYING_ASK:
            Serial.println("   📺 Display: Switching to PLAYING_ASK screen (yellow)");
            display_manager_set_state(DISPLAY_STATE_PLAYING_ASK);
            break;
        case STATE_PLAYING_CONFIRM:
            Serial.println("   📺 Display: Switching to PLAYING_CONFIRM screen (purple)");
            display_manager_set_state(DISPLAY_STATE_PLAYING_CONFIRM);
            break;
        case STATE_QUEUED: {
            display_manager_set_state(DISPLAY_STATE_QUEUED);
            
            // Update queue screen with filenames list
            String filenames = queue_get_filenames_list();
            display_manager_update_queue_filenames(filenames.c_str());
            
            // Update WiFi state for button visibility
            display_manager_update_queue_wifi(isWiFiConnected());
            break;
        }
        case STATE_SETTINGS:
            // Note: Settings handled by button callback
            break;
        case STATE_SLEEP:
            // Note: Sleep handled by sleep_manager
            break;
        default:
            break;
    }
}

// ============================================
// FORWARD DECLARATIONS
// ============================================
void display_event_callback(display_state_t from_state, int event_data);

// ============================================
// BUTTON EVENT CALLBACK
// ============================================
void button_event_callback(button_id_t button, button_event_t event) {
    const char* btn_name = (button == BUTTON_TOP) ? "TOP" : "BOTTOM";
    const char* evt_name = "UNKNOWN";
    
    switch (event) {
        case BUTTON_EVENT_CLICK:        evt_name = "CLICK"; break;
        case BUTTON_EVENT_TRIPLE_CLICK: evt_name = "TRIPLE_CLICK"; break;
        case BUTTON_EVENT_HOLD_RECORD:  evt_name = "HOLD_RECORD"; break;
        case BUTTON_EVENT_HOLD_POWER:   evt_name = "HOLD_POWER"; break;
        case BUTTON_EVENT_RELEASE:      evt_name = "RELEASE"; break;
    }
    
    Serial.printf("🔘 Button: %s %s\n", btn_name, evt_name);
    
    // Update activity timestamp
    state_machine_update_activity();
    
    // Check if sleep manager wants to handle this (wake from sleep)
    if (sleep_manager_handle_button(button, event)) {
        return;  // Sleep manager handled it (device woke up)
    }
    
    // Handle button events based on current state
    app_state_t current_state = state_machine_get();
    
    if (button == BUTTON_TOP) {
        if (event == BUTTON_EVENT_CLICK) {
            // CONFIRM state: Execute the command
            if (current_state == STATE_PLAYING_CONFIRM) {
                Serial.println("   → Top button: Confirming command execution");
                
                // Get conversation data from state machine
                const char* convId = state_machine_get_conversation_id();
                
                // Execute the command (need to store command from pending_response)
                // We'll use a static variable to store the command from server response
                extern String g_pending_command;  // Declared below
                
                if (g_pending_command.length() > 0 && convId && strlen(convId) > 0) {
                    Serial.printf("   📤 Executing command: %s\n", g_pending_command.c_str());
                    
                    // Call executeCommand
                    ServerResponse execResp = executeCommand(convId, g_pending_command.c_str());
                    
                    // Check if server returned audio (success OR failure)
                    if (execResp.ttsUrl.length() > 0) {
                        // Server returned a response - download and play it (even for errors)
                        if (execResp.success) {
                            Serial.println("   ✅ Command executed - downloading response");
                        } else {
                            Serial.println("   ❌ Command failed - downloading error explanation");
                        }
                        
                        pending_tts_url = execResp.ttsUrl;
                        pending_response_text = execResp.message;
                        g_playing_text_ver++;  // Mark text as dirty/updated
                        pending_response_type = RESPONSE_SAY;  // Execute always returns SAY
                        pending_conversation_reset = true;  // Flag to reset after playback
                        
                        Serial.printf("   📝 Success response message: '%s'\n", pending_response_text.c_str());
                        Serial.println("   ℹ️  This message will be displayed on the PLAYING screen");
                        Serial.println("   🔄 Conversation will reset after response plays");
                        
                        state_machine_set(STATE_DOWNLOADING);
                    } else if (execResp.success) {
                        // Success but no audio response - return to idle
                        Serial.println("   ✅ Command executed successfully (no audio)");
                        
                        // Reset conversation (command completed)
                        state_machine_reset_conversation();
                        Serial.println("   🔄 Conversation reset (command completed)");
                        
                        if (queue_size() > 0) {
                            state_machine_set(STATE_QUEUED);
                        } else {
                            state_machine_set(STATE_IDLE);
                        }
                    } else {
                        // Execution failed with no audio - return to idle
                        Serial.println("   ❌ Command execution failed (no audio)");
                        
                        // Reset conversation (start fresh)
                        state_machine_reset_conversation();
                        Serial.println("   🔄 Conversation reset (execution failed)");
                        
                        if (queue_size() > 0) {
                            state_machine_set(STATE_QUEUED);
                        } else {
                            state_machine_set(STATE_IDLE);
                        }
                    }
                    
                    g_pending_command = "";  // Clear after use
                } else {
                    Serial.println("   ⚠️  No command or conversation ID to execute");
                    state_machine_set(STATE_IDLE);
                }
                return;
            }
            
            // Queue screen: Trigger upload
            if (current_state == STATE_QUEUED) {
                Serial.println("   → Top button: Upload selected recording");
                // Trigger upload event (same as touch button)
                display_event_callback(DISPLAY_STATE_QUEUED, QUEUE_EVENT_PROCESS);
                return;
            }
            
            // Other states: No action
            Serial.println("   → Top click (no action in this state)");
        } else if (event == BUTTON_EVENT_TRIPLE_CLICK) {
            // Enter settings (only from IDLE or QUEUED states)
            if (current_state == STATE_IDLE || current_state == STATE_QUEUED) {
                Serial.println("   → Entering SETTINGS mode");
                state_machine_set(STATE_SETTINGS);
                display_manager_set_state(DISPLAY_STATE_SETTINGS);
                
                // Small delay to ensure LVGL is ready
                lv_timer_handler();
                delay(10);
                
                // Load and set current values
                display_manager_set_settings_values(
                    settings_manager_get_brightness(),
                    settings_manager_get_volume(),
                    settings_manager_get_mic_gain()
                );
            } else {
                Serial.printf("   ⚠️  Cannot enter settings from state: %s\n", state_machine_get_name(current_state));
            }
        } else if (event == BUTTON_EVENT_HOLD_RECORD) {
            // Start recording (hold detected)
            // Allow from IDLE, QUEUED, or PLAYING states (for corrections/multi-turn)
            if (current_state == STATE_IDLE || 
                current_state == STATE_QUEUED ||
                current_state == STATE_PLAYING ||
                current_state == STATE_PLAYING_ASK ||
                current_state == STATE_PLAYING_CONFIRM) {
                
                // Cancel SAY screen linger timer (if active)
                if (say_screen_linger_start != 0) {
                    Serial.println("   ⏱️  SAY screen linger cancelled by user recording");
                    say_screen_linger_start = 0;
                }
                
                // Check if we have enough PSRAM for new recording
                if (!queue_can_record()) {
                    Serial.println("   ❌ Cannot record: Queue full (PSRAM < 4MB free)");
                    Serial.println("   💡 Wait for queued uploads to complete");
                    // TODO: Show visual feedback to user
                    return;
                }
                
                // Capture origin state before entering RECORDING
                g_state_before_recording = current_state;
                Serial.printf("   → Starting recording (origin: %s)\n", state_machine_get_name(g_state_before_recording));
                
                state_machine_set(STATE_RECORDING);
                
                // Start audio recording
                if (audio_start_recording()) {
                    Serial.println("   ✅ Audio recording started");
                } else {
                    Serial.println("   ❌ Failed to start audio recording");
                    // Return to appropriate state (check if queue has items)
                    if (queue_size() > 0 && g_state_before_recording == STATE_IDLE) {
                        state_machine_set(STATE_QUEUED);
                    } else {
                        state_machine_set(g_state_before_recording);
                    }
                }
            }
        } else if (event == BUTTON_EVENT_RELEASE) {
            // Stop recording (button released)
            if (current_state == STATE_RECORDING) {
                Serial.println("   → Stopping recording (released)");
                
                // Get filename before stopping
                String filename = audio_get_recording_filename();
                
                // Stop audio recording
                if (audio_stop_recording()) {
                    if (filename.length() > 0) {
                        Serial.printf("   ✅ Recording saved: %s\n", filename.c_str());
                    } else {
                        Serial.println("   ✅ Recording ready in PSRAM (SD not available)");
                    }
                    
                    // Handle based on origin state - QUEUED takes priority!
                    if (g_state_before_recording == STATE_QUEUED) {
                        // From QUEUE: Add to queue and return to QUEUE
                        Serial.println("   → Origin was QUEUE: adding to queue and returning to QUEUE");
                        
                        // Get recording data
                        size_t wavSize = 0;
                        uint8_t* wavData = audio_get_recording_wav(&wavSize);
                        
                        if (wavData && wavSize > 0) {
                            if (queue_enqueue_recording(wavData, wavSize, filename.c_str())) {
                                Serial.println("   ✅ Added to queue (new recording at end)");
                                audio_free_recording_buffer();
                                Serial.println("   🧹 Freed recording PSRAM buffer");
                                
                                // Refresh queue display
                                String filenames = queue_get_filenames_list();
                                display_manager_update_queue_filenames(filenames.c_str());
                            } else {
                                Serial.println("   ❌ Failed to add to queue");
                            }
                        }
                        
                        // Return to QUEUE state
                        state_machine_set(STATE_QUEUED);
                    } else if (g_state_before_recording == STATE_IDLE ||
                               g_state_before_recording == STATE_PLAYING_ASK ||
                               g_state_before_recording == STATE_PLAYING_CONFIRM) {
                        // From IDLE/ASK/CONFIRM: Auto-upload (continue conversation)
                        Serial.println("   → Transitioning to UPLOADING (conversation continues)");
                        state_machine_set(STATE_UPLOADING);
                        state_machine_set_recording(filename.c_str());
                        // Upload happens in loop (non-blocking)
                    } else {
                        // Fallback: transition to UPLOADING (shouldn't happen)
                        Serial.println("   ⚠️  Unknown origin state: defaulting to UPLOADING");
                        state_machine_set(STATE_UPLOADING);
                        state_machine_set_recording(filename.c_str());
                    }
                } else {
                    Serial.println("   ❌ Failed to stop audio recording");
                    // Return to appropriate state (check if queue has items)
                    if (queue_size() > 0 && g_state_before_recording == STATE_IDLE) {
                        state_machine_set(STATE_QUEUED);
                    } else {
                        state_machine_set(g_state_before_recording);
                    }
                }
            }
        }
    }
    
    if (button == BUTTON_BOTTOM) {
        if (event == BUTTON_EVENT_CLICK) {
            // SAY state: Cancel linger and return to IDLE immediately
            if (current_state == STATE_PLAYING) {
                Serial.println("   → Bottom button: Canceling SAY screen (returning to IDLE)");
                
                // Stop audio playback immediately
                if (audio_is_playing()) {
                    audio_stop_playback();
                    Serial.println("   ⏸️  Audio playback stopped");
                }
                
                // Free TTS buffers to recover PSRAM
                freeTtsBuffers();
                
                // Cancel linger timer
                if (say_screen_linger_start != 0) {
                    Serial.println("   ⏱️  SAY screen linger cancelled by button");
                    say_screen_linger_start = 0;
                }
                
                // Give system time to clean up audio before state transition
                delay(10);
                lv_refr_now(NULL);  // Process any pending LVGL updates
                
                // Return to appropriate state
                if (queue_size() > 0) {
                    state_machine_set(STATE_QUEUED);
                } else {
                    state_machine_set(STATE_IDLE);
                }
                return;
            }
            
            // CONFIRM state: Abort command execution
            if (current_state == STATE_PLAYING_CONFIRM) {
                Serial.println("   → Bottom button: Aborting command (returning to IDLE)");
                extern String g_pending_command;
                g_pending_command = "";  // Clear pending command
                
                // Free TTS buffers to recover PSRAM
                freeTtsBuffers();
                
                // Reset conversation (start fresh next time)
                state_machine_reset_conversation();
                Serial.println("   🔄 Conversation reset (next recording starts fresh)");
                
                // Return to appropriate state
                if (queue_size() > 0) {
                    state_machine_set(STATE_QUEUED);
                } else {
                    state_machine_set(STATE_IDLE);
                }
                return;
            }
            
            // ASK state: Abort/skip (return to IDLE)
            if (current_state == STATE_PLAYING_ASK) {
                Serial.println("   → Bottom button: Aborting ASK (returning to IDLE)");
                
                // Free TTS buffers to recover PSRAM
                freeTtsBuffers();
                
                // Reset conversation (start fresh next time)
                state_machine_reset_conversation();
                Serial.println("   🔄 Conversation reset (next recording starts fresh)");
                
                // Return to appropriate state
                if (queue_size() > 0) {
                    state_machine_set(STATE_QUEUED);
                } else {
                    state_machine_set(STATE_IDLE);
                }
                return;
            }
            
            // Queue screen: Delete selected recording
            if (current_state == STATE_QUEUED) {
                Serial.println("   → Bottom button: Delete selected recording");
                // Trigger delete event (same as touch button)
                display_event_callback(DISPLAY_STATE_QUEUED, QUEUE_EVENT_DELETE_ALL);
                return;
            }
            
            // Cancel UPLOADING/DOWNLOADING
            if (current_state == STATE_UPLOADING || current_state == STATE_DOWNLOADING) {
                Serial.println("   → Bottom button: Canceling network operation");
                extern volatile bool g_upload_cancel;  // Defined in https_client.cpp
                g_upload_cancel = true;
                return;
            }
            
            // Cancel current operation
            if (current_state == STATE_RECORDING) {
                Serial.println("   → Canceling recording");
                
                // Stop recording (file will be saved but not uploaded)
                if (audio_stop_recording()) {
                    Serial.println("   ✅ Recording canceled (file discarded)");
                    // TODO: Optionally delete the file here
                }
                
                // Return to appropriate state (check if queue has items)
                if (queue_size() > 0 && g_state_before_recording == STATE_IDLE) {
                    Serial.println("   → Queue has files - returning to QUEUED");
                    state_machine_set(STATE_QUEUED);
                } else {
                    Serial.printf("   → Returning to origin state: %s\n", state_machine_get_name(g_state_before_recording));
                    state_machine_set(g_state_before_recording);
                }
                
            } else if (current_state == STATE_UPLOADING || 
                       current_state == STATE_DOWNLOADING || 
                       current_state == STATE_PLAYING ||
                       current_state == STATE_PLAYING_ASK ||
                       current_state == STATE_PLAYING_CONFIRM) {
                // Cancel upload/download/playback
                Serial.println("   → Canceling operation");
                
                // Signal upload to abort
                extern volatile bool g_upload_cancel;
                g_upload_cancel = true;
                
                // Clear pending command if in CONFIRM state
                extern String g_pending_command;
                g_pending_command = "";
                
                // Return to appropriate state
                if (queue_size() > 0) {
                    state_machine_set(STATE_QUEUED);
                } else {
                    state_machine_set(STATE_IDLE);
                }
                
            } else if (current_state != STATE_IDLE) {
                Serial.println("   → Canceling, return to IDLE");
                state_machine_set(STATE_IDLE);
            }
        } else if (event == BUTTON_EVENT_HOLD_POWER) {
            // Power off (enter sleep)
            Serial.println("   → Power off (entering SLEEP)");
            sleep_manager_enter_sleep();
        }
    }
}

// ============================================
// HELPER: Create confirmation dialog
// ============================================
static lv_obj_t* create_confirmation_dialog(const char* title, const char* message,
                                            lv_event_cb_t yes_cb, lv_event_cb_t no_cb) {
    // Create modal background
    lv_obj_t* bg = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bg, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_70, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    
    // Create dialog container
    lv_obj_t* dialog = lv_obj_create(bg);
    lv_obj_set_size(dialog, 280, 180);
    lv_obj_center(dialog);
    lv_obj_set_style_radius(dialog, 12, 0);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0x2C2C2C), 0);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0x505050), 0);
    lv_obj_set_style_border_width(dialog, 2, 0);
    lv_obj_clear_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);  // Disable left/right scrolling
    
    // Title
    lv_obj_t* title_label = lv_label_create(dialog);
    lv_label_set_text(title_label, title);
    lv_obj_set_style_text_color(title_label, lv_color_white(), 0);
    lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 12);
    
    // Message
    lv_obj_t* msg_label = lv_label_create(dialog);
    lv_label_set_text(msg_label, message);
    lv_obj_set_width(msg_label, 250);
    lv_obj_set_style_text_align(msg_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(msg_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_align(msg_label, LV_ALIGN_TOP_MID, 0, 45);
    
    // No button (left)
    lv_obj_t* btn_no = lv_btn_create(dialog);
    lv_obj_set_size(btn_no, 110, 40);
    lv_obj_align(btn_no, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_bg_color(btn_no, lv_color_hex(0x505050), 0);
    lv_obj_add_event_cb(btn_no, no_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* label_no = lv_label_create(btn_no);
    lv_label_set_text(label_no, "No");
    lv_obj_center(label_no);
    
    // Yes button (right)
    lv_obj_t* btn_yes = lv_btn_create(dialog);
    lv_obj_set_size(btn_yes, 110, 40);
    lv_obj_align(btn_yes, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_bg_color(btn_yes, lv_color_hex(0xCC3333), 0);  // Red for destructive action
    lv_obj_add_event_cb(btn_yes, yes_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* label_yes = lv_label_create(btn_yes);
    lv_label_set_text(label_yes, "Yes");
    lv_obj_center(label_yes);
    
    return bg;
}

// ============================================
// DELETE CONFIRMATION DIALOG CALLBACKS
// ============================================
static void delete_confirm_yes_cb(lv_event_t* e) {
    Serial.println("📋 Delete confirmed (Yes clicked)");
    
    // Close dialog FIRST (before state transitions) to prevent freeze
    if (g_delete_msgbox) {
        lv_obj_del_async(g_delete_msgbox);
        g_delete_msgbox = nullptr;
    }
    
    if (g_delete_all_pending) {
        Serial.println("   ✅ Deleting all queue files");
        queue_delete_all();
        g_delete_all_pending = false;
        g_queue_delete_confirmation = false;
        
        // Return to IDLE (queue is now empty)
        state_machine_set(STATE_IDLE);
        display_manager_set_state(DISPLAY_STATE_IDLE);  // Update display to show IDLE screen
    } else if (g_delete_pending_filename.length() > 0) {
        Serial.printf("   ✅ Deleting: %s\n", g_delete_pending_filename.c_str());
        
        // Delete using sd_manager (POSIX API)
        if (deleteFile(g_delete_pending_filename.c_str())) {
            Serial.println("   ✅ File deleted from SD");
            
            // Remove from in-memory queue
            queue_remove_from_memory(g_delete_pending_filename);
            
            // Refresh queue UI
            String filenames = queue_get_filenames_list();
            display_manager_update_queue_filenames(filenames.c_str());
            
            // If queue is now empty, return to IDLE
            if (queue_size() == 0) {
                Serial.println("   📋 Queue now empty - returning to IDLE");
                state_machine_set(STATE_IDLE);
                display_manager_set_state(DISPLAY_STATE_IDLE);  // Update display to show IDLE screen
            }
        } else {
            Serial.println("   ❌ Failed to delete file from SD");
        }
        
        g_delete_pending_filename = "";
    }
}

static void delete_confirm_no_cb(lv_event_t* e) {
    Serial.println("📋 Delete canceled (No clicked)");
    g_delete_all_pending = false;
    g_delete_pending_filename = "";
    g_queue_delete_confirmation = false;
    
    // Close dialog
    if (g_delete_msgbox) {
        lv_obj_del_async(g_delete_msgbox);
        g_delete_msgbox = nullptr;
    }
}

// ============================================
// DISPLAY EVENT CALLBACK (from display_manager)
// ============================================
void display_event_callback(display_state_t from_state, int event_data) {
    Serial.printf("🎯 Touch event: state=%d, data=%d\n", from_state, event_data);
    
    // Update activity timestamp (touch is activity!)
    state_machine_update_activity();
    
    if (from_state == DISPLAY_STATE_IDLE) {
        if (event_data == 1) {
            Serial.println("   🎤 Mic icon touched (not used - physical button only)");
        } else if (event_data == 2) {
            Serial.println("   ⏻ Power icon touched");
            sleep_manager_enter_sleep();
        }
    }
    
    // Queue screen events
    else if (from_state == DISPLAY_STATE_QUEUED) {
        Serial.printf("   Queue event: %d\n", event_data);
        
        if (event_data == QUEUE_EVENT_PLAY) {
            // Play button clicked - play selected recording INLINE (stay in QUEUED)
            Serial.println("   PLAY button clicked");
            
            // Get selected recording from queue screen
            lv_obj_t* queue_screen = display_manager_get_queue_screen();
            String filename = "";
            int selected_index = 1;
            
            if (queue_screen) {
                selected_index = screen_queue_get_selected_index(queue_screen);
                filename = screen_queue_get_selected_filename(queue_screen);
                
                if (selected_index == 0) {
                    // "All recordings" selected - enable sequential playback
                    Serial.println("   → Playing ALL recordings (auto-advance enabled)");
                    g_auto_advance_playback = true;
                    g_current_playback_index = 1;  // Start with first file
                    
                    // Get items list to find first file
                    auto items = queue_get_items_info();
                    if (!items.empty()) {
                        filename = items[0].filename;
                        // Update queue screen selection to highlight first file
                        if (queue_screen) {
                            extern void screen_queue_set_selected_index(lv_obj_t *screen_obj, int index);
                            screen_queue_set_selected_index(queue_screen, 1);
                        }
                    }
                } else {
                    Serial.printf("   → Playing selected recording (index %d)\n", selected_index);
                    g_auto_advance_playback = false;  // Single file playback
                    g_current_playback_index = selected_index;
                }
            }
            
            if (filename.length() == 0) {
                filename = queue_get_oldest_filename();
            }
            
            if (filename.length() > 0) {
                Serial.printf("   → File selected: %s\n", filename.c_str());
                Serial.printf("   → Attempting inline playback (stay in QUEUED)...\n");
                
                // Show progress bar
                if (queue_screen) {
                    screen_queue_show_progress(queue_screen, true);
                    screen_queue_update_progress(queue_screen, 0);  // Start at 0%
                }
                
                // Use 100% volume for queue playback (louder for easier listening)
                audio_set_volume(100);
                Serial.println("   🔊 Queue playback: Using 100% volume");
                
                // Start playback WITHOUT changing state (inline preview)
                if (audio_play_file(filename.c_str())) {
                    // STAY in QUEUED state - this is inline preview playback
                    state_machine_set_recording(filename.c_str());  // Track what's playing
                    Serial.println("   Inline playback started (staying in QUEUED)");
                    Serial.printf("   → Progress bar visible, file will NOT be deleted\n");
                } else {
                    Serial.println("   Failed to start playback!");
                    Serial.println("   → Possible causes:");
                    Serial.println("      - File not found or corrupted");
                    Serial.println("      - Audio codec busy or not initialized");
                    Serial.println("      - Invalid WAV format");
                    // Hide progress bar on failure
                    if (queue_screen) {
                        screen_queue_show_progress(queue_screen, false);
                    }
                }
            } else {
                Serial.println("   ⚠️  Queue is empty!");
            }
            
        } else if (event_data == QUEUE_EVENT_STOP) {
            // Stop button clicked - stop playback
            Serial.println("   ⏸️  STOP button clicked");
            
            if (audio_is_playing()) {
                audio_stop_playback();
                Serial.println("   ✅ Playback stopped");
                
                // Restore NVS volume setting
                int nvs_volume = settings_manager_get_volume();
                audio_set_volume(nvs_volume);
                Serial.printf("   🔊 Restored NVS volume: %d\n", nvs_volume);
                
                // Clear auto-advance flag (stop sequential playback)
                if (g_auto_advance_playback) {
                    g_auto_advance_playback = false;
                    Serial.printf("   → Auto-advance disabled (stopped at file %d)\n", g_current_playback_index);
                }
                
                // Hide progress bar
                lv_obj_t* queue_screen = display_manager_get_queue_screen();
                if (queue_screen) {
                    screen_queue_show_progress(queue_screen, false);
                    Serial.println("   → Progress bar hidden");
                }
            } else {
                Serial.println("   ⚠️  Nothing is playing");
            }
            
        } else if (event_data == QUEUE_EVENT_PROCESS) {
            // Upload button - upload selected recording
            Serial.println("   🟢 UPLOAD button clicked");
            
            // Interrupt playback if active and proceed with upload
            if (audio_is_playing()) {
                Serial.println("   ⏸️  Interrupting playback for upload");
                audio_stop_playback();
                lv_obj_t* queue_screen = display_manager_get_queue_screen();
                if (queue_screen) {
                    screen_queue_show_progress(queue_screen, false);
                }
                state_machine_set_recording("");  // Clear playback tracking
                // Continue with upload...
            }
            
            // Get selected recording from queue screen
            lv_obj_t* queue_screen = display_manager_get_queue_screen();
            String filename = "";
            int selected_index = 1;
            
            if (queue_screen) {
                selected_index = screen_queue_get_selected_index(queue_screen);
                filename = screen_queue_get_selected_filename(queue_screen);
                
                if (selected_index == 0) {
                    // "All recordings" selected - upload all
                    Serial.println("   → Uploading ALL recordings sequentially");
                    // TODO: Implement sequential upload of all
                    filename = queue_get_oldest_filename();  // Start with oldest for now
                } else {
                    Serial.printf("   → Uploading selected recording (index %d)\n", selected_index);
                }
            }
            
            if (filename.length() == 0) {
                filename = queue_get_oldest_filename();
            }
            
            if (filename.length() > 0) {
                Serial.printf("   → Starting upload: %s\n", filename.c_str());
                
                // Transition to UPLOADING state
                state_machine_set(STATE_UPLOADING);
                state_machine_set_recording(filename.c_str());
                Serial.println("   ✅ State changed to UPLOADING");
                // Upload happens in loop
            } else {
                Serial.println("   ⚠️  Queue is empty!");
            }
            
        } else if (event_data == QUEUE_EVENT_DELETE_ALL) {
            // Delete button - delete selected or all recordings
            Serial.println("   🗑️  DELETE button clicked");
            
            // Interrupt playback if active and proceed with delete
            if (audio_is_playing()) {
                Serial.println("   ⏸️  Interrupting playback for delete");
                audio_stop_playback();
                lv_obj_t* queue_screen = display_manager_get_queue_screen();
                if (queue_screen) {
                    screen_queue_show_progress(queue_screen, false);
                }
                state_machine_set_recording("");  // Clear playback tracking
                // Continue with delete...
            }
            
            // Get selected recording from queue screen
            lv_obj_t* queue_screen = display_manager_get_queue_screen();
            int selected_index = 1;
            
            if (queue_screen) {
                selected_index = screen_queue_get_selected_index(queue_screen);
            }
            
            if (selected_index == 0) {
                // "All recordings" selected - delete all with on-screen confirmation
                Serial.println("   🗑️  Delete ALL requested - showing confirmation dialog");
                
                // Set pending delete state
                g_delete_all_pending = true;
                g_delete_pending_filename = "";
                
                // Create confirmation dialog
                g_delete_msgbox = create_confirmation_dialog(
                    "Delete All?",
                    "Delete all recordings?\n\nThis cannot be undone.",
                    delete_confirm_yes_cb,
                    delete_confirm_no_cb
                );
            } else {
                // Single recording selected - delete with on-screen confirmation
                String filename = screen_queue_get_selected_filename(queue_screen);
                if (filename.length() > 0) {
                    // Extract just the basename for display
                    int lastSlash = filename.lastIndexOf('/');
                    String displayName = (lastSlash >= 0) ? filename.substring(lastSlash + 1) : filename;
                    
                    Serial.printf("   🗑️  Delete requested: %s\n", filename.c_str());
                    
                    // Set pending delete state
                    g_delete_all_pending = false;
                    g_delete_pending_filename = filename;
                    
                    // Create confirmation dialog
                    String msg = "Delete recording?\n\n" + displayName + "\n\nThis cannot be undone.";
                    g_delete_msgbox = create_confirmation_dialog(
                        "Delete?",
                        msg.c_str(),
                        delete_confirm_yes_cb,
                        delete_confirm_no_cb
                    );
                } else {
                    Serial.println("   ⚠️  No file selected");
                }
            }
        }
    }
    
    // Settings screen events - forward to settings manager
    else if (from_state == DISPLAY_STATE_SETTINGS) {
        settings_manager_handle_event(event_data);
    }
}

// ============================================
// PSRAM STRESS TEST (for testing 80 MHz PSRAM)
// ============================================
/**
 * @brief Test PSRAM stability with memcpy stress
 * 
 * Call this after setup() if you enabled CONFIG_SPIRAM_SPEED_80M
 * to verify hardware stability at higher speed.
 * 
 * If it fails/crashes/verifies incorrectly: drop back to 40 MHz
 */
void psram_stress_test() {
    Serial.println("\n🧪 PSRAM Stress Test - Testing memcpy at current speed...");
    
    const size_t TEST_SIZE = 256 * 1024;  // 256 KB
    uint8_t *buf_a = (uint8_t*) heap_caps_malloc(TEST_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t *buf_b = (uint8_t*) heap_caps_malloc(TEST_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!buf_a || !buf_b) {
        Serial.println("   ❌ PSRAM alloc failed for stress test");
        if (buf_a) heap_caps_free(buf_a);
        if (buf_b) heap_caps_free(buf_b);
        return;
    }
    
    // Fill buffer A with test pattern
    memset(buf_a, 0xA5, TEST_SIZE);
    
    // Copy to buffer B
    unsigned long start = micros();
    memcpy(buf_b, buf_a, TEST_SIZE);
    unsigned long duration = micros() - start;
    
    // Verify copy
    bool pass = true;
    for (size_t i = 0; i < TEST_SIZE; i += 4096) {
        if (buf_b[i] != 0xA5) {
            Serial.printf("   ❌ PSRAM verify FAILED @ offset %u (expected 0xA5, got 0x%02X)\n", 
                         (unsigned)i, buf_b[i]);
            pass = false;
            break;
        }
    }
    
    heap_caps_free(buf_a);
    heap_caps_free(buf_b);
    
    if (pass) {
        float mb_per_sec = (TEST_SIZE / 1024.0 / 1024.0) / (duration / 1000000.0);
        Serial.println("\n╔════════════════════════════════════════╗");
        Serial.println("║  ✅ PSRAM TEST PASSED (80 MHz OK!)    ║");
        Serial.printf("║  Bandwidth: %.1f MB/s                 ║\n", mb_per_sec);
        Serial.println("╚════════════════════════════════════════╝\n");
    } else {
        Serial.println("\n╔════════════════════════════════════════╗");
        Serial.println("║  ❌ PSRAM TEST FAILED!                ║");
        Serial.println("║  Drop back to 40 MHz in platformio.ini║");
        Serial.println("╚════════════════════════════════════════╝\n");
    }
}

// ============================================
// SETUP
// ============================================
void setup() {
    // Boost CPU to max frequency for best performance
    setCpuFrequencyMhz(240);
    
    Serial.begin(115200);
    delay(3000);  // Give USB CDC time to initialize (important for PlatformIO monitor)
    
    // Flush any stale data
    while (Serial.available()) Serial.read();
    
    Serial.println("\n========================================");
    Serial.println("LVGL Native Test - ESP32-S3 (PlatformIO)");
    Serial.println("========================================");
    
    // ===== PSRAM STRESS TEST (80 MHz verification) =====
    psram_stress_test();  // Comment out if test passes, drop mem freq in pio.ini if fails
    Serial.println();  // Extra newline for readability
    
    // ===== OTA ROLLBACK PROTECTION =====
    // Mark app as valid after successful boot to enable rollback
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // This is first boot after OTA - mark as valid
            Serial.println("🔄 OTA: First boot after update detected");
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                Serial.println("✅ OTA: Firmware validated - rollback disabled");
            } else {
                Serial.println("⚠️  OTA: Failed to mark app as valid");
            }
        }
    }
    Serial.printf("🔄 OTA: Running from %s\n", 
                 (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) ? "app0 (ota_0)" : "app1 (ota_1)");
    
    // ===== PSRAM CHECK =====
    bool psram_ok = psramFound();
    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    
    if (psram_ok && psram_total > 0) {
        Serial.printf("💾 RAM: PSRAM %.0f MB (%.0f MB free), Internal %.0f KB (%.0f KB free) ✅\n",
                      psram_total / 1024.0 / 1024.0,
                      psram_free / 1024.0 / 1024.0,
                      ESP.getHeapSize() / 1024.0,
                      ESP.getFreeHeap() / 1024.0);
    } else {
        Serial.println("💾 RAM: PSRAM NOT WORKING ⚠️");
    }
    
    // ===== FLASH CHECK =====
    uint32_t flashSize = ESP.getFlashChipSize();
    Serial.printf("💾 FLASH: %.0f MB, Sketch %.0f KB, Free %.1f MB %s\n",
                  flashSize / 1024.0 / 1024.0,
                  ESP.getSketchSize() / 1024.0,
                  ESP.getFreeSketchSpace() / 1024.0 / 1024.0,
                  (flashSize < 16 * 1024 * 1024) ? "⚠️" : "✅");
    
    // ===== I2C INIT =====
    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(100000);
    int devices = 0;
    for (byte addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) devices++;
    }
    Serial.printf("🔧 I2C: Ready, %d devices found ✅\n", devices);
    
    // ===== BATTERY INIT (Must be BEFORE IO Expander begin()!) =====
    if (initBattery()) {
        Serial.printf("🔋 BATTERY: AXP2101 initialized ✅\n");
    } else {
        Serial.println("🔋 BATTERY: Init failed ⚠️");
    }
    
    // ===== IO EXPANDER INIT (Required for Display!) =====
    expander = new EXAMPLE_CHIP_CLASS(TCA95xx_8bit,
                                      (i2c_port_t)0, 
                                      ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000,
                                      IIC_SCL, IIC_SDA);
    expander->init();
    expander->begin();
    
    // Configure expander pins for display
    expander->pinMode(0, OUTPUT);
    expander->pinMode(1, OUTPUT);
    expander->pinMode(2, OUTPUT);
    expander->pinMode(6, OUTPUT);
    
    // Display enable sequence
    expander->digitalWrite(0, LOW);
    expander->digitalWrite(1, LOW);
    expander->digitalWrite(2, LOW);
    expander->digitalWrite(6, LOW);
    delay(20);
    expander->digitalWrite(0, HIGH);
    expander->digitalWrite(1, HIGH);
    expander->digitalWrite(2, HIGH);
    expander->digitalWrite(6, HIGH);
    expander->pinMode(4, INPUT_PULLUP);
    
    Serial.println("🔧 IO EXPANDER: TCA9554 initialized ✅");
    
    // ===== INITIALIZE AUDIO (BEFORE LVGL - RESERVES DMA BUFFERS) =====
    if (audio_init()) {
        Serial.printf("🎙️ AUDIO: ES8311, I2S DMA 8KB, Rec buffer 2MB PSRAM, Free PSRAM %dMB ✅\n",
                     ESP.getFreePsram() / (1024 * 1024));
        
        // Initialize SPIFFS for audio assets (boot sounds, UI feedback)
        if (audio_assets_init()) {
            Serial.println("🔊 AUDIO ASSETS: SPIFFS ready for UI sounds ✅");
        }
    } else {
        Serial.println("🎙️ AUDIO: Init failed ⚠️");
    }
    
    // ===== DISPLAY HARDWARE INIT =====
    bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
    gfx = new Arduino_SH8601(bus, -1, 0, false, LCD_WIDTH, LCD_HEIGHT);
    if (!gfx->begin()) {
        Serial.println("❌ DISPLAY: Init failed!");
        while(1) delay(1000);
    }
    gfx->fillScreen(BLACK);
    gfx->Display_Brightness(255);
    Serial.println("🖥️  DISPLAY: SH8601 initialized ✅");
    
    // ===== TOUCH CONTROLLER INIT =====
    IIC_Bus = std::make_shared<Arduino_HWIIC>(IIC_SDA, IIC_SCL, &Wire);
    touch = new Arduino_FT3x68(IIC_Bus, 0x38, DRIVEBUS_DEFAULT_VALUE, TP_INT, touch_interrupt_handler);
    if (touch->begin()) {
        touch->IIC_Write_Device_State(touch->Arduino_IIC_Touch::Device::TOUCH_POWER_MODE,
                                      touch->Arduino_IIC_Touch::Device_Mode::TOUCH_POWER_MONITOR);
        Serial.println("🖱️  TOUCH: FT3x68 initialized ✅");
    } else {
        Serial.println("🖱️  TOUCH: Init failed ⚠️");
    }
    
    // ===== LVGL INIT =====
    lv_init();
    
    // Allocate LVGL buffers - try large PSRAM buffers first for better performance
    // QSPI display doesn't need DMA buffers - flush callback copies to display
    // Larger buffers = fewer flush calls = smoother rendering
    
    // Target: 1/4 screen in PSRAM (112 lines × 368 pixels = ~80 KB per buffer)
    // Fallback: 1/20 screen in internal RAM (22 lines × 368 pixels = ~16 KB per buffer)
    const size_t FAST_BUF_SIZE = (LCD_WIDTH * LCD_HEIGHT) / 4;   // ~1/4 screen
    const size_t SAFE_BUF_SIZE = (LCD_WIDTH * LCD_HEIGHT) / 20;  // ~1/20 screen
    size_t buf_size = 0;
    
    // Try FAST mode: large buffers in PSRAM
    buf1 = (lv_color_t *)heap_caps_malloc(FAST_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    buf2 = (lv_color_t *)heap_caps_malloc(FAST_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (buf1 && buf2) {
        // Success: High-performance mode (~1/4 screen, ~160 KB total in PSRAM)
        buf_size = FAST_BUF_SIZE;
        Serial.printf("🎨 LVGL: FAST mode - Buffers 2×%d KB (PSRAM, 1/4 screen)\n", 
                     (buf_size * sizeof(lv_color_t)) / 1024);
    } else {
        // Cleanup partial allocation
        if (buf1) { heap_caps_free(buf1); buf1 = nullptr; }
        if (buf2) { heap_caps_free(buf2); buf2 = nullptr; }
        
        Serial.println("⚠️  LVGL PSRAM large buffer allocation failed");
        Serial.printf("   Requested: %d KB per buffer\n", (FAST_BUF_SIZE * sizeof(lv_color_t)) / 1024);
        Serial.printf("   Free PSRAM: %d MB\n", ESP.getFreePsram() / (1024 * 1024));
        Serial.println("   Falling back to small internal RAM buffers...");
        
        // Fallback: SAFE mode with small buffers in internal RAM
        buf1 = (lv_color_t *)heap_caps_malloc(SAFE_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        buf2 = (lv_color_t *)heap_caps_malloc(SAFE_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        
        if (buf1 && buf2) {
            // Safe mode: smaller buffers (~1/20 screen, ~32 KB total in internal RAM)
            buf_size = SAFE_BUF_SIZE;
            Serial.printf("🎨 LVGL: SAFE mode - Buffers 2×%d KB (Internal RAM, 1/20 screen)\n",
                         (buf_size * sizeof(lv_color_t)) / 1024);
        } else {
            // Last resort: single buffer in internal RAM
            if (buf2) { heap_caps_free(buf2); buf2 = nullptr; }
            if (!buf1) {
                buf1 = (lv_color_t *)heap_caps_malloc(SAFE_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
            
            if (buf1) {
                buf_size = SAFE_BUF_SIZE;
                Serial.printf("⚠️  LVGL: MINIMAL mode - Buffer 1×%d KB (Internal RAM, single buffer)\n",
                             (buf_size * sizeof(lv_color_t)) / 1024);
            } else {
                Serial.println("❌ LVGL buffer allocation FAILED completely!");
                while(1) delay(1000);
            }
        }
    }
    
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_size);
    
    // Register display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = lvgl_flush_callback;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
    
    // Register touch input
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_callback;
    lv_indev_drv_register(&indev_drv);
    
    // Setup LVGL tick timer (2ms - matches factory ROM)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &lvgl_tick_callback,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer);
    esp_timer_start_periodic(lvgl_tick_timer, 2000);
    Serial.println("🎨 LVGL: Tick timer 2ms ✅");
    
    // Log memory after LVGL buffer allocation
    Serial.printf("💾 After LVGL buffers: Internal %d KB free, PSRAM %d MB free\n",
                  ESP.getFreeHeap() / 1024,
                  ESP.getFreePsram() / (1024 * 1024));
    
    // ===== INITIALIZE NVS MANAGER =====
    NVS::init();
    Serial.println("💾 NVS: Initialized ✅");
    
    // ===== INITIALIZE SETTINGS MANAGER =====
    settings_manager_init(gfx);
    settings_manager_load_from_nvs();  // Loads and applies all settings
    
    // ===== INITIALIZE SLEEP MANAGER =====
    sleep_manager_init(gfx, settings_manager_get_brightness());
    
    // ===== INITIALIZE STATE MACHINE =====
    state_machine_init(state_change_callback);
    Serial.printf("🔄 STATE MACHINE: Initialized (%s) ✅\n", state_machine_get_name(state_machine_get()));
    
    // ===== INITIALIZE DISPLAY MANAGER =====
    if (!display_manager_init(display_event_callback)) {
        Serial.println("❌ DISPLAY MANAGER: Init failed!");
        while(1) delay(1000);
    }
    display_manager_set_state(DISPLAY_STATE_BOOT);
    Serial.println("🎨 DISPLAY MANAGER: Ready (BOOT screen) ✅");
    
    // Start checking for test mode button hold
    bool test_mode_requested = false;
    unsigned long button_press_start = 0;
    if (expander) {
        expander->pinMode(4, INPUT_PULLUP);
        delay(10);
        if (expander->digitalRead(4) == HIGH) {
            Serial.println("🔘 Bottom button held - will enter TEST MODE if held >1s total");
            test_mode_requested = true;
            button_press_start = millis();  // Record when button was first detected
        }
    }
    
    // ===== BOOT SEQUENCE WITH REAL INITIALIZATION =====
    Serial.println("\n🔄 Boot sequence (initializing modules)...");
    int progress = 10;  // Display manager already done
    
    // Step 1: Initialize RTC (10% -> 20%)
    if (initRTC()) {
        Serial.println("🕐 RTC: PCF85063 initialized ✅");
    } else {
        Serial.println("🕐 RTC: Not detected ⚠️");
    }
    progress = 20;
    display_manager_update_data(&progress);
    lv_timer_handler();
    
    // Step 2: Initialize IMU (20% -> 30%)
    if (IMU::init(Wire, IIC_SDA, IIC_SCL)) {
        Serial.println("🎯 IMU: QMI8658 initialized ✅");
    } else {
        Serial.println("🎯 IMU: Not detected ⚠️");
    }
    progress = 30;
    display_manager_update_data(&progress);
    lv_timer_handler();
    
    // Step 3: Power up SD card (30% -> 40%)
    expander->pinMode(7, OUTPUT);
    expander->digitalWrite(7, HIGH);
    delay(500);  // Wait for SD card to stabilize
    progress = 40;
    display_manager_update_data(&progress);
    lv_timer_handler();
    
    // Step 4: Initialize SD card (40% -> 60%)
    if (initSDCard(false)) {
        createRecordingsDirectory();
        Serial.printf("💾 SD CARD: %.0f MB, %d recordings ✅\n", 
                     (float)sdCardSize, getRecordingCount());
        
        // Set time from last recording (fallback before WiFi/NTP)
        setTimeFromLastRecording();
    } else {
        Serial.println("💾 SD CARD: Not available ⚠️");
    }
    progress = 60;
    display_manager_update_data(&progress);
    lv_timer_handler();
    
    // Step 5: Initialize WiFi (60% -> 75%)
    if (initWiFi()) {
        // Wait a moment for connection
        delay(2000);
        if (isWiFiConnected()) {
            Serial.printf("📡 WIFI: Connected to %s (IP: %s) ✅\n", 
                         getWiFiSSID().c_str(), getWiFiIP().c_str());
            
            // Initialize HTTPS client after WiFi is connected
            initHttpsClient();
        } else {
            Serial.println("📡 WIFI: Connecting in background...");
        }
    } else {
        Serial.println("📡 WIFI: Not configured ⚠️");
    }
    progress = 75;
    display_manager_update_data(&progress);
    lv_timer_handler();
    
    // Step 5.5: Initialize upload queue (75% -> 80%)
    queue_init();
    
    // ===== TEMPORARY: POPULATE QUEUE FOR TESTING (COMMENT OUT AFTER TESTING) =====
    // This copies 2-3 recordings from /sdcard/recordings to /sdcard/queue for testing
    // TO UNDO: Just comment out this entire block and reflash
    #if 1  // Set to 0 to disable
    if (sdCardAvailable && queue_size() == 0) {
        Serial.println("\n🧪 TEST MODE: Populating queue with sample recordings...");
        File root = SD.open("/sdcard/recordings");
        if (root && root.isDirectory()) {
            int copied = 0;
            File entry;
            while ((entry = root.openNextFile()) && copied < 3) {
                if (!entry.isDirectory() && String(entry.name()).endsWith(".wav")) {
                    String sourcePath = "/sdcard/recordings/" + String(entry.name());
                    String destPath = "/sdcard/queue/" + String(entry.name());
                    
                    // Copy file
                    File source = SD.open(sourcePath, FILE_READ);
                    File dest = SD.open(destPath, FILE_WRITE);
                    if (source && dest) {
                        uint8_t buf[512];
                        size_t bytesRead;
                        while ((bytesRead = source.read(buf, sizeof(buf))) > 0) {
                            dest.write(buf, bytesRead);
                        }
                        source.close();
                        dest.close();
                        Serial.printf("   ✅ Copied: %s → queue\n", entry.name());
                        copied++;
                    } else {
                        if (source) source.close();
                        if (dest) dest.close();
                        Serial.printf("   ❌ Failed to copy: %s\n", entry.name());
                    }
                }
                entry.close();
            }
            root.close();
            
            if (copied > 0) {
                Serial.printf("   📦 Queue populated with %d test files\n", copied);
                // Re-init queue to pick up new files
                queue_init();
            } else {
                Serial.println("   ⚠️  No recordings found to copy");
            }
        } else {
            Serial.println("   ⚠️  /recordings directory not found");
        }
    }
    #endif
    // ===== END TEMPORARY TEST CODE =====
    
    progress = 80;
    display_manager_update_data(&progress);
    lv_timer_handler();
    
    // Step 6: Initialize button manager (80% -> 100%)
    if (!button_manager_init(button_event_callback, expander)) {
        Serial.println("❌ BUTTONS: Init failed!");
        while(1) delay(1000);
    }
    Serial.println("🔘 BUTTONS: Manager initialized ✅");
    progress = 100;
    display_manager_update_data(&progress);
    lv_timer_handler();
    
    // ===== CHECK IF TEST MODE WAS REQUESTED =====
    if (test_mode_requested && expander) {
        // Check how long button has been held total
        unsigned long total_hold_time = millis() - button_press_start;
        
        // If still pressed
        if (expander->digitalRead(4) == HIGH) {
            Serial.printf("\n🔘 Button still held (%lums so far)...\n", total_hold_time);
            
            // If already >1s, enter immediately
            if (total_hold_time >= 1000) {
                Serial.println("✅ Button held >1s - ENTERING TEST MODE\n");
                startTestMode(expander);
                
                // Test mode loop (never returns - restarts on exit)
                while (true) {
                    handleTestMode();
                    delay(10);
                }
            }
            
            // Otherwise wait for remaining time or release
            unsigned long wait_remaining = 1000 - total_hold_time;
            Serial.printf("⏳ Waiting %lums more for test mode...\n", wait_remaining);
            unsigned long wait_start = millis();
            
            while (expander->digitalRead(4) == HIGH && (millis() - wait_start < wait_remaining)) {
                lv_timer_handler();
                delay(10);
            }
            
            // Check final state
            if (expander->digitalRead(4) == HIGH) {
                Serial.println("✅ Button held >1s total - ENTERING TEST MODE\n");
                startTestMode(expander);
                
                // Test mode loop (never returns - restarts on exit)
                while (true) {
                    handleTestMode();
                    delay(10);
                }
            } else {
                Serial.println("🔘 Button released - continuing to normal boot");
            }
        } else {
            Serial.printf("🔘 Button released after %lums - continuing to normal boot\n", total_hold_time);
        }
    }
    
    // Switch to IDLE or QUEUED based on queue contents
    Serial.println("\n🔄 Transitioning from BOOT to IDLE/QUEUE...");
    
    if (queue_size() > 0) {
        // Queue has files - set state to QUEUED
        Serial.printf("📋 Queue has %d files - setting STATE_QUEUED\n", queue_size());
        state_machine_set(STATE_QUEUED);  // This triggers callback which shows queue screen
        
        // Update queue screen with filenames list
        String filenames = queue_get_filenames_list();
        display_manager_update_queue_filenames(filenames.c_str());
        
        // Update WiFi state for button visibility
        display_manager_update_queue_wifi(isWiFiConnected());
    } else {
        // No queue - normal IDLE
        Serial.println("📋 Queue empty - setting STATE_IDLE");
        state_machine_set(STATE_IDLE);
    }
    Serial.println("✅ Boot complete - state machine initialized");
    
    // CRITICAL: Give LVGL time to process boot screen deletion and new screen creation
    // The display manager queues these operations, but LVGL needs multiple passes to complete them
    Serial.println("⏳ Processing screen transition...");
    for (int i = 0; i < 5; i++) {
        lv_timer_handler();  // Process LVGL tasks
        delay(10);  // Small delay between passes
    }
    Serial.println("✅ Screen transition complete");
    
    // CRITICAL FIX: Force sleep/wake cycle to ensure display refresh
    // This fixes boot screen persistence bug - ensures all LVGL objects are properly rendered
    Serial.println("🔄 Forcing sleep/wake cycle to refresh display...");
    
    // Force sleep (only works from IDLE state, which we're in)
    sleep_manager_enter_sleep();
    lv_timer_handler();  // Let LVGL render sleep screen
    delay(100);  // Brief pause to ensure display updates
    
    // Wake immediately - check queue status to decide state
    if (queue_size() > 0) {
        Serial.printf("🔄 State: SLEEP → QUEUED\n");
        Serial.printf("📋 Queue has %d files - showing queue screen\n", queue_size());
        state_machine_set(STATE_QUEUED);
    } else {
        Serial.printf("🔄 State: SLEEP → IDLE\n");
        state_machine_set(STATE_IDLE);
    }
    if (gfx) {
        gfx->Display_Brightness(settings_manager_get_brightness() * 255 / 100);
    }
    lv_timer_handler();  // Let LVGL render appropriate screen
    delay(50);
    
    Serial.println("✅ Display refresh complete");
    
    Serial.println("\n========================================");
    Serial.println("✅ Setup complete - Ready to test!");
    Serial.println("========================================\n");
    
    // ===== INITIALIZE WATCHDOG TIMER (60s timeout) =====
    Serial.println("🐶 Initializing watchdog timer...");
    
    // Use legacy API for Arduino ESP32 core compatibility
    esp_err_t wdt_result = esp_task_wdt_init(60, true);  // 60s timeout, panic on timeout
    if (wdt_result == ESP_OK) {
        // Subscribe current task (loop task) to watchdog
        wdt_result = esp_task_wdt_add(NULL);  // NULL = current task
        if (wdt_result == ESP_OK) {
            Serial.println("   ✅ Watchdog enabled: 60s timeout, auto-reboot on hang");
        } else {
            Serial.printf("   ⚠️  Watchdog task add failed: %d\n", wdt_result);
        }
    } else {
        Serial.printf("   ⚠️  Watchdog init failed: %d\n", wdt_result);
    }
    
    // ===== INITIALIZE LVGL THREAD SAFETY =====
    Serial.println("🔒 Initializing LVGL thread safety...");
    
    // Store UI task handle for assertions
    g_ui_task = xTaskGetCurrentTaskHandle();
    Serial.printf("   ✅ UI task handle: %p\n", g_ui_task);
    
    // Create UI job queue
    g_ui_jobs = xQueueCreate(16, sizeof(UiJob));
    if (!g_ui_jobs) {
        Serial.println("   ❌ Failed to create UI job queue!");
    } else {
        Serial.println("   ✅ UI job queue created (16 slots)");
    }
    
    // Create LVGL recursive mutex
    g_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (!g_lvgl_mutex) {
        Serial.println("   ❌ Failed to create LVGL mutex!");
    } else {
        Serial.println("   ✅ LVGL mutex created");
    }
    
    // Create legacy event queue (to be phased out)
    g_ui_event_queue = xQueueCreate(8, sizeof(ui_event_t));
    if (g_ui_event_queue) {
        Serial.println("   ✅ Legacy event queue created");
    }
    
    Serial.println("🔒 LVGL thread safety initialized!");
    
    // Check LVGL heap (note: lv_mem_monitor doesn't track when using custom allocator)
    lv_mem_monitor_t mem_mon;
    lv_mem_monitor(&mem_mon);
    Serial.printf("📊 LVGL HEAP: total=%u KB, free=%u KB, used=%u KB, frag=%u%%\n",
                  (unsigned)(mem_mon.total_size / 1024),
                  (unsigned)(mem_mon.free_size / 1024),
                  (unsigned)(mem_mon.total_size - mem_mon.free_size) / 1024,
                  (unsigned)mem_mon.frag_pct);
    if (mem_mon.total_size == 0) {
        Serial.println("   ℹ️  LVGL using custom PSRAM allocator (monitoring disabled)");
        Serial.println("   ✅ LVGL heap: PSRAM (effectively unlimited)");
    } else if (mem_mon.free_size < 10000) {
        Serial.println("   ⚠️  WARNING: LVGL heap very low!");
    } else {
        Serial.println("   ✅ LVGL heap healthy");
    }
    
    // Enable direct pixel rendering for galaxy (Strategy A)
    galaxy_set_gfx(gfx);
    
    // Mark boot as complete - allow galaxy rendering in loop
    g_boot_complete = true;
    
    // Play boot sound (if available)
    audio_play_boot_sound();
}

// ============================================
// LOOP
// ============================================

static unsigned long last_perf_check = 0;
static unsigned long loop_count = 0;
static unsigned long total_lv_time = 0;

void loop() {
    // ===== UI JOB DISPATCHER (Process LVGL jobs from other tasks) =====
    // This MUST be FIRST - processes all queued UI work before anything else
    if (g_ui_jobs) {
        UiJob job;
        while (xQueueReceive(g_ui_jobs, &job, 0) == pdTRUE) {
            if (job.fn) {
                job.fn(job.arg);  // Execute UI job on correct thread
            }
        }
    }
    
    // ===== UI EVENT QUEUE (Legacy - will be replaced with ui_post) =====
    if (g_ui_event_queue) {
        ui_event_t evt;
        while (xQueueReceive(g_ui_event_queue, &evt, 0) == pdTRUE) {
            switch (evt.type) {
                case UI_EVENT_UPLOAD_SUCCESS:
                    if (evt.ttsUrl != nullptr && strlen(evt.ttsUrl) > 0) {
                        // Has TTS - transition to DOWNLOADING
                        state_machine_set(STATE_DOWNLOADING);
                        // Download happens later in this loop iteration
                    } else {
                        // No TTS - return to appropriate state
                        if (queue_size() > 0) {
                            Serial.println("   → No TTS, returning to QUEUED");
                            state_machine_set(STATE_QUEUED);
                        } else {
                            Serial.println("   → No TTS, returning to IDLE");
                            state_machine_set(STATE_IDLE);
                            display_manager_set_state(DISPLAY_STATE_IDLE);
                        }
                    }
                    break;
                    
                case UI_EVENT_UPLOAD_FAILED:
                    if (evt.userCancelled) {
                        Serial.println("   → Upload cancelled, returning to IDLE");
                    } else {
                        if (evt.errorMessage) {
                            Serial.printf("   → Upload failed: %s\n", evt.errorMessage);
                        }
                    }
                    state_machine_set(STATE_IDLE);
                    break;
                    
                case UI_EVENT_DOWNLOAD_COMPLETE:
                case UI_EVENT_DOWNLOAD_FAILED:
                    // Handle download events if needed
                    break;
            }
            
            // CRITICAL: Free all allocated strings to prevent memory leaks
            if (evt.ttsUrl) free(evt.ttsUrl);
            if (evt.message) free(evt.message);
            if (evt.command) free(evt.command);
            if (evt.errorMessage) free(evt.errorMessage);
        }
    }
    
    // ===== WIFI UPDATE (Auto-reconnect) =====
    updateWiFi();
    
    // ===== IMU UPDATE (Read sensors) =====
    IMU::update();
    
    // ===== BUTTON UPDATES =====
    button_manager_update();  // Process button events
    
    // ===== AUDIO UPDATES =====
    if (audio_is_recording()) {
        audio_update_recording();  // Stream to SD card
    } else if (audio_is_playing()) {
        audio_update_playback();   // Stream from SD to I2S
    } else {
        audio_keep_codec_warm();   // Flush I2S buffer for instant capture
    }
    
    // ===== LVGL & DISPLAY =====
    unsigned long start = micros();
    
    // Lock LVGL before calling timer handler
    lvgl_lock();
    lv_timer_handler();  // Handle LVGL tasks
    lvgl_unlock();
    
    // Only render galaxy on IDLE screen (not QUEUE) AND after boot is complete
    // Check DISPLAY state, not app state (app can be IDLE but showing queue screen)
    // Render direct pixel animations (after LVGL flush)
    if (g_boot_complete) {
        app_state_t state = state_machine_get();
        display_state_t disp_state = display_manager_get_state();
        
        if (state == STATE_IDLE && disp_state == DISPLAY_STATE_IDLE) {
            galaxy_render_direct(gfx);  // Galaxy stars for IDLE screen
        } 
        // TODO: DISABLED FOR TESTING - Recording ring animation may be causing audio issues
        // else if (state == STATE_RECORDING && disp_state == DISPLAY_STATE_RECORDING) {
        //     recording_ring_render_direct(gfx);  // Recording ring stars
        // }
    }
    
    unsigned long lv_time = micros() - start;
    
    total_lv_time += lv_time;
    loop_count++;
    
    // Heartbeat every 10 seconds (like Device_Rom_Complete)
    if (millis() - last_perf_check >= 10000) {
        float avg_lv_time = total_lv_time / (float)loop_count;
        float theoretical_fps = 1000000.0f / avg_lv_time;
        
        // NOTE: Queue processing is now MANUAL only (via top button click)
        // Auto-retry removed to prevent battery drain during 30s sleep timer
        
        Serial.printf("💓 ");
        Serial.printf("State: %s | ", state_machine_get_name(state_machine_get()));
        
        // WiFi status (with bars and SSID)
        if (isWiFiConnected()) {
            int bars = getWiFiSignalBars();
            Serial.printf("WiFi: %s (%d bars, %d dBm) | ", 
                         getWiFiSSID().c_str(), bars, getWiFiRSSI());
        } else {
            Serial.print("WiFi: ✗ | ");
        }
        
        // Queue status
        if (queue_size() > 0) {
            Serial.printf("Queue: %d | ", queue_size());
        }
        
        // Battery status
        Serial.printf("Bat: %d%% %s(%.2fV) | ",
                     getBatteryPercent(),
                     isCharging() ? "⚡ " : "",
                     getBatteryVoltage());
        
        // IMU data (if available)
        IMUdata acc, gyr;
        if (IMU::getAccelerometer(acc) && IMU::getGyroscope(gyr)) {
            Serial.printf("IMU: A(%.2f,%.2f,%.2f) G(%.1f,%.1f,%.1f) | ",
                         acc.x, acc.y, acc.z, gyr.x, gyr.y, gyr.z);
        }
        
        // Performance
        Serial.printf("FPS: %.1f (%.1fms)\n",
                     theoretical_fps, avg_lv_time / 1000.0f);
        
        // Reset watchdog timer (heartbeat - system is alive)
        esp_task_wdt_reset();
        
        last_perf_check = millis();
        loop_count = 0;
        total_lv_time = 0;
    }
    
    // ===== UPLOAD HANDLER (non-blocking state check) =====
    static bool upload_in_progress = false;
    
    if (state_machine_get() == STATE_UPLOADING && !upload_in_progress) {
        upload_in_progress = true;
        state_machine_update_activity();  // Server communication is activity!
        pending_tts_url = "";  // Clear any previous URL
        pending_response_text = "";  // Clear any previous text
        g_playing_text_ver++;  // Mark as cleared
        pending_response_type = RESPONSE_SAY;  // Clear response type
        g_pending_command = "";  // Clear pending command
        
        // Reset cancel flag
        extern volatile bool g_upload_cancel;
        g_upload_cancel = false;
        
        const char* filename = state_machine_get_recording();
        Serial.printf("📤 Starting upload: %s\n", filename);
        
        // Don't pre-check server - causes SHA allocation failure + wastes time
        // Just attempt upload; connection failure will be caught quickly
        ServerResponse response;
        response.success = false;
        
        {
            // Server is up - attempt upload with retries
            const int MAX_UPLOAD_RETRIES = 2;
            
            for (int attempt = 1; attempt <= MAX_UPLOAD_RETRIES; attempt++) {
                if (attempt > 1) {
                    Serial.printf("   🔄 Retry attempt %d/%d...\n", attempt, MAX_UPLOAD_RETRIES);
                    delay(1000);  // Brief delay between retries
                }
                
                response = transcribeAudio(filename);
                
                // Check if user cancelled
                if (g_upload_cancel) {
                    Serial.println("⚠️  Upload cancelled by user");
                    response.success = false;
                    response.errorMessage = "Cancelled by user";
                    break;
                }
                
                if (response.success) {
                    break;  // Success - exit retry loop
                }
                
                // Check if it's a timeout/stall or connection issue (retryable)
                bool isRetryable = (response.errorMessage.indexOf("timeout") >= 0 || 
                                    response.errorMessage.indexOf("Connection") >= 0 ||
                                    response.errorMessage.indexOf("Failed to send") >= 0);
                
                // HTTP 500 from OpenAI is NOT retryable (it's a valid response - e.g., empty transcription)
                bool isHttp500 = (response.httpCode == 500);
                
                if (isRetryable && !isHttp500) {
                    Serial.printf("   ⚠️  Upload failed: %s\n", response.errorMessage.c_str());
                    if (attempt < MAX_UPLOAD_RETRIES) {
                        Serial.println("   ⏱️  Will retry...");
                    }
                } else {
                    // Non-retryable error (HTTP 400, HTTP 500 from OpenAI, etc.)
                    if (isHttp500) {
                        Serial.println("   ℹ️  HTTP 500 from server (OpenAI response - not retrying)");
                    } else {
                        Serial.printf("   ❌ Non-retryable error: %s\n", response.errorMessage.c_str());
                    }
                    break;
                }
            }
        }
        
        if (response.success) {
            // Upload successful - store conversation data
            Serial.println("✅ Upload successful");
            state_machine_set_conversation_id(response.conversationId.c_str());
            
            // Free recording buffer now that upload is complete
            audio_free_recording_buffer();
            Serial.println("   🧹 Freed recording PSRAM buffer");
            
            // Move file to /processed (upload succeeded, regardless of TTS presence)
            if (filename && strlen(filename) > 0 && strstr(filename, "/queue/") != nullptr) {
                Serial.printf("   📦 Upload successful - moving to processed: %s\n", filename);
                queue_move_to_processed(String(filename));
            }
            
            // Check if we have TTS URL to download
            if (response.ttsUrl.length() > 0) {
                pending_tts_url = response.ttsUrl;  // Store for DOWNLOADING state
                pending_response_text = response.message;  // Store response text for PLAYING display
                pending_transcription = response.transcription;  // Store user's transcription for logging
                g_playing_text_ver++;  // Mark text as dirty/updated
                pending_response_type = response.type;  // Store response type (SAY/ASK/CONFIRM)
                
                // Store command for CONFIRM responses
                if (response.type == RESPONSE_CONFIRM && response.command.length() > 0) {
                    g_pending_command = response.command;
                    Serial.printf("   📋 Command stored: %s\n", g_pending_command.c_str());
                } else {
                    g_pending_command = "";  // Clear for non-CONFIRM responses
                }
                
                // POST EVENT to UI queue (thread-safe - no LVGL calls here)
                ui_event_t evt;
                evt.type = UI_EVENT_UPLOAD_SUCCESS;
                evt.ttsUrl = strdup(response.ttsUrl.c_str());      // Allocate on heap
                evt.message = strdup(response.message.c_str());    // Allocate on heap
                evt.command = strdup(response.command.c_str());    // Allocate on heap
                evt.responseType = (int)response.type;
                evt.errorMessage = nullptr;
                evt.userCancelled = false;
                
                if (g_ui_event_queue) {
                    xQueueSend(g_ui_event_queue, &evt, 0);
                } else {
                    Serial.println("   ⚠️  UI queue not available - direct transition (unsafe!)");
                    state_machine_set(STATE_DOWNLOADING);
                }
            } else {
                // No TTS response - post event to return to appropriate state
                ui_event_t evt;
                evt.type = UI_EVENT_UPLOAD_SUCCESS;
                evt.ttsUrl = nullptr;  // No TTS URL
                evt.message = strdup(response.message.c_str());  // Allocate on heap
                evt.command = nullptr;
                evt.responseType = (int)response.type;
                evt.errorMessage = nullptr;
                evt.userCancelled = false;
                
                if (g_ui_event_queue) {
                    xQueueSend(g_ui_event_queue, &evt, 0);
                } else {
                    // Fallback - direct transition (unsafe but better than nothing)
                    if (queue_size() > 0) {
                        Serial.println("   No TTS audio, returning to QUEUED");
                        state_machine_set(STATE_QUEUED);
                    } else {
                        Serial.println("   No TTS audio, returning to IDLE");
                        state_machine_set(STATE_IDLE);
                        display_manager_set_state(DISPLAY_STATE_IDLE);
                    }
                }
            }
        } else {
            // All retries failed
            Serial.printf("❌ Upload failed after retries: %s\n", response.errorMessage.c_str());
            
            // Check if user cancelled
            if (g_upload_cancel) {
                Serial.println("   → Posting UI event (cancelled by user)");
                audio_free_recording_buffer();
                upload_in_progress = false;
                
                ui_event_t evt;
                evt.type = UI_EVENT_UPLOAD_FAILED;
                evt.ttsUrl = nullptr;
                evt.message = nullptr;
                evt.command = nullptr;
                evt.errorMessage = strdup(response.errorMessage.c_str());  // Allocate on heap
                evt.userCancelled = true;
                evt.responseType = 0;
                
                if (g_ui_event_queue) {
                    xQueueSend(g_ui_event_queue, &evt, 0);
                } else {
                    state_machine_set(STATE_IDLE);
                }
                return;  // Exit early
            }
            
            // Only queue if it's a network/connection issue, NOT if server responded with HTTP 500
            // HTTP 500 from OpenAI means the transcription was processed (even if empty)
            bool shouldQueue = (response.httpCode != 500 && response.httpCode != 400);
            
            if (shouldQueue) {
                // Try to queue recording for later retry
                size_t wavSize = 0;
                uint8_t* wavData = audio_get_recording_wav(&wavSize);
                
                if (wavData && wavSize > 0) {
                    if (queue_enqueue_recording(wavData, wavSize, filename)) {
                        Serial.println("   ✅ Added to queue for later retry");
                        // Free PSRAM buffer now that it's queued (SD-only or copied)
                        audio_free_recording_buffer();
                        Serial.println("   🧹 Freed recording PSRAM buffer");
                    } else {
                        Serial.println("   ❌ Failed to queue recording");
                    }
                }
            } else {
                Serial.printf("   ℹ️  Not queuing (HTTP %d is a valid server response)\n", response.httpCode);
            }
            
            // Return to QUEUED if queue has items, otherwise IDLE
            if (queue_size() > 0) {
                Serial.printf("   📋 Queue still has %d files - returning to QUEUED\n", queue_size());
                state_machine_set(STATE_QUEUED);
            } else {
                Serial.println("   📋 Queue empty - returning to IDLE");
                state_machine_set(STATE_IDLE);
            }
        }
        
        upload_in_progress = false;
    }
    
    // ===== DOWNLOAD HANDLER (non-blocking state check) =====
    static bool download_in_progress = false;
    if (state_machine_get() == STATE_DOWNLOADING && !download_in_progress) {
        download_in_progress = true;
        state_machine_update_activity();  // Server communication is activity!
        
        // Reset cancel flag
        extern volatile bool g_upload_cancel;
        g_upload_cancel = false;
        
        Serial.printf("📥 Starting TTS download from: %s\n", pending_tts_url.c_str());
        
        // Download TTS file from URL (automatically saves to PSRAM or SD based on size)
        bool download_success = (pending_tts_url.length() > 0 && downloadAudio(pending_tts_url.c_str(), nullptr));
        
        // Check if user cancelled
        extern volatile bool g_upload_cancel;
        if (g_upload_cancel) {
            Serial.println("   ⚠️  Download cancelled by user");
            Serial.println("   → Returning to IDLE");
            download_in_progress = false;
            state_machine_set(STATE_IDLE);
        } else if (download_success) {
            Serial.println("   ✅ TTS download complete");
            
            bool playback_started = false;
            
            // Check if TTS was saved to SD card (file >2 MB)
            if (isTtsOnSdCard()) {
                Serial.printf("   💾 Playing large TTS from SD card: %s\n", getTtsSdCardPath());
                Serial.printf("   📊 File size: %.1f MB\n", getTtsSize() / 1024.0 / 1024.0);
                
                if (audio_play_file(getTtsSdCardPath())) {
                    Serial.println("   ✅ TTS playback started (from SD card)");
                    playback_started = true;
                } else {
                    Serial.println("   ❌ Failed to start SD card playback");
                }
            } else {
                // Get TTS buffer and play directly from PSRAM
                size_t ttsSize = 0;
                const uint8_t* ttsBuffer = getTtsBuffer(&ttsSize);
                
                if (ttsBuffer && ttsSize > 0) {
                    Serial.printf("   🧠 Playing TTS from PSRAM: %.1f KB\n", ttsSize / 1024.0);
                    
                    if (audio_play_buffer(ttsBuffer, ttsSize)) {
                        Serial.println("   ✅ TTS playback started (from PSRAM)");
                        playback_started = true;
                    } else {
                        Serial.println("   ❌ Failed to start PSRAM playback");
                    }
                } else {
                    Serial.println("   ❌ TTS buffer empty");
                }
            }
            
            if (playback_started) {
                // Set state based on response type
                app_state_t target_state = STATE_PLAYING;
                if (pending_response_type == RESPONSE_ASK) {
                    target_state = STATE_PLAYING_ASK;
                    Serial.println("   🟡 ASK mode - will auto-record after playback");
                } else if (pending_response_type == RESPONSE_CONFIRM) {
                    target_state = STATE_PLAYING_CONFIRM;
                    Serial.println("   🟣 CONFIRM mode - awaiting button press");
                } else {
                    Serial.println("   🟢 SAY mode - normal playback");
                }
                state_machine_set(target_state);
                
                Serial.printf("   ⏱️  Duration: %d seconds\n", audio_get_playback_duration_sec());
                
                // Text will be updated in next loop iteration after display manager creates screen
            } else {
                Serial.println("   ❌ Failed to start TTS playback");
                if (queue_size() > 0) {
                    state_machine_set(STATE_QUEUED);
                } else {
                    state_machine_set(STATE_IDLE);
                    display_manager_set_state(DISPLAY_STATE_IDLE);  // Update display to show IDLE screen
                }
            }
        } else {
            Serial.println("   ❌ TTS download failed");
            if (queue_size() > 0) {
                state_machine_set(STATE_QUEUED);
            } else {
                state_machine_set(STATE_IDLE);
                display_manager_set_state(DISPLAY_STATE_IDLE);  // Update display to show IDLE screen
            }
        }
        
        pending_tts_url = "";  // Clear URL after use
        download_in_progress = false;
    }
    
    // ===== RECORDING TIMEOUT HANDLER (auto-stop at 60 seconds) =====
    if (state_machine_get() == STATE_RECORDING && !audio_is_recording()) {
        // Recording was auto-stopped (60 second timeout)
        Serial.println("⏱️  Recording timeout (60 seconds)");
        
        // Get filename before transitioning (may be empty if SD not available)
        String filename = audio_get_recording_filename();
        
        if (filename.length() > 0) {
            Serial.printf("   ✅ Recording saved: %s\n", filename.c_str());
        } else {
            Serial.println("   ✅ Recording ready in PSRAM (SD not available)");
        }
        
        // Handle based on origin state (works with or without SD) - QUEUED takes priority!
        if (g_state_before_recording == STATE_QUEUED) {
            // From QUEUE: Add to queue and return to QUEUE
            Serial.println("   → Origin was QUEUE: adding to queue and returning to QUEUE");
            
            // Get recording data
            size_t wavSize = 0;
            uint8_t* wavData = audio_get_recording_wav(&wavSize);
            
            if (wavData && wavSize > 0) {
                if (queue_enqueue_recording(wavData, wavSize, filename.c_str())) {
                    Serial.println("   ✅ Added to queue (new recording at end)");
                    audio_free_recording_buffer();
                    Serial.println("   🧹 Freed recording PSRAM buffer");
                    
                    // Refresh queue display
                    String filenames = queue_get_filenames_list();
                    display_manager_update_queue_filenames(filenames.c_str());
                } else {
                    Serial.println("   ❌ Failed to add to queue");
                }
            }
            
            // Return to QUEUE state
            state_machine_set(STATE_QUEUED);
        } else if (g_state_before_recording == STATE_IDLE ||
                   g_state_before_recording == STATE_PLAYING_ASK ||
                   g_state_before_recording == STATE_PLAYING_CONFIRM) {
            // From IDLE/ASK/CONFIRM: Auto-upload (continue conversation)
            Serial.println("   → Transitioning to UPLOADING (conversation continues)");
            state_machine_set(STATE_UPLOADING);
            state_machine_set_recording(filename.c_str());  // Empty string is OK - upload uses PSRAM
        } else {
            // Fallback: transition to UPLOADING
            Serial.println("   ⚠️  Unknown origin state: defaulting to UPLOADING");
            state_machine_set(STATE_UPLOADING);
            state_machine_set_recording(filename.c_str());  // Empty string is OK - upload uses PSRAM
        }
    }
    
    // ===== PLAYBACK HANDLER (check if finished) =====
    // Handle both STATE_PLAYING (server TTS) and inline playback (QUEUED state)
    const unsigned long SAY_LINGER_TIME = 10000;  // 10 seconds
    
    if (!audio_is_playing()) {
        app_state_t current = state_machine_get();
        
        // STATE_PLAYING: Server TTS finished
        if (current == STATE_PLAYING) {
            // Start linger timer if not already started
            if (say_screen_linger_start == 0) {
                say_screen_linger_start = millis();
                Serial.println("✅ Server TTS (SAY) playback finished");
                Serial.println("   ⏱️  Screen will remain visible for 10 seconds (or until user interaction)");
                
                // Free TTS buffers to recover PSRAM
                freeTtsBuffers();
                
                // Hide queue progress bar if visible
                lv_obj_t* queue_screen = display_manager_get_queue_screen();
                if (queue_screen) {
                    screen_queue_show_progress(queue_screen, false);
                    screen_queue_update_progress(queue_screen, 0);  // Reset to 0%
                }
                
                // File was already moved to /processed after upload success
                // Just clear the recording filename
                state_machine_set_recording("");
                
                // Check if we have a pending reset (from execute command)
                if (pending_conversation_reset) {
                    state_machine_reset_conversation();
                    pending_conversation_reset = false;
                    Serial.println("   🔄 Conversation reset (after execute response played)");
                }
                // NOTE: Don't reset conversation here for normal SAY!
                // User may want to correct/clarify during 10s linger period.
                // Conversation will be reset when linger expires or user cancels.
            }
            
            // Check if linger time has elapsed
            if (millis() - say_screen_linger_start >= SAY_LINGER_TIME) {
                Serial.println("   ⏱️  10 seconds elapsed - transitioning away from PLAYING");
                say_screen_linger_start = 0;  // Reset timer
                
                // Reset conversation (user didn't interact during linger)
                state_machine_reset_conversation();
                Serial.println("   🔄 Conversation reset (SAY linger expired)");
                
                // Return to QUEUED if queue has items, otherwise IDLE
                if (queue_size() > 0) {
                    Serial.printf("   📋 Queue still has %d files - returning to QUEUED\n", queue_size());
                    state_machine_set(STATE_QUEUED);
                } else {
                    Serial.println("   📋 Queue empty - returning to IDLE");
                    state_machine_set(STATE_IDLE);
                }
            }
        }
        // STATE_PLAYING_ASK: Server TTS (ASK) finished - wait for button press
        else if (current == STATE_PLAYING_ASK) {
            static bool ask_logged = false;
            if (!ask_logged) {
                Serial.println("✅ Server TTS (ASK) playback finished - awaiting button press");
                Serial.println("   💡 Hold TOP button to answer, or wait 30s to timeout");
                
                // Free TTS buffers to recover PSRAM
                freeTtsBuffers();
                
                ask_logged = true;
            }
            // Don't change state - wait for top button hold to record answer
            // Timeout is handled separately below
        }
        // STATE_PLAYING_CONFIRM: Stay in state until button press (handled elsewhere)
        else if (current == STATE_PLAYING_CONFIRM) {
            static bool confirm_logged = false;
            if (!confirm_logged) {
                Serial.println("✅ Server TTS (CONFIRM) playback finished - awaiting button press");
                
                // Free TTS buffers to recover PSRAM
                freeTtsBuffers();
                
                confirm_logged = true;
            }
            // Don't change state - wait for top button press to execute command
            // Button handler will call executeCommand() and return to IDLE
        }
        // STATE_QUEUED: Inline preview finished
        else if (current == STATE_QUEUED) {
            static bool was_playing_inline = false;
            const char* filename = state_machine_get_recording();
            
            // Only handle if we were actually playing something
            if (filename && strlen(filename) > 0) {
                if (!was_playing_inline) {
                    Serial.println("✅ Inline preview playback finished");
                    Serial.println("   → File NOT moved (preview only)");
                    
                    // Restore NVS volume setting after queue playback
                    int nvs_volume = settings_manager_get_volume();
                    audio_set_volume(nvs_volume);
                    Serial.printf("   🔊 Restored NVS volume: %d\n", nvs_volume);
                    
                    was_playing_inline = true;  // Prevent log spam
                }
                
                // Check if auto-advance is enabled (sequential playback)
                if (g_auto_advance_playback) {
                    auto items = queue_get_items_info();
                    g_current_playback_index++;  // Move to next file
                    
                    if (g_current_playback_index <= (int)items.size()) {
                        // More files to play - start next one
                        String next_filename = items[g_current_playback_index - 1].filename;
                        Serial.printf("   → Auto-advancing to file %d: %s\n", g_current_playback_index, next_filename.c_str());
                        
                        // Update queue screen selection to highlight current file
                        lv_obj_t* queue_screen = display_manager_get_queue_screen();
                        if (queue_screen) {
                            extern void screen_queue_set_selected_index(lv_obj_t *screen_obj, int index);
                            screen_queue_set_selected_index(queue_screen, g_current_playback_index);
                            screen_queue_update_progress(queue_screen, 0);  // Reset progress
                        }
                        
                        // Start playing next file
                        if (audio_play_file(next_filename.c_str())) {
                            state_machine_set_recording(next_filename.c_str());
                            was_playing_inline = false;  // Reset for next file
                        } else {
                            // Failed to play - disable auto-advance
                            g_auto_advance_playback = false;
                            Serial.println("   ❌ Failed to play next file - auto-advance disabled");
                        }
                    } else {
                        // All files played - disable auto-advance
                        g_auto_advance_playback = false;
                        Serial.println("   ✅ All files played - auto-advance complete");
                        
                        // Restore NVS volume setting
                        int nvs_volume = settings_manager_get_volume();
                        audio_set_volume(nvs_volume);
                        Serial.printf("   🔊 Restored NVS volume: %d\n", nvs_volume);
                        
                        // Hide progress bar and reset play button
                        lv_obj_t* queue_screen = display_manager_get_queue_screen();
                        if (queue_screen) {
                            screen_queue_show_progress(queue_screen, false);
                            screen_queue_update_progress(queue_screen, 0);
                            screen_queue_reset_play_button(queue_screen);
                        }
                        state_machine_set_recording("");
                    }
                } else {
                    // Not auto-advance - just hide progress bar and reset
                    lv_obj_t* queue_screen = display_manager_get_queue_screen();
                    if (queue_screen) {
                        screen_queue_show_progress(queue_screen, false);
                        screen_queue_update_progress(queue_screen, 0);
                        screen_queue_reset_play_button(queue_screen);  // Reset to play icon
                    }
                    state_machine_set_recording("");
                }
            } else {
                was_playing_inline = false;  // Reset flag when not playing
            }
        }
        // Any other state - reset SAY linger timer
        else {
            if (say_screen_linger_start != 0) {
                say_screen_linger_start = 0;  // Reset timer when not in PLAYING states
            }
        }
    }
    
    // Reset log flags when leaving CONFIRM/ASK states
    {
        static app_state_t last_confirm_state = STATE_IDLE;
        static app_state_t last_ask_state = STATE_IDLE;
        app_state_t curr = state_machine_get();
        
        if (last_confirm_state == STATE_PLAYING_CONFIRM && curr != STATE_PLAYING_CONFIRM) {
            // Force reset of static flag in CONFIRM handler (hacky but works for C++)
            // The flag will be re-initialized to false next time we enter CONFIRM state
        }
        if (last_ask_state == STATE_PLAYING_ASK && curr != STATE_PLAYING_ASK) {
            // Force reset of static flag in ASK handler
        }
        
        last_confirm_state = curr;
        last_ask_state = curr;
    }
    
    // ===== PLAYING_ASK TIMEOUT (30 seconds after playback finishes) =====
    static unsigned long ask_timeout_start = 0;
    if (state_machine_get() == STATE_PLAYING_ASK) {
        // Start timeout when we first enter this waiting state
        if (!audio_is_playing() && ask_timeout_start == 0) {
            ask_timeout_start = millis();
        }
        
        // Check if 30 seconds have elapsed
        if (ask_timeout_start > 0 && (millis() - ask_timeout_start >= 30000)) {
            Serial.println("⏱️  ASK timeout (30 seconds) - returning to IDLE");
            
            // Reset conversation (user didn't answer)
            state_machine_reset_conversation();
            Serial.println("   🔄 Conversation reset (timeout)");
            
            // Return to appropriate state
            if (queue_size() > 0) {
                state_machine_set(STATE_QUEUED);
            } else {
                state_machine_set(STATE_IDLE);
            }
            
            ask_timeout_start = 0;  // Reset timeout
        }
    } else {
        ask_timeout_start = 0;  // Reset timeout when leaving state
    }
    
    // ===== UPDATE PLAYING SCREEN TEXT (versioned event-driven update) =====
    app_state_t current = state_machine_get();
    
    // Log transcription and state on entry to PLAYING
    static app_state_t last_logged_state = STATE_IDLE;
    if (current != last_logged_state && (current == STATE_PLAYING || current == STATE_PLAYING_ASK || current == STATE_PLAYING_CONFIRM)) {
        // Log user's transcription first
        if (pending_transcription.length() > 0) {
            Serial.printf("💬 User said: %s\n", pending_transcription.c_str());
        }
        last_logged_state = current;
    } else if (current != last_logged_state) {
        last_logged_state = current;
    }
    
    // Apply text once per version when in any PLAYING state
    if ((current == STATE_PLAYING || current == STATE_PLAYING_ASK || current == STATE_PLAYING_CONFIRM) && 
        g_playing_text_ver != g_playing_text_applied) {
        lv_obj_t* playing_screen = display_manager_get_playing_screen();
        if (playing_screen && pending_response_text.length() > 0) {
            screen_playing_update_text(playing_screen, pending_response_text.c_str());
            Serial.printf("📝 AI response: %s\n", pending_response_text.c_str());
            g_playing_text_applied = g_playing_text_ver;  // Mark as applied
        } else if (!playing_screen) {
            Serial.println("   ⚠️  Playing screen not ready yet, will retry next frame");
        } else if (pending_response_text.length() == 0) {
            Serial.println("   ⚠️  No pending text to display!");
        }
    }
    
    // Force re-apply on next entry when leaving playing states
    if (!(current == STATE_PLAYING || current == STATE_PLAYING_ASK || current == STATE_PLAYING_CONFIRM)) {
        g_playing_text_applied = 0;  // Reset so next entry will trigger update
    }
    
    // ===== UPDATE PLAYBACK TIMER (Every 100ms) =====
    // Works for STATE_PLAYING/ASK/CONFIRM (server TTS) and QUEUED (inline preview)
    static unsigned long last_playback_update = 0;
    if (audio_is_playing()) {
        if (millis() - last_playback_update >= 100) {
            last_playback_update = millis();
            int time_data[2];
            time_data[0] = audio_get_playback_position_sec();  // Current position
            time_data[1] = audio_get_playback_duration_sec();  // Total duration
            display_manager_update_data(time_data);
            
            // Update queue screen progress bar if in QUEUED state (inline preview)
            if (current == STATE_QUEUED) {
                const char* filename = state_machine_get_recording();
                if (filename && strstr(filename, "/queue/") != nullptr) {
                    // Calculate progress percentage
                    int percent = 0;
                    if (time_data[1] > 0) {
                        percent = (time_data[0] * 100) / time_data[1];
                    }
                    
                    // Update queue screen progress bar
                    lv_obj_t* queue_screen = display_manager_get_queue_screen();
                    if (queue_screen) {
                        screen_queue_update_progress(queue_screen, percent);
                        screen_queue_show_progress(queue_screen, true);
                    }
                }
            }
            
            // Playback is activity! Update timestamp to prevent sleep
            state_machine_update_activity();
        }
    }
    
    // ===== UPDATE GALAXY IMU PARALLAX (Every frame when galaxy visible) =====
    // Only update when in IDLE state (galaxy is visible)
    static bool imu_parallax_logged = false;
    if (state_machine_get() == STATE_IDLE) {
        IMUdata acc, gyr;
        if (IMU::getAccelerometer(acc) && IMU::getGyroscope(gyr)) {
            // Apply IMU-based 3D parallax effect to galaxy
            galaxy_set_imu_offset(acc.x, acc.y, acc.z);
            
            // Log once that IMU parallax is active
            if (!imu_parallax_logged) {
                Serial.println("🌌 IMU parallax effect ACTIVE (will log every ~1 second)");
                imu_parallax_logged = true;
            }
        } else if (!imu_parallax_logged) {
            Serial.println("⚠️  IMU parallax: Failed to read IMU data!");
            imu_parallax_logged = true;
        }
    } else {
        imu_parallax_logged = false;  // Reset when leaving IDLE
    }
    
    // ===== SLEEP MANAGER UPDATE (inactivity, shake, clock) =====
    sleep_manager_update();
    
    delay(5);  // Factory ROM has this - reduces CPU to ~50% and improves stability
    
    // ===== UPDATE STATUS BAR (Every 2 seconds) =====
    static unsigned long last_status_update = 0;
    if (millis() - last_status_update >= 2000) {
        last_status_update = millis();
        
        // Read real sensor data
        idle_status_t status;
        status.battery_percent = getBatteryPercent();
        status.battery_charging = isCharging();
        status.wifi_connected = isWiFiConnected();
        status.wifi_bars = getWiFiSignalBars();
        
        // Update display
        display_manager_update_status(&status);
        
        // Status updates now integrated into heartbeat only
    }
    
    // ===== UPDATE RECORDING TIMER (Every 100ms) =====
    static unsigned long last_recording_update = 0;
    if (state_machine_get() == STATE_RECORDING) {
        if (millis() - last_recording_update >= 100) {
            last_recording_update = millis();
            int seconds = audio_get_recording_seconds();
            display_manager_update_data(&seconds);
        }
        
        // TODO: DISABLED FOR TESTING - Update recording ring audio level (every frame for smooth animation)
        // float audioLevel = audio_get_recording_level();
        // recording_ring_set_audio(audioLevel);
    } else {
        last_recording_update = 0;  // Reset when not recording
    }
    
    // No delay - LVGL timer handler paces itself based on LV_DISP_DEF_REFR_PERIOD
}

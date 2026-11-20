#pragma once

#include <Arduino.h>

// ============================================
// APPLICATION STATE MACHINE
// ============================================

// Application states
typedef enum {
    STATE_IDLE,              // Waiting for input
    STATE_RECORDING,         // Recording audio
    STATE_UPLOADING,         // Sending to server
    STATE_DOWNLOADING,       // Getting TTS response
    STATE_PLAYING,           // Playing audio response (SAY type - no interaction)
    STATE_PLAYING_ASK,       // Playing audio response (ASK type - wait for audio)
    STATE_PLAYING_CONFIRM,   // Playing audio response (CONFIRM type - wait for button)
    STATE_QUEUED,            // Offline, recordings queued
    STATE_SETTINGS,          // Settings menu
    STATE_SLEEP              // Low power sleep mode
} app_state_t;

// State change callback function type
typedef void (*state_change_callback_t)(app_state_t old_state, app_state_t new_state);

/**
 * Initialize state machine
 * @param callback Function to call when state changes (optional)
 */
void state_machine_init(state_change_callback_t callback = nullptr);

/**
 * Change application state
 * @param new_state State to transition to
 */
void state_machine_set(app_state_t new_state);

/**
 * Get current state
 * @return Current application state
 */
app_state_t state_machine_get();

/**
 * Get previous state
 * @return Previous application state
 */
app_state_t state_machine_get_previous();

/**
 * Check if currently in IDLE state
 * @return true if in IDLE
 */
bool state_machine_is_idle();

/**
 * Check if currently recording
 * @return true if in RECORDING state
 */
bool state_machine_is_recording();

/**
 * Check if recording is allowed in current state
 * @return true if can start recording
 */
bool state_machine_can_record();

/**
 * Get string representation of state
 * @param state State to convert
 * @return State name as string
 */
const char* state_machine_get_name(app_state_t state);

/**
 * Update last activity timestamp (for inactivity timeout)
 */
void state_machine_update_activity();

/**
 * Check if device has been inactive for given duration
 * @param timeout_ms Timeout in milliseconds
 * @return true if inactive for longer than timeout
 */
bool state_machine_is_inactive(unsigned long timeout_ms);

/**
 * Reset conversation state (clear IDs, etc.)
 */
void state_machine_reset_conversation();

/**
 * Get time elapsed in current state (ms)
 * @return Milliseconds since state was entered
 */
unsigned long state_machine_get_elapsed();

// ============================================
// CONVERSATION STATE (for HTTPS integration)
// ============================================

/**
 * Set current conversation ID
 */
void state_machine_set_conversation_id(const char* id);

/**
 * Get current conversation ID
 */
const char* state_machine_get_conversation_id();

/**
 * Set current recording filename
 */
void state_machine_set_recording(const char* filename);

/**
 * Get current recording filename
 */
const char* state_machine_get_recording();

/**
 * Set current response filename
 */
void state_machine_set_response(const char* filename);

/**
 * Get current response filename
 */
const char* state_machine_get_response();

#include "state_machine.h"

// ============================================
// STATE MACHINE - Implementation
// ============================================

// Internal state structure
typedef struct {
    app_state_t current_state;
    app_state_t previous_state;
    unsigned long state_start_time;
    unsigned long last_activity_time;
    state_change_callback_t callback;
    
    // Conversation tracking
    char conversation_id[64];
    char current_recording[128];
    char current_response[128];
} state_machine_data_t;

// Global state (zero-initialized, properly set in init function)
static state_machine_data_t state = {
    STATE_IDLE,     // current_state
    STATE_IDLE,     // previous_state
    0,              // state_start_time
    0,              // last_activity_time
    nullptr,        // callback
    {},             // conversation_id (zero-initialized array)
    {},             // current_recording (zero-initialized array)
    {}              // current_response (zero-initialized array)
};

void state_machine_init(state_change_callback_t callback) {
    Serial.println("\n🔄 Initializing State Machine...");
    
    state.current_state = STATE_IDLE;
    state.previous_state = STATE_IDLE;
    state.state_start_time = millis();
    state.last_activity_time = millis();
    state.callback = callback;
    
    state.conversation_id[0] = '\0';
    state.current_recording[0] = '\0';
    state.current_response[0] = '\0';
    
    Serial.println("✅ State Machine initialized: IDLE");
}

void state_machine_set(app_state_t new_state) {
    if (state.current_state == new_state) {
        return;  // No change
    }
    
    app_state_t old_state = state.current_state;
    state.previous_state = state.current_state;
    state.current_state = new_state;
    state.state_start_time = millis();
    
    Serial.printf("🔄 State: %s → %s\n",
                  state_machine_get_name(old_state),
                  state_machine_get_name(new_state));
    
    // Call callback if registered
    if (state.callback) {
        state.callback(old_state, new_state);
    }
}

app_state_t state_machine_get() {
    return state.current_state;
}

app_state_t state_machine_get_previous() {
    return state.previous_state;
}

bool state_machine_is_idle() {
    return state.current_state == STATE_IDLE;
}

bool state_machine_is_recording() {
    return state.current_state == STATE_RECORDING;
}

bool state_machine_can_record() {
    // Can start recording from IDLE, QUEUED, or PLAYING states
    // ASK state waits for audio response
    // CONFIRM state waits for button, but can also accept audio correction
    return (state.current_state == STATE_IDLE || 
            state.current_state == STATE_QUEUED ||
            state.current_state == STATE_PLAYING ||
            state.current_state == STATE_PLAYING_ASK ||
            state.current_state == STATE_PLAYING_CONFIRM);
}

const char* state_machine_get_name(app_state_t s) {
    switch (s) {
        case STATE_IDLE:            return "IDLE";
        case STATE_RECORDING:       return "RECORDING";
        case STATE_UPLOADING:       return "UPLOADING";
        case STATE_DOWNLOADING:     return "DOWNLOADING";
        case STATE_PLAYING:         return "PLAYING";
        case STATE_PLAYING_ASK:     return "PLAYING_ASK";
        case STATE_PLAYING_CONFIRM: return "PLAYING_CONFIRM";
        case STATE_QUEUED:          return "QUEUED";
        case STATE_SETTINGS:        return "SETTINGS";
        case STATE_SLEEP:           return "SLEEP";
        default:                    return "UNKNOWN";
    }
}

void state_machine_update_activity() {
    state.last_activity_time = millis();
}

bool state_machine_is_inactive(unsigned long timeout_ms) {
    return (millis() - state.last_activity_time) > timeout_ms;
}

void state_machine_reset_conversation() {
    Serial.println("🔄 Resetting conversation...");
    
    state.conversation_id[0] = '\0';
    state.current_recording[0] = '\0';
    state.current_response[0] = '\0';
    
    // Don't force state transition - let caller decide next state
    // (This was causing booking confirmations to skip IDLE and lose text)
    
    Serial.println("✅ Conversation reset");
}

unsigned long state_machine_get_elapsed() {
    return millis() - state.state_start_time;
}

// ============================================
// CONVERSATION STATE MANAGEMENT
// ============================================

void state_machine_set_conversation_id(const char* id) {
    if (id) {
        strncpy(state.conversation_id, id, sizeof(state.conversation_id) - 1);
        state.conversation_id[sizeof(state.conversation_id) - 1] = '\0';
    }
}

const char* state_machine_get_conversation_id() {
    return state.conversation_id;
}

void state_machine_set_recording(const char* filename) {
    if (filename) {
        strncpy(state.current_recording, filename, sizeof(state.current_recording) - 1);
        state.current_recording[sizeof(state.current_recording) - 1] = '\0';
    }
}

const char* state_machine_get_recording() {
    return state.current_recording;
}

void state_machine_set_response(const char* filename) {
    if (filename) {
        strncpy(state.current_response, filename, sizeof(state.current_response) - 1);
        state.current_response[sizeof(state.current_response) - 1] = '\0';
    }
}

const char* state_machine_get_response() {
    return state.current_response;
}

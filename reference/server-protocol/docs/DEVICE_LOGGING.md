# Device Logging System

## Overview

The Voice Assistant Server includes comprehensive per-device logging that captures the complete conversation flow, including:

- **User transcriptions** - What the user said
- **LLM prompts** - Complete prompts sent to OpenAI
- **LLM responses** - Complete responses from OpenAI
- **TTS text** - What was sent back to the device
- **API calls** - Phoneshop API interactions
- **Errors** - Any issues that occurred

## Log File Location

Device logs are stored in: `VoiceAssistantServer/logs/`

**Format:** `device_<MAC-ADDRESS>_<DATE>.log`

**Example:** `device_CC-BA-97-11-A8-00_2025-11-07.log`

## Log Format

```
YYYY-MM-DD HH:MM:SS | LEVEL    | [conversation_id] MESSAGE
```

## Log Entries

### 1. User Transcription
```
2025-11-07 21:45:30 | INFO     | [abc123] REQUEST
2025-11-07 21:45:30 | INFO     | [abc123] Transcription: iPhone 7 broken screen
```

### 2. LLM Prompt (Complete)
```
2025-11-07 21:45:31 | INFO     | [abc123] LLM_PROMPT (EXTRACT_BOOK_PARAMS)
2025-11-07 21:45:31 | INFO     | [abc123] LLM_MODEL: gpt-4o-mini
2025-11-07 21:45:31 | INFO     | [abc123] LLM_MESSAGES:
2025-11-07 21:45:31 | INFO     | [abc123]   Message 1 [system]: Extract structured repair data. Be precise and concise...
2025-11-07 21:45:31 | INFO     | [abc123]   Message 2 [user]: Extract device repair parameters from this text: "iPhone 7 broken screen"...
```

### 3. LLM Response (Complete)
```
2025-11-07 21:45:33 | INFO     | [abc123] LLM_RESPONSE (EXTRACT_BOOK_PARAMS)
2025-11-07 21:45:33 | INFO     | [abc123] LLM_DATA: {'brand': 'Apple', 'model': 'iPhone 7', 'problem': 'Broken Screen', ...}
```

### 4. TTS Text (Sent to Device)
```
2025-11-07 21:45:34 | INFO     | [abc123] TTS_TEXT: Booking repair for Apple iPhone 7 with Broken Screen. Total price £75. Confirm?
2025-11-07 21:45:34 | INFO     | [abc123] TTS_FILE: tts_abc123_20251107_214534.wav
```

### 5. Conversation Flow
```
2025-11-07 21:45:35 | INFO     | [abc123] FLOW: CONFIRM 
```

### 6. API Calls
```
2025-11-07 21:45:36 | INFO     | [abc123] API_CALL: /YOUR_SHOP_API_PATH
2025-11-07 21:45:36 | INFO     | [abc123] API_PARAMS: {'brand': 'Apple', 'model': 'iPhone 7', ...}
2025-11-07 21:45:37 | INFO     | [abc123] API_RESPONSE: 200
2025-11-07 21:45:37 | INFO     | [abc123] API_DATA: {'ticket_id': '12345'}
```

### 7. Errors
```
2025-11-07 21:45:38 | ERROR    | [abc123] ERROR: CREDENTIALS_MISSING - Device not configured with Phoneshop credentials
```

## Complete Conversation Example

```
2025-11-07 21:45:30 | INFO     | [abc123] REQUEST
2025-11-07 21:45:30 | INFO     | [abc123] Transcription: iPhone 7 broken screen
2025-11-07 21:45:30 | INFO     | [abc123] Parsed: {}

2025-11-07 21:45:31 | INFO     | [abc123] LLM_PROMPT (EXTRACT_BOOK_PARAMS)
2025-11-07 21:45:31 | INFO     | [abc123] LLM_MODEL: gpt-4o-mini
2025-11-07 21:45:31 | INFO     | [abc123] LLM_MESSAGES:
2025-11-07 21:45:31 | INFO     | [abc123]   Message 1 [system]: Extract structured repair data...
2025-11-07 21:45:31 | INFO     | [abc123]   Message 2 [user]: Extract device repair parameters from this text: "iPhone 7 broken screen"...

2025-11-07 21:45:33 | INFO     | [abc123] LLM_RESPONSE (EXTRACT_BOOK_PARAMS)
2025-11-07 21:45:33 | INFO     | [abc123] LLM_DATA: {'brand': 'Apple', 'model': 'iPhone 7', 'problem': 'Broken Screen', 'deviceCondition': null, 'accessories': null, 'totalPrice': null, 'deposit': null, 'devicePassword': null, 'notes': null}

2025-11-07 21:45:34 | INFO     | [abc123] FLOW: CONFIRM 
2025-11-07 21:45:34 | INFO     | [abc123] TTS_TEXT: Booking repair for Apple iPhone 7 with Broken Screen. Total price £75. Confirm?
2025-11-07 21:45:34 | INFO     | [abc123] TTS_FILE: tts_abc123_20251107_214534.wav

2025-11-07 21:45:35 | INFO     | [abc123] REQUEST
2025-11-07 21:45:35 | INFO     | [abc123] Transcription: yes
2025-11-07 21:45:35 | INFO     | [abc123] Parsed: {}

2025-11-07 21:45:36 | INFO     | [abc123] EXECUTE: book
2025-11-07 21:45:36 | INFO     | [abc123] API_CALL: /YOUR_SHOP_API_PATH
2025-11-07 21:45:36 | INFO     | [abc123] API_PARAMS: {'brand': 'Apple', 'model': 'iPhone 7', 'problem': 'Broken Screen', 'deviceCondition': '', 'accessories': '', 'totalPrice': '75', 'deposit': '', 'devicePassword': '', 'notes': ''}
2025-11-07 21:45:37 | INFO     | [abc123] API_RESPONSE: 200
2025-11-07 21:45:37 | INFO     | [abc123] API_DATA: {'ticket_id': '12345'}

2025-11-07 21:45:37 | INFO     | [abc123] FLOW: SAY 
2025-11-07 21:45:37 | INFO     | [abc123] TTS_TEXT: Ticket created successfully! Your ticket number is 12345.
2025-11-07 21:45:37 | INFO     | [abc123] TTS_FILE: tts_abc123_20251107_214537.wav
```

## LLM Prompt Types

### EXTRACT_BOOK_PARAMS
Extracts repair ticket parameters from natural language.

### EXTRACT_SELL_PARAMS
Extracts device sale parameters from natural language.

### ANALYZE_UNKNOWN_COMMAND
Analyzes unclear commands and asks clarifying questions.

## Benefits

### For Debugging
- See exactly what the LLM received and returned
- Trace conversation flow step-by-step
- Identify where things went wrong

### For Improvement
- Analyze failed command parsing
- Improve LLM prompts based on real interactions
- Identify common user patterns

### For Compliance
- Complete audit trail of all interactions
- Track API calls and responses
- Monitor device usage

## Daily Rotation

Log files are created daily per device:
- `device_CC-BA-97-11-A8-00_2025-11-07.log`
- `device_CC-BA-97-11-A8-00_2025-11-08.log`
- etc.

This prevents log files from growing too large and makes analysis easier.

## Implementation

### Device Logger (`device_logger.py`)
Core logging service that creates per-device log files.

### LLM Parser (`llm_parser.py`)
Logs all LLM prompts and responses automatically.

### Server (`server.py`)
Coordinates logging throughout the request lifecycle.

### Command Parser (`command_parser.py`)
Passes device context to enable logging in LLM calls.

## Usage

Logs are written automatically - no special configuration needed!

Just use the Voice Assistant normally, and complete logs will be generated in the `logs/` directory.

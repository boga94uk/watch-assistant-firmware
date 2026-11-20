"""
Main Flask server for Voice Assistant.
Handles audio upload, transcription, command parsing, and execution.
"""
import os
import json
import logging
import threading
import time
import secrets
import re
import yaml
from datetime import datetime, timedelta
from pathlib import Path
from flask import Flask, request, jsonify, send_file, render_template, session, redirect, url_for
from werkzeug.utils import secure_filename
from functools import wraps

from utils import load_config, load_commands, load_env, setup_logging, get_audio_temp_dir, sanitize_for_display
from transcription import TranscriptionService
from command_parser import CommandParser
from conversation import ConversationManager
from llm_parser import LLMParser
from llm_agent import LLMAgent
from unified_command_extractor import UnifiedCommandExtractor
from tts_service import TTSService
from device_manager import DeviceManager
from auth_middleware import require_device_auth
from phoneshop_api import PhoneshopAPIClient, print_pdf_windows
from device_logger import DeviceLogger
from invoice_cleanup import InvoiceCleanup

# Load configuration
load_env()
config = load_config()
commands_config = load_commands()
logger = setup_logging(config)

# Load help context
def load_help_context():
    """Load help.md context for LLM"""
    help_path = os.path.join(os.path.dirname(__file__), '..', 'config', 'help.md')
    try:
        with open(help_path, 'r', encoding='utf-8') as f:
            return f.read()
    except Exception as e:
        logger.warning(f"Could not load help.md: {e}")
        return "You are a helpful phone shop assistant. Answer questions briefly and clearly."

def load_api_schema():
    """Load LLM API schema for agentic behavior"""
    schema_path = os.path.join(os.path.dirname(__file__), '..', 'llm_api_schema.json')
    try:
        with open(schema_path, 'r', encoding='utf-8') as f:
            return json.load(f)
    except Exception as e:
        logger.warning(f"Could not load llm_api_schema.json: {e}")

def create_response(data: dict):
    """Create JSON response with sanitized message field for ESP32 display"""
    if 'message' in data and data['message']:
        data['message'] = sanitize_for_display(data['message'])
    return jsonify(data)

help_context = load_help_context()
api_schema = load_api_schema()

# Initialize Flask app
app = Flask(__name__, template_folder='templates')
app.config['MAX_CONTENT_LENGTH'] = 16 * 1024 * 1024  # 16MB max file size
app.config['SECRET_KEY'] = os.getenv('SECRET_KEY', secrets.token_hex(32))

# Initialize device logger first (needed by other services)
device_logger = DeviceLogger()

# Initialize services
audio_temp_dir = get_audio_temp_dir()
transcription_service = TranscriptionService(config, os.getenv('OPENAI_API_KEY'))
llm_parser = LLMParser(config, os.getenv('OPENAI_API_KEY'), device_logger)  # Pass device_logger
command_parser = CommandParser(commands_config, llm_parser)
conversation_manager = ConversationManager()
tts_service = TTSService(config, os.getenv('OPENAI_API_KEY'), audio_temp_dir)
device_manager = DeviceManager()

# Initialize Unified Command Extractor (new architecture)
config_dir = Path(__file__).parent.parent / 'config'
openai_config = {
    'api_key': os.getenv('OPENAI_API_KEY'),
    'model': config.get('openai', {}).get('model', 'gpt-4o-mini'),
    'max_tokens': config.get('openai', {}).get('max_tokens', 500),
    'temperature': config.get('openai', {}).get('temperature', 0.7)
}
unified_extractor = UnifiedCommandExtractor(openai_config, config_dir)

# Initialize Phoneshop API
phoneshop_api = PhoneshopAPIClient()

# Initialize LLM Agent (for conversational API access)
llm_agent = None
if api_schema:
    openai_config = {
        'api_key': os.getenv('OPENAI_API_KEY'),
        'model': config.get('openai', {}).get('model', 'gpt-4o-mini'),
        'max_tokens': config.get('openai', {}).get('max_tokens', 500),
        'temperature': config.get('openai', {}).get('temperature', 0.7)
    }
    llm_agent = LLMAgent(api_schema, phoneshop_api, openai_config)
    logger.info("LLM Agent initialized with API schema")
else:
    logger.warning("LLM Agent not initialized - API schema not loaded")

# Initialize invoice directory and cleanup
invoice_dir = os.path.join(os.path.dirname(__file__), '..', 'invoices')
os.makedirs(invoice_dir, exist_ok=True)
invoice_cleanup = InvoiceCleanup(invoice_dir, max_age_days=7, check_interval_hours=24)
invoice_cleanup.start()

logger.info("Voice Assistant Server initialized (with Phoneshop API + device logging + LLM Agent)")

# Brute-force protection for admin login
admin_login_attempts = {
    'failed_count': 0,
    'locked_until': None
}

# Authentication decorator
def require_admin(f):
    @wraps(f)
    def decorated_function(*args, **kwargs):
        token = request.headers.get('Admin-Token') or session.get('admin_token')
        if not token or token != session.get('admin_token'):
            return jsonify({'error': 'Unauthorized'}), 401
        return f(*args, **kwargs)
    return decorated_function

# Background cleanup task
def cleanup_task():
    """Background task to clean up expired conversations and temporary TTS files"""
    while True:
        time.sleep(3600)  # Run every hour
        try:
            conversation_manager.cleanup_expired()
            tts_service.cleanup_temp_files()  # Clean up uncached TTS files
            logger.info("🧹 Background cleanup: Expired conversations & temp files removed")
        except Exception as e:
            logger.error(f"Cleanup task error: {e}")

# Start cleanup thread
cleanup_thread = threading.Thread(target=cleanup_task, daemon=True)
cleanup_thread.start()
logger.info("Background cleanup task started (conversations only - TTS uses size-based cache)")

@app.route('/health', methods=['GET'])
def health():
    """Health check endpoint"""
    return jsonify({
        'status': 'ok',
        'version': '1.0.0'
    })

@app.route('/transcribe', methods=['POST'])
@require_device_auth(device_manager)
def transcribe():
    """
    Transcribe audio and parse command.
    
    Expected: multipart/form-data with 'audio' file
    Optional: 'conversation_id' header for multi-turn conversations
    """
    try:
        # Device authentication handled by @require_device_auth decorator
        # Device info is attached to request.device by the middleware
        device_id = request.device['id']
        device_info = request.device['info']
        logger.info(f"Request from device {device_id} ({device_info.get('staff_name')})")
        
        logger.info(f"🔍 DEBUG: Content-Type: {request.content_type}")
        logger.info(f"🔍 DEBUG: Content-Length: {request.content_length}")
        logger.info(f"🔍 DEBUG: Checking for audio in request.files...")
        
        # Get audio file with fallback for testing
        if 'audio' not in request.files:
            logger.warning("No audio form-data part from device, returning mock for now")
            return jsonify({
                "audio_url": "https://your-server.example.com/audio/test_audio.wav",
                "message": "Mock response because no audio was parsed",
                "transcription": "test input",
                "type": "unknown"
            }), 200
        
        audio_file = request.files['audio']
        if audio_file.filename == '':
            return jsonify({'error': 'Empty filename'}), 400
        
        # Save audio temporarily
        filename = secure_filename(audio_file.filename)
        audio_path = os.path.join(get_audio_temp_dir(), filename)
        audio_file.save(audio_path)
        
        logger.info(f"Received audio file: {filename}")
        
        # Transcribe
        text = transcription_service.transcribe(audio_path)
        if not text:
            logger.warning("⚠️ Transcription failed, returning mock response for testing")
            # For testing with fake audio, return a mock successful response
            return create_response({
                'type': 'unknown',
                'transcription': '[mock - transcription failed]',
                'message': 'Mock response: transcription service unavailable or audio invalid',
                'audio_url': None
            }), 200
        
        logger.info(f"📝 TRANSCRIPTION: '{text}'")  # Enhanced logging
        
        # Check for conversation continuation (header or form field)
        conv_id = request.headers.get('Conversation-ID') or request.form.get('conversation_id')
        if conv_id:
            logger.info(f"📨 Received conversation_id: {conv_id}")
        
        # Check if new command keyword should reset context
        if conv_id and conversation_manager.should_reset_context(text, conv_id):
            logger.info(f"🔄 New command keyword detected - cancelling conversation {conv_id}")
            conversation_manager.complete(conv_id)
            conv_id = None  # Reset to treat as new command
        
        # Log to device-specific log (use conv_id or generate temp one for logging)
        log_conv_id = conv_id or f"new_{datetime.now().strftime('%H%M%S')}"
        device_logger.log_request(device_id, log_conv_id, text, {})
        
        if conv_id:
            # Continue existing conversation
            conv = conversation_manager.get(conv_id)
            if not conv:
                # Conversation doesn't exist (timed out or deleted)
                logger.warning(f"⚠️  Conversation {conv_id} not found - treating as new request")
                conv_id = None  # Reset to process as new conversation
            
            if conv:
                conv_type = conv.get('type', 'ask')  # Default to 'ask' for backwards compat
                
                # Handle based on conversation type
                if conv_type == 'ask':
                    # User responded to a question
                    # Check question limit (max 3)
                    question_count = conv.get('question_count', 0)
                    
                    if question_count >= 3:
                        # Max questions reached - give up
                        message = "I'm having trouble understanding. Please try using a command format."
                        logger.info(f"⚠️  Max questions (3) reached, ending conversation")
                        audio_file = tts_service.generate(message)
                        audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                        
                        conversation_manager.complete(conv_id)
                        
                        return create_response({
                            'type': 'say',
                            'transcription': text,
                            'message': message,
                            'audio_url': audio_url
                        })
                    
                    # Use LLM to analyze response in context
                    # Get ALL valid commands (both regex and unified)
                    all_commands = list(commands_config['commands'].keys())
                    # Add unified commands from command_schema
                    unified_commands = unified_extractor.command_schema.get('commands', {}).keys()
                    all_commands.extend([cmd for cmd in unified_commands if cmd not in all_commands])
                    
                    llm_result = llm_parser.analyze_unknown_command(
                        text,
                        all_commands,
                        context=conv,
                        device_id=device_id,
                        conversation_id=log_conv_id
                    )
                    
                    if llm_result['type'] == 'command':
                        # LLM extracted complete command - move to confirmation
                        command_name = llm_result['command']
                        params = llm_result.get('params', {})
                        
                        # Validate command exists (check both sources)
                        cmd_config = unified_extractor.get_command_config(command_name)
                        if not cmd_config:
                            cmd_config = commands_config['commands'].get(command_name, {})
                        
                        if not cmd_config:
                            # Invalid command - ask LLM to clarify
                            logger.error(f"❌ Invalid command extracted: '{command_name}' - falling back to ask")
                            question = f"Sorry, I didn't understand. Could you rephrase what you'd like to do?"
                            audio_file = tts_service.generate(question)
                            audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                            
                            conv['history'].append(('user', text))
                            conv['history'].append(('assistant', question))
                            
                            return create_response({
                                'type': 'ask',
                                'transcription': text,
                                'message': question,
                                'audio_url': audio_url,
                                'conversation_id': conv_id
                            })
                        
                        confirmation_template = cmd_config.get('confirmation_template', f"Confirm {command_name} command?")
                        
                        # Prepare template variables
                        template_params = params.copy()
                        
                        # Add price_text for book command if totalPrice is present
                        if command_name == 'book' and params.get('totalPrice'):
                            template_params['price_text'] = f" for £{params['totalPrice']}"
                        else:
                            template_params['price_text'] = ""
                        
                        try:
                            confirmation = confirmation_template.format(**template_params)
                        except KeyError as e:
                            # Template requires params we don't have
                            logger.error(f"❌ Missing param {e} for confirmation template")
                            confirmation = f"Confirm {command_name} for {params.get('brand', '')} {params.get('model', '')}?"
                        
                        logger.info(f"🔊 TTS (confirm - from ask): '{confirmation}'")
                        audio_file = tts_service.generate(confirmation)
                        audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                        
                        # Update conversation to confirm type
                        conv['type'] = 'confirm'
                        conv['command'] = command_name
                        conv['params'] = params
                        
                        # Append to conversation history
                        conv['history'].append(('user', text))
                        conv['history'].append(('assistant', confirmation))
                        
                        return create_response({
                            'type': 'confirm',
                            'transcription': text,
                            'message': confirmation,
                            'audio_url': audio_url,
                            'conversation_id': conv_id,
                            'command': command_name,
                            'params': params
                        })
                    
                    elif llm_result['type'] == 'ask':
                        # LLM still needs more info - ask another question
                        question = llm_result.get('question', 'Could you provide more details?')
                        
                        logger.info(f"🔊 TTS (ask - follow-up): '{question}'")
                        audio_file = tts_service.generate(question)
                        audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                        
                        # Increment question count
                        conv['question_count'] = question_count + 1
                        
                        # Update params if LLM extracted any
                        params_so_far = llm_result.get('params_so_far', {})
                        conv['params'].update(params_so_far)
                        
                        # Append to conversation history
                        conv['history'].append(('user', text))
                        conv['history'].append(('assistant', question))
                        
                        return create_response({
                            'type': 'ask',
                            'transcription': text,
                            'message': question,
                            'audio_url': audio_url,
                            'conversation_id': conv_id
                        })
                    
                    else:
                        # Fallback - acknowledge and end
                        cmd_config = commands_config['commands'].get(conv['command'], {})
                        message = cmd_config.get('success_message', "Got it, thanks!")
                        
                        logger.info(f"🔊 TTS (say - acknowledge): '{message}'")
                        audio_file = tts_service.generate(message)
                        audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                        
                        conversation_manager.complete(conv_id)
                        
                        return create_response({
                            'type': 'say',
                            'transcription': text,
                            'message': message,
                            'audio_url': audio_url
                        })
                
                elif conv_type == 'confirm':
                    # User sent audio instead of button confirmation
                    # Check if they're saying yes/no or providing a correction
                    text_lower = text.lower().strip()
                    
                    # Check for yes/no responses
                    if text_lower in ['yes', 'yeah', 'yep', 'confirm', 'correct', 'ok']:
                        # User confirmed - execute the command
                        # This will be handled by /execute endpoint via button press
                        # For now, just re-send confirmation
                        cmd_config = commands_config['commands'][conv['command']]
                        confirmation = command_parser.format_confirmation({
                            'confirmation_template': cmd_config['confirmation_template'],
                            'params': conv['params']
                        })
                        
                        logger.info(f"🔊 TTS (re-confirm after yes): '{confirmation}'")
                        audio_file = tts_service.generate(confirmation)
                        audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                        
                        return create_response({
                            'type': 'confirm',
                            'transcription': text,
                            'message': confirmation,
                            'audio_url': audio_url,
                            'conversation_id': conv_id,
                            'command': conv['command'],
                            'params': conv['params']
                        })
                    
                    elif text_lower in ['no', 'nope', 'cancel', 'wrong', 'incorrect']:
                        # User cancelled
                        message = "Cancelled. What would you like to do?"
                        logger.info(f"❌ User cancelled confirmation")
                        audio_file = tts_service.generate(message)
                        audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                        
                        conversation_manager.complete(conv_id)
                        
                        return create_response({
                            'type': 'say',
                            'transcription': text,
                            'message': message,
                            'audio_url': audio_url
                        })
                    
                    else:
                        # User is providing correction - re-extract using LLM with context
                        logger.info(f"🔄 User correcting during confirm: '{text}'")
                        logger.info(f"   Current params: {conv['params']}")
                        
                        # Build correction prompt with current params for context
                        correction_prompt = f"""The user is correcting their booking.

Current booking: {conv['params']}

User's correction: "{text}"

IMPORTANT - Common voice transcription errors:
- "scream" often means "screen"
- "bakery" often means "battery"
- "walter" often means "water"
- "chargin" often means "charging"

This may be:
1. A full replacement (e.g., "No, it's actually a Razer laptop")
2. A partial correction (e.g., "Change model to S24 Ultra")
3. A negation + new info (e.g., "Not iPhone, Samsung Galaxy")
4. Adding missing info (e.g., "At the price 65" → totalPrice: 65)

Extract the NEW/CORRECTED information. If user says "no" or "not", REPLACE the old value completely.
If user mentions price, extract it as totalPrice.
Return the complete corrected booking as JSON."""

                        # Use LLM to intelligently extract correction
                        try:
                            if conv['command'] == 'book':
                                # Use chat mode for better correction handling
                                messages = [
                                    {"role": "system", "content": "You extract device repair bookings. Handle corrections and negations intelligently. Return JSON with brand, model, problem, and optionally totalPrice fields. Be aware of common transcription errors (scream=screen, bakery=battery)."},
                                    {"role": "user", "content": correction_prompt}
                                ]
                                
                                # Log LLM prompt
                                device_logger.log_llm_prompt(
                                    device_id, log_conv_id,
                                    "CORRECTION_EXTRACTION",
                                    messages,
                                    config['openai']['model']
                                )
                                
                                response = llm_parser.client.chat.completions.create(
                                    model=config['openai']['model'],
                                    messages=messages,
                                    response_format={"type": "json_object"},
                                    temperature=0.3,
                                    max_tokens=200
                                )
                                correction_params = json.loads(response.choices[0].message.content)
                                
                                # Log LLM response
                                device_logger.log_llm_response(
                                    device_id, log_conv_id,
                                    "CORRECTION_EXTRACTION",
                                    correction_params
                                )
                                
                                logger.info(f"   LLM correction result: {correction_params}")
                            else:
                                correction_params = {}
                        except Exception as e:
                            logger.error(f"LLM correction failed: {e}, using fallback extraction")
                            if conv['command'] == 'book':
                                correction_params = llm_parser.extract_book_params(text, device_id, log_conv_id)
                            else:
                                correction_params = {}
                        
                        # Store original params before updating (for learning)
                        original_params = conv['params'].copy()
                        
                        # Update ALL extracted parameters (full replacement)
                        for param_name, param_value in correction_params.items():
                            if param_value and param_value != 'null' and param_name != 'ask_clarification':
                                old_value = conv['params'].get(param_name, 'None')
                                conv['params'][param_name] = param_value
                                logger.info(f"  ✏️  {param_name}: '{old_value}' → '{param_value}'")
                        
                        # Learn from correction (auto-update transcription knowledge base)
                        # Pass: original transcription, correction transcription, params, command
                        llm_parser.learn_from_correction(
                            conv.get('original_transcription', ''),  # "Book iPhone 13 for green"
                            text,                                     # "No, screen"
                            correction_params,                        # {problem: "screen"}
                            original_params,                          # {problem: "green"}
                            conv['command']                           # "book"
                        )
                        
                        # Re-generate confirmation with updated params
                        # Get config from unified_extractor for book/sell/invoice, or commands_config for others
                        cmd_config = unified_extractor.get_command_config(conv['command'])
                        if not cmd_config:
                            cmd_config = commands_config['commands'].get(conv['command'], {})
                        
                        confirmation = command_parser.format_confirmation({
                            'confirmation_template': cmd_config.get('confirmation_template', f"Confirm {conv['command']}?"),
                            'params': conv['params']
                        })
                        
                        # Generate TTS for confirmation
                        logger.info(f"🔊 TTS (confirm - corrected): '{confirmation}'")
                        audio_file = tts_service.generate(confirmation)
                        audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                        
                        return create_response({
                            'type': 'confirm',
                            'transcription': text,  # Show user what was heard
                            'message': confirmation,
                            'audio_url': audio_url,
                            'conversation_id': conv_id,
                            'command': conv['command'],
                            'params': conv['params']
                        })
                
                else:  # 'incomplete' - collecting missing parameters
                    # Check question limit
                    question_count = conv.get('question_count', 0)
                    
                    if question_count >= 3:
                        # Max questions reached
                        message = "I'm having trouble understanding. Please try using a command format."
                        logger.info(f"⚠️  Max questions (3) reached for incomplete params")
                        audio_file = tts_service.generate(message)
                        audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                        
                        conversation_manager.complete(conv_id)
                        
                        return create_response({
                            'type': 'say',
                            'transcription': text,
                            'message': message,
                            'audio_url': audio_url
                        })
                    
                    # Parse response - may contain multiple parameters
                    parsed_params = command_parser.parse_multi_param_response(
                        text, 
                        conv['missing'], 
                        conv['command']
                    )
                    
                    # Update conversation with parsed parameters
                    for param_name, param_value in parsed_params.items():
                        if param_name in conv['missing']:
                            conversation_manager.update(conv_id, param_name, param_value)
                    
                    # Check if still missing parameters
                    if conv['missing']:
                        # Increment question count
                        conv['question_count'] = question_count + 1
                        
                        # Get smart prompt for remaining missing params
                        cmd_info = commands_config['commands'][conv['command']]
                        prompt = command_parser._get_smart_prompt(cmd_info, conv['missing'])
                        
                        # Generate TTS for prompt
                        logger.info(f"🔊 TTS (ask - missing params): '{prompt}'")
                        audio_file = tts_service.generate(prompt)
                        audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                        
                        return create_response({
                            'type': 'ask',
                            'transcription': text,
                            'message': prompt,
                            'audio_url': audio_url,
                            'conversation_id': conv_id
                        })
                    else:
                        # All parameters collected - move to confirmation
                        confirmation = command_parser.format_confirmation({
                            'confirmation_template': commands_config['commands'][conv['command']]['confirmation_template'],
                            'params': conv['params']
                        })
                        
                        # Update conversation type to 'confirm'
                        conv['type'] = 'confirm'
                        
                        # Generate TTS for confirmation
                        logger.info(f"🔊 TTS (confirm): '{confirmation}'")
                        audio_file = tts_service.generate(confirmation)
                        audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                        
                        return create_response({
                            'type': 'confirm',
                            'transcription': text,
                            'message': confirmation,
                            'audio_url': audio_url,
                            'conversation_id': conv_id,
                            'command': conv['command'],
                            'params': conv['params']
                        })
        
        # === NEW 3-LAYER ROUTING ARCHITECTURE ===
        
        # Check for retry command
        retry_patterns = r'\b(try\s+again|retry|try\s+that\s+again|do\s+it\s+again)\b'
        if re.search(retry_patterns, text.lower()):
            failed_cmd = conversation_manager.get_failed_command(device_id)
            if failed_cmd:
                logger.info(f"🔄 Retry detected - re-executing {failed_cmd['command']} with params {failed_cmd['params']}")
                
                # Create a fake result to trigger execution
                result = {
                    'type': 'command',
                    'command': failed_cmd['command'],
                    'params': failed_cmd['params'],
                    'requires_confirmation': True,
                    'confirmation_template': f"Retry {failed_cmd['command']}?",
                    'api_endpoint': None,
                    'api_method': None,
                    'auto_print': False
                }
                
                # Generate confirmation
                confirmation = f"Retry {failed_cmd['command']}?"
                logger.info(f"🔊 TTS (confirm - retry): '{confirmation}'")
                audio_file = tts_service.generate(confirmation)
                audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                
                # Create conversation
                conv_id = conversation_manager.create(
                    failed_cmd['command'],
                    failed_cmd['params'],
                    []
                )
                conv = conversation_manager.get(conv_id)
                if conv:
                    conv['type'] = 'confirm'
                
                return create_response({
                    'type': 'confirm',
                    'transcription': text,
                    'message': confirmation,
                    'audio_url': audio_url,
                    'conversation_id': conv_id,
                    'command': failed_cmd['command'],
                    'params': failed_cmd['params']
                })
            else:
                # No failed command to retry
                message = "There's nothing to retry. What would you like me to do?"
                logger.info(f"⚠️ Retry requested but no failed command found")
                audio_file = tts_service.generate(message)
                audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                
                return create_response({
                    'type': 'say',
                    'transcription': text,
                    'message': message,
                    'audio_url': audio_url
                })
        
        # Check if text contains book/sell/invoice keywords - prioritize Layer 2
        prioritize_llm_keywords = r'\b(book|sell|invoice)\b'
        force_layer2 = bool(re.search(prioritize_llm_keywords, text.lower()))
        
        # LAYER 1: Try regex first (fast path for simple patterns) - unless forced to Layer 2
        if not force_layer2:
            result = command_parser.parse_with_regex(text)
        else:
            result = None
            logger.info(f"⚡ Detected book/sell/invoice keyword - skipping to Layer 2")
        
        if result and result.get('type') == 'command':
            # Regex matched - handle as before
            cmd_config = commands_config['commands'].get(result.get('command'), {})
            logger.info(f"✅ LAYER 1 (Regex): Matched command '{result.get('command')}'")
        else:
            # LAYER 2: Try unified command extractor for natural language commands
            logger.info(f"🔄 LAYER 2 (Unified Extractor): Analyzing '{text[:50]}...'")
            extracted = unified_extractor.extract_command(text)
            
            if extracted['command'] != 'unknown' and extracted['confidence'] > 0.5:
                # LLM extracted a valid command
                logger.info(f"✅ LAYER 2: Extracted '{extracted['command']}' (confidence: {extracted['confidence']})")
                
                # Check for missing params
                if extracted['missing_params']:
                    # Ask for missing information
                    missing_param = extracted['missing_params'][0]  # Ask for first missing param
                    prompt = unified_extractor.get_clarification_prompt(missing_param, extracted['params'])
                    
                    logger.info(f"❓ Missing param: {missing_param}, asking: '{prompt}'")
                    
                    # Create conversation to track parameter collection
                    conv_id = conversation_manager.create(
                        extracted['command'],
                        extracted['params'],
                        extracted['missing_params']
                    )
                    conv = conversation_manager.get(conv_id)
                    if conv:
                        conv['type'] = 'incomplete'
                        conv['question_count'] = 1
                    
                    audio_file = tts_service.generate(prompt)
                    audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                    
                    return create_response({
                        'type': 'ask',
                        'transcription': text,
                        'message': prompt,
                        'audio_url': audio_url,
                        'conversation_id': conv_id
                    })
                
                # All params present - convert to result format for execution
                cmd_config = unified_extractor.get_command_config(extracted['command'])
                if not cmd_config:
                    logger.error(f"Command config not found for '{extracted['command']}'")
                    cmd_config = {}
                
                result = {
                    'type': 'command',
                    'command': extracted['command'],
                    'params': extracted['params'],
                    'requires_confirmation': cmd_config.get('requires_confirmation', True),
                    'confirmation_template': cmd_config.get('confirmation_template', ''),
                    'api_endpoint': cmd_config.get('api_endpoint'),
                    'api_method': cmd_config.get('api_method'),
                    'auto_print': cmd_config.get('auto_print', False)
                }
            else:
                # LAYER 3: Default to LLM Agent for conversational queries
                logger.info(f"🤖 LAYER 3 (Agent): Routing to conversational AI")
                result = {'type': 'help'}  # Mark as help to trigger agent below
                cmd_config = {}
        
        # Check if command has 'action' field for special handling
        action = cmd_config.get('action')
        
        if action == 'ask':
            # Command that asks a question
            question = cmd_config.get('ask_question', 'Please provide more information.')
            logger.info(f"🔊 TTS (ask): '{question}'")
            audio_file = tts_service.generate(question)
            audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
            
            # Create conversation to track the ask
            conv_id = conversation_manager.create(
                result['command'],
                {},
                []
            )
            # Set conversation type to 'ask'
            conv = conversation_manager.get(conv_id)
            if conv:
                conv['type'] = 'ask'
                conv['question_count'] = 1  # First question
            
            return create_response({
                'type': 'ask',
                'transcription': text,
                'message': question,
                'audio_url': audio_url,
                'conversation_id': conv_id
            })
        
        elif action == 'test_phoneshop':
            # Test Phoneshop API connection
            device_logger.log_conversation_flow(device_id, log_conv_id, "TEST_CONNECTION", "Phoneshop API")
            
            credentials = device_manager.get_phoneshop_credentials(device_id)
            if not credentials or not credentials[0] or not credentials[1]:
                message = "Device not configured with Phoneshop credentials. Cannot test connection."
                logger.warning(message)
            else:
                username, password = credentials
                # Try to login
                success = phoneshop_api._login(device_id, username, password)
                if success:
                    message = cmd_config.get('success_message', "Phoneshop connection test successful!")
                    device_logger.log_conversation_flow(device_id, log_conv_id, "TEST_SUCCESS", "Phoneshop API")
                else:
                    message = cmd_config.get('error_message', "Phoneshop connection test failed. Check credentials.")
                    device_logger.log_error(device_id, log_conv_id, "TEST_FAILED", "Phoneshop API")
            
            logger.info(f"🔊 TTS (test_connection): '{message}'")
            audio_file = tts_service.generate(message)
            audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
            
            return create_response({
                'type': 'say',
                'transcription': text,
                'message': message,
                'audio_url': audio_url
            })
        
        elif action == 'ai_help':
            # AI-based help/conversation using LLM with help.md context
            device_logger.log_conversation_flow(device_id, log_conv_id, "HELP_REQUEST", text)
            
            # Use LLM with help.md context for open-ended conversation
            try:
                messages = [
                    {"role": "system", "content": f"{help_context}\n\nRespond to the user's question naturally and conversationally. Keep responses brief and voice-friendly (1-3 sentences max)."},
                    {"role": "user", "content": text}
                ]
                
                # Log LLM prompt
                device_logger.log_llm_prompt(
                    device_id, log_conv_id,
                    "AI_HELP",
                    messages,
                    config['openai']['model']
                )
                
                response = llm_parser.client.chat.completions.create(
                    model=config['openai']['model'],
                    messages=messages,
                    temperature=0.7,
                    max_tokens=150
                )
                message = response.choices[0].message.content.strip()
                
                # Log LLM response
                device_logger.log_llm_response(
                    device_id, log_conv_id,
                    "AI_HELP",
                    {"message": message}
                )
                
                logger.info(f"💬 AI response: '{message}'")
            except Exception as e:
                logger.error(f"AI conversation failed: {e}")
                message = "I can help with booking repairs, creating invoices, and checking ticket status. What would you like to do?"
            
            logger.info(f"🔊 TTS (ai_help): '{message}'")
            audio_file = tts_service.generate(message)
            audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
            
            # Log TTS sent to device
            if audio_file:
                device_logger.log_tts(device_id, log_conv_id, message, audio_file)
            
            return create_response({
                'type': 'say',
                'transcription': text,
                'message': message,
                'audio_url': audio_url
            })
        
        elif action == 'repeat_last':
            # Repeat last assistant response
            device_logger.log_conversation_flow(device_id, log_conv_id, "REPEAT_LAST", "")
            
            # Try to find last response from any recent conversation
            last_response = None
            for cid, conv in sorted(conversation_manager.conversations.items(), 
                                   key=lambda x: x[1]['created_at'], reverse=True):
                last_response = conv.get('last_response')
                if last_response:
                    break
            
            if not last_response:
                message = "Sorry, I don't have anything to repeat."
            else:
                message = last_response
                logger.info(f"Repeating last response: '{message}'")
            
            logger.info(f"🔊 TTS (repeat_last): '{message}'")
            audio_file = tts_service.generate(message)
            audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
            
            return create_response({
                'type': 'say',
                'transcription': text,
                'message': message,
                'audio_url': audio_url
            })
        
        elif action == 'repeat_all':
            # Repeat entire conversation and continue with ASK
            device_logger.log_conversation_flow(device_id, log_conv_id, "REPEAT_ALL", "")
            
            # Find most recent conversation with transcript
            transcript_text = None
            most_recent_conv_id = None
            for cid, conv in sorted(conversation_manager.conversations.items(), 
                                   key=lambda x: x[1]['created_at'], reverse=True):
                transcript = conv.get('transcript', [])
                if transcript:
                    # Build readable transcript
                    lines = []
                    for entry in transcript:
                        role = "You" if entry['role'] == 'user' else "Assistant"
                        lines.append(f"{role}: {entry['message']}")
                    transcript_text = ". ".join(lines)
                    most_recent_conv_id = cid
                    break
            
            if not transcript_text:
                message = "Sorry, I don't have any conversation history to repeat."
                response_type = 'say'
                response_conv_id = None
            else:
                message = f"Here's what we discussed: {transcript_text}. Is there anything else you need?"
                logger.info(f"Repeating conversation transcript")
                response_type = 'ask'
                response_conv_id = most_recent_conv_id
            
            logger.info(f"🔊 TTS (repeat_all): '{message}'")
            audio_file = tts_service.generate(message)
            audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
            
            return create_response({
                'type': response_type,
                'transcription': text,
                'message': message,
                'audio_url': audio_url,
                'conversation_id': response_conv_id
            })
        
        # Handle result types
        if result.get('type') == 'help' or result.get('type') == 'unknown':
            # Open-ended conversation with LLM using help context
            logger.info(f"💬 Help/Unknown - engaging conversational AI: '{text}'")
            device_logger.log_conversation_flow(device_id, log_conv_id, "CONVERSATION", text)
            
            # Check if continuing existing conversation
            existing_conv = None
            if conv_id:
                existing_conv = conversation_manager.get(conv_id)
                if existing_conv:
                    logger.info(f"📚 Continuing conversation {conv_id} with history")
            
            # Check if this is a query that needs API access (ticket search, status check, etc.)
            query_keywords = ['status', 'repair', 'ticket', 'my', 'find', 'check', 'where is', 'how much']
            needs_api_access = any(keyword in text.lower() for keyword in query_keywords)
            
            if llm_agent and needs_api_access:
                # Use LLM Agent for queries that might need API calls
                logger.info(f"🤖 Using LLM Agent for query: '{text}'")
                try:
                    # Get device credentials
                    device = device_manager.get_device(device_id)
                    if device and device.get('phoneshop_username') and device.get('phoneshop_password'):
                        username = device['phoneshop_username']
                        password = device['phoneshop_password']
                        
                        # Call LLM Agent
                        message = llm_agent.query(device_id, username, password, text)
                        
                        logger.info(f"🤖 LLM Agent response: '{message[:100]}...'")
                    else:
                        message = "I need your shop credentials to check that information. Please register this device first."
                        logger.warning(f"Device {device_id} not registered with shop credentials")
                
                except Exception as e:
                    logger.error(f"LLM Agent query failed: {e}")
                    message = "Sorry, I couldn't access that information right now."
            
            else:
                # Use standard LLM with help.md context for general conversation
                try:
                    # Load conversational prompt from YAML
                    prompts_path = Path(__file__).parent / '..' / 'config' / 'llm_prompts.yaml'
                    with open(prompts_path, 'r', encoding='utf-8') as f:
                        prompts = yaml.safe_load(f)
                    
                    conversational_prompt = prompts['conversational']['system']
                    
                    # Build messages with conversation history if available
                    system_instruction = f"{help_context}\n\n{conversational_prompt}"
                    
                    messages = [
                        {"role": "system", "content": system_instruction}
                    ]
                    
                    # Add conversation history if continuing existing conversation
                    if existing_conv and existing_conv.get('history'):
                        for role, content in existing_conv['history']:
                            messages.append({"role": role, "content": content})
                        logger.info(f"📜 Added {len(existing_conv['history'])} history messages to context")
                    
                    # Add current user message
                    messages.append({"role": "user", "content": text})
                    
                    # Log LLM prompt
                    device_logger.log_llm_prompt(
                        device_id, log_conv_id,
                        "CONVERSATION",
                        messages,
                        config['openai']['model']
                    )
                    
                    response = llm_parser.client.chat.completions.create(
                        model=config['openai']['model'],
                        messages=messages,
                        temperature=0.7,
                        max_tokens=150
                    )
                    message = response.choices[0].message.content.strip()
                    
                    # Log LLM response
                    device_logger.log_llm_response(
                        device_id, log_conv_id,
                        "CONVERSATION",
                        {"message": message}
                    )
                    
                    logger.info(f"💬 AI conversation response: '{message}'")
                except Exception as e:
                    logger.error(f"AI conversation failed: {e}")
                    message = "I can help with booking repairs, creating invoices, and checking tickets. Or ask me anything! What would you like to know? [[ASK]]"
            
            # Extract conversation type from LLM response
            response_type = 'say'  # Default
            execute_command = None
            
            if '[[ASK]]' in message:
                response_type = 'ask'
                message = message.replace('[[ASK]]', '').strip()
                logger.info(f"🤖 LLM decided: ASK (continuing conversation)")
            elif '[[SAY]]' in message:
                response_type = 'say'
                message = message.replace('[[SAY]]', '').strip()
                logger.info(f"🤖 LLM decided: SAY (ending conversation)")
            elif '[[EXECUTE:' in message:
                # LLM wants to execute a command
                match = re.search(r'\[\[EXECUTE:(\w+)\]\]', message)
                if match:
                    execute_command = match.group(1)
                    message = message.replace(match.group(0), '').strip()
                    logger.info(f"🤖 LLM decided: EXECUTE command '{execute_command}'")
                    
                    # Execute the command from conversation context
                    if existing_conv and existing_conv.get('history'):
                        # Extract parameters from conversation history
                        full_context = ' '.join([msg for role, msg in existing_conv['history'] if role == 'user'])
                        full_context += ' ' + text
                        
                        logger.info(f"🔄 Extracting command '{execute_command}' from context: '{full_context}'")
                        
                        # Use unified extractor to parse the full context
                        extraction_result = unified_extractor.extract_command(full_context)
                        
                        if extraction_result['command'] == execute_command and extraction_result.get('params'):
                            # Execute the command
                            logger.info(f"✅ Extracted params: {extraction_result['params']}")
                            
                            # TODO: Execute command and get result
                            # For now, provide result back to LLM
                            execution_result = f"Command '{execute_command}' would be executed with params: {extraction_result['params']}"
                            
                            # Ask LLM to inform user of the result
                            messages.append({"role": "assistant", "content": message})
                            messages.append({"role": "system", "content": f"EXECUTION RESULT: {execution_result}\n\nInform the user of this result naturally. Use [[SAY]] to end."})
                            
                            # Get LLM's final response
                            response = llm_parser.client.chat.completions.create(
                                model=config['openai']['model'],
                                messages=messages,
                                temperature=0.7,
                                max_tokens=150
                            )
                            message = response.choices[0].message.content.strip()
                            message = message.replace('[[SAY]]', '').replace('[[ASK]]', '').strip()
                            response_type = 'say'  # Always end after execution
                            logger.info(f"🤖 LLM final message after execution: '{message}'")
                        else:
                            logger.error(f"❌ Failed to extract command '{execute_command}' - falling back")
                            message += " (I couldn't extract all the details needed)"
                            response_type = 'ask'
            else:
                # Fallback: if ends with ?, assume asking
                response_type = 'ask' if message.endswith('?') else 'say'
                logger.warning(f"⚠️ LLM didn't specify type, defaulting to: {response_type}")
            
            logger.info(f"🔊 TTS (conversation): '{message}'")
            audio_file = tts_service.generate(message)
            audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
            
            # Log TTS sent to device
            if audio_file:
                device_logger.log_tts(device_id, log_conv_id, message, audio_file)
            
            logger.info(f"📤 Returning response: type={response_type}, audio_url={audio_url}")
            
            response_data = {
                'type': response_type,
                'transcription': text,
                'message': message,
                'audio_url': audio_url
            }
            
            # Always maintain conversation for potential continuation
            if existing_conv:
                # Update existing conversation
                conv = existing_conv
                if 'history' not in conv:
                    conv['history'] = []
                conv['history'].append(('user', text))
                conv['history'].append(('assistant', message))
                logger.info(f"📝 Updated conversation {conv_id} history (now {len(conv['history'])} messages)")
            else:
                # Create new conversation (user can always reply)
                conv_id = conversation_manager.create('help', {}, [])
                conv = conversation_manager.get(conv_id)
                if conv:
                    conv['type'] = 'ask'  # Always allow continuation
                    # Store conversation history for LLM context
                    conv['history'] = [
                        ('user', text),
                        ('assistant', message)
                    ]
                logger.info(f"📤 Created new conversation {conv_id}")
            
            # Always return conversation_id so user can continue if they want
            response_data['conversation_id'] = conv_id
            logger.info(f"📤 Conversation ID in response: {conv_id}, type: {response_type}")
            
            return create_response(response_data)
        
        elif result.get('type') == 'incomplete':
            # Missing parameters - Ask for them
            conv_id = conversation_manager.create(
                result['command'],
                result['params'],
                result['missing']
            )
            # Set conversation type to 'incomplete' (will collect params)
            conv = conversation_manager.get(conv_id)
            if conv:
                conv['type'] = 'incomplete'
                conv['question_count'] = 1  # First question for params
            
            prompt = result['prompt']
            logger.info(f"🔊 TTS (ask - params): '{prompt}'")
            audio_file = tts_service.generate(prompt)
            audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
            
            return create_response({
                'type': 'ask',
                'transcription': text,
                'message': prompt,
                'audio_url': audio_url,
                'conversation_id': conv_id
            })
        
        elif result.get('type') == 'command':
            # Complete command
            if result['requires_confirmation']:
                # Needs confirmation
                confirmation = command_parser.format_confirmation(result)
                logger.info(f"🔊 TTS (confirm): '{confirmation}'")
                audio_file = tts_service.generate(confirmation)
                audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                
                # Create conversation to track confirmation
                conv_id = conversation_manager.create(
                    result['command'],
                    result['params'],
                    [],
                    text  # Store original transcription for learning
                )
                # Set conversation type to 'confirm'
                conv = conversation_manager.get(conv_id)
                if conv:
                    conv['type'] = 'confirm'
                
                return create_response({
                    'type': 'confirm',
                    'transcription': text,
                    'message': confirmation,
                    'audio_url': audio_url,
                    'conversation_id': conv_id,
                    'command': result['command'],
                    'params': result['params'],
                    'device_command': result.get('device_command', False)
                })
            else:
                # Execute immediately (no confirmation needed)
                # For device commands, just acknowledge
                message = cmd_config.get('success_message', f"{result['command'].title()} command executed")
                logger.info(f"🔊 TTS (say - execute): '{message}'")
                audio_file = tts_service.generate(message)
                audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
                
                return create_response({
                    'type': 'say',
                    'transcription': text,
                    'message': message,
                    'audio_url': audio_url,
                    'command': result['command'],
                    'params': result['params'],
                    'device_command': result.get('device_command', False)
                })
        
        else:
            # Fallback - should not reach here
            message = 'Something went wrong. Please try again.'
            logger.warning(f"Unexpected result type: {result.get('type')}")
            audio_file = tts_service.generate(message)
            audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
            
            return create_response({
                'type': 'say',
                'transcription': text,
                'message': message,
                'audio_url': audio_url
            })
    
    except Exception as e:
        logger.error(f"Error in /transcribe: {e}", exc_info=True)
        return jsonify({'error': str(e)}), 500
    
    finally:
        # Cleanup audio file
        if 'audio_path' in locals() and audio_path and os.path.exists(audio_path):
            os.remove(audio_path)

@app.route('/correct', methods=['POST'])
@require_device_auth(device_manager)
def correct():
    """
    Handle correction after user holds button for correction.
    
    Expected: multipart/form-data with 'audio' file + 'conversation_id' header
    """
    try:
        # Device authentication handled by @require_device_auth decorator
        conv_id = request.headers.get('Conversation-ID')
        if not conv_id:
            return jsonify({'error': 'Conversation-ID required for corrections'}), 400
        
        conv = conversation_manager.get(conv_id)
        if not conv:
            return jsonify({'error': 'Conversation not found'}), 404
        
        # Check correction limit
        correction_count = conv.get('correction_count', 0)
        if correction_count >= 3:
            conversation_manager.complete(conv_id)
            return create_response({
                'type': 'error',
                'message': 'Too many corrections. Please start over.',
                'action': 'reset'
            })
        
        # Get audio file
        if 'audio' not in request.files:
            return jsonify({'error': 'No audio file provided'}), 400
        
        audio_file = request.files['audio']
        filename = secure_filename(audio_file.filename)
        audio_path = os.path.join(get_audio_temp_dir(), filename)
        audio_file.save(audio_path)
        
        # Transcribe correction
        text = transcription_service.transcribe(audio_path)
        if not text:
            return jsonify({'error': 'Transcription failed'}), 500
        
        logger.info(f"Correction transcribed: {text}")
        
        # Use LLM to extract what changed
        if conv['command'] in ['book', 'sell']:
            if conv['command'] == 'book':
                correction_params = llm_parser.extract_book_params(text, device_id, conv_id)
            else:
                correction_params = llm_parser.extract_sell_params(text, device_id, conv_id)
            
            # Merge with existing params (only update non-null values)
            for key, value in correction_params.items():
                if value:
                    conv['params'][key] = value
            
            # Increment correction count
            conv['correction_count'] = correction_count + 1
            
            # Save successful correction to knowledge base
            if 'brand' in correction_params and 'model' in correction_params:
                llm_parser.save_correction(
                    text,
                    correction_params['brand'],
                    correction_params['model']
                )
            
            # Generate new confirmation
            confirmation = command_parser.format_confirmation({
                'confirmation_template': commands_config['commands'][conv['command']]['confirmation_template'],
                'params': conv['params']
            })
            
            # Generate TTS for confirmation
            audio_file = tts_service.generate(confirmation)
            audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
            
            return create_response({
                'type': 'confirm',
                'transcription': text,
                'message': confirmation,
                'audio_url': audio_url,
                'conversation_id': conv_id,
                'command': conv['command'],
                'params': conv['params'],
                'correction_count': conv['correction_count']
            })
        
        return jsonify({'error': 'Correction not supported for this command type'}), 400
    
    except Exception as e:
        logger.error(f"Error in /correct: {e}", exc_info=True)
        return jsonify({'error': str(e)}), 500
    
    finally:
        if 'audio_path' in locals() and audio_path and os.path.exists(audio_path):
            os.remove(audio_path)

@app.route('/execute', methods=['POST'])
@require_device_auth(device_manager)
def execute():
    """
    Execute confirmed command.
    
    Expected JSON: {
        'command': 'book',
        'params': {...},
        'conversation_id': '...' (optional)
    }
    """
    try:
        # Device authentication handled by @require_device_auth decorator
        device_id = request.device['id']
        data = request.json
        command = data.get('command')
        conv_id = data.get('conversation_id', 'direct')
        
        # Retrieve params from conversation (LLM already extracted them)
        params = {}
        if conv_id and conv_id != 'direct':
            conversation = conversation_manager.get(conv_id)
            if conversation:
                params = conversation.get('params', {})
                logger.info(f"Retrieved params from conversation {conv_id}: {params}")
            else:
                logger.warning(f"Conversation {conv_id} not found, using empty params")
        
        # Allow params override from request (optional)
        if 'params' in data:
            params.update(data['params'])
        
        logger.info(f"Executing command: {command} with params: {params}")
        
        # Log to device-specific log
        device_logger.log_request(device_id, conv_id, f"EXECUTE: {command}", params)
        
        # Get device's Phoneshop credentials
        credentials = device_manager.get_phoneshop_credentials(device_id)
        if not credentials or not credentials[0] or not credentials[1]:
            error_msg = "Device not configured with Phoneshop credentials"
            logger.error(f"{error_msg} (device: {device_id})")
            device_logger.log_error(device_id, conv_id, "CREDENTIALS_MISSING", error_msg)
            return create_response({
                'status': 'error',
                'message': error_msg
            }), 400
        
        username, password = credentials
        
        # Execute command based on type
        result_message = ""
        success = False
        ticket_number = None  # Track ticket/invoice IDs for success messages
        
        if command == 'book':
            # Create repair ticket
            # Clean up trailing periods from LLM output (except notes)
            brand = params.get('brand', '').rstrip('.')
            model = params.get('model', '').rstrip('.')
            problem = params.get('problem', '').rstrip('.')
            device_condition = params.get('deviceCondition')
            accessories = params.get('accessories')
            total_price = params.get('totalPrice')
            deposit = params.get('deposit')
            device_password = params.get('devicePassword')
            notes = params.get('notes')  # Keep notes as-is
            
            success, message, ticket_id = phoneshop_api.create_ticket(
                device_id, username, password,
                brand, model, problem,
                device_condition=device_condition,
                accessories=accessories,
                total_price=total_price,
                deposit=deposit,
                device_password=device_password,
                notes=notes
            )
            
            device_logger.log_api_call(device_id, conv_id, "/YOUR_SHOP_API_PATH", params, 
                                      200 if success else 500, {'ticket_id': ticket_id})
            
            result_message = message
            
            # Store ticket_id for later use in response
            ticket_number = ticket_id
            
        elif command == 'sell':
            # Create sales invoice
            brand = params.get('brand', '')
            model = params.get('model', '')
            price = float(params.get('price', 0))
            serial_number = params.get('imei', params.get('serial_number', ''))
            payment_method = params.get('payment_method', 'CARD')
            
            success, message, invoice_id = phoneshop_api.create_invoice(
                device_id, username, password,
                brand, model, price, serial_number, payment_method
            )
            
            device_logger.log_api_call(device_id, conv_id, "/YOUR_SHOP_API_PATH", params,
                                      200 if success else 500, {'invoice_id': invoice_id})
            
            # Store invoice_id for success message
            ticket_number = invoice_id
            
            if success and invoice_id:
                # Download PDF
                pdf_path = phoneshop_api.download_invoice_pdf(device_id, invoice_id, invoice_dir)
                
                if pdf_path:
                    device_logger.log_conversation_flow(device_id, conv_id, "INVOICE_PDF", pdf_path)
                    
                    # Try to print
                    if print_pdf_windows(pdf_path):
                        result_message = f"{message}. PDF sent to printer."
                        device_logger.log_conversation_flow(device_id, conv_id, "PRINTED", pdf_path)
                    else:
                        result_message = f"{message}. PDF saved but printing failed."
                else:
                    result_message = f"{message}. Failed to download PDF."
            else:
                result_message = message
            
        elif command == 'status':
            # Get ticket status
            ticket_id = params.get('ticket_id', '')
            
            success, message, ticket_data = phoneshop_api.get_ticket_status(
                device_id, username, password, ticket_id
            )
            
            device_logger.log_api_call(device_id, conv_id, f"/YOUR_SHOP_API_PATH?id={ticket_id}", {},
                                      200 if success else 500, ticket_data)
            
            result_message = message
            
        elif command in ['ticket_start', 'ticket_complete', 'ticket_unfixable', 'ticket_collected', 'ticket_freeze', 'ticket_cancel']:
            # Update ticket status
            ticket_id = params.get('ticket_id', '')
            
            # Determine endpoint based on command
            if command == 'ticket_start':
                endpoint = f"/YOUR_SHOP_API_PATH?id={ticket_id}"
            elif command == 'ticket_complete':
                endpoint = f"/YOUR_SHOP_API_PATH?id={ticket_id}&success=true"
            elif command == 'ticket_unfixable':
                endpoint = f"/YOUR_SHOP_API_PATH?id={ticket_id}&success=false"
            elif command == 'ticket_collected':
                endpoint = f"/YOUR_SHOP_API_PATH?id={ticket_id}"
            elif command == 'ticket_freeze':
                endpoint = f"/YOUR_SHOP_API_PATH?id={ticket_id}"
            elif command == 'ticket_cancel':
                endpoint = f"/YOUR_SHOP_API_PATH?id={ticket_id}"
            
            success, message = phoneshop_api.update_ticket_status(
                device_id, username, password, ticket_id, endpoint
            )
            
            device_logger.log_api_call(device_id, conv_id, endpoint, {},
                                      200 if success else 500, {'ticket_id': ticket_id})
            
            result_message = message
            ticket_number = ticket_id
            
            # Auto-print invoice for collected tickets
            if command == 'ticket_collected' and success:
                # Note: The backend creates the invoice, we'd need the invoice_id to print
                # For now, just log success
                result_message = f"{message}. Invoice created."
            
        elif command in ['print_ticket', 'print_tag']:
            # Print ticket documents
            ticket_id = params.get('ticket_id', '')
            document_type = 'tag' if command == 'print_tag' else 'ticket'
            
            endpoint = f"{'/YOUR_SHOP_API_PATH' if document_type == 'tag' else '/YOUR_SHOP_API_PATH'}?ticketId={ticket_id}"
            
            success, message, pdf_path = phoneshop_api.print_ticket_document(
                device_id, username, password, ticket_id, document_type, invoice_dir
            )
            
            device_logger.log_api_call(device_id, conv_id, endpoint, {},
                                      200 if success else 500, {'pdf_path': pdf_path})
            
            if success and pdf_path:
                device_logger.log_conversation_flow(device_id, conv_id, f"PRINT_{document_type.upper()}", pdf_path)
                
                # Try to print
                if print_pdf_windows(pdf_path):
                    result_message = f"{message}. Sent to printer."
                    device_logger.log_conversation_flow(device_id, conv_id, "PRINTED", pdf_path)
                else:
                    result_message = f"{message}. PDF saved but printing failed."
            else:
                result_message = message
            
            ticket_number = ticket_id
            
        elif command == 'whats_next':
            # Get pending tickets
            
            success, message, tickets = phoneshop_api.get_pending_tickets(
                device_id, username, password
            )
            
            device_logger.log_api_call(device_id, conv_id, "/YOUR_SHOP_API_PATH", {},
                                      200 if success else 500, {'count': len(tickets) if tickets else 0})
            
            result_message = message
            
        else:
            # Unknown command
            result_message = f"Command '{command}' not implemented yet"
            logger.warning(result_message)
        
        # Clean up conversation if exists
        if conv_id and conv_id != 'direct':
            conversation_manager.complete(conv_id)
        
        # Log result
        if success:
            device_logger.log_conversation_flow(device_id, conv_id, "EXECUTE_SUCCESS", result_message)
        else:
            device_logger.log_error(device_id, conv_id, "EXECUTE_FAILED", result_message)
        
        # Generate natural language message
        if success:
            # For success, create clear confirmation with ticket/invoice number
            if command == 'book' and ticket_number:
                brand = params.get('brand', '')
                model = params.get('model', '')
                spoken_message = f"Booking confirmed. Ticket number {ticket_number}. {brand} {model}."
                logger.info(f"📋 Success message: {spoken_message}")
            elif command == 'sell' and ticket_number:
                spoken_message = f"Sale completed. Invoice number {ticket_number}."
                logger.info(f"💰 Success message: {spoken_message}")
            elif command == 'ticket_start' and ticket_number:
                spoken_message = f"Ticket {ticket_number} started."
                logger.info(f"▶️ Success message: {spoken_message}")
            elif command == 'ticket_complete' and ticket_number:
                spoken_message = f"Ticket {ticket_number} marked as completed."
                logger.info(f"✅ Success message: {spoken_message}")
            elif command == 'ticket_unfixable' and ticket_number:
                spoken_message = f"Ticket {ticket_number} marked as unfixable."
                logger.info(f"❌ Success message: {spoken_message}")
            elif command == 'ticket_collected' and ticket_number:
                spoken_message = f"Ticket {ticket_number} collected. Invoice created."
                logger.info(f"📦 Success message: {spoken_message}")
            elif command == 'ticket_freeze' and ticket_number:
                spoken_message = f"Ticket {ticket_number} frozen."
                logger.info(f"⏸️ Success message: {spoken_message}")
            elif command == 'ticket_cancel' and ticket_number:
                spoken_message = f"Ticket {ticket_number} cancelled."
                logger.info(f"🚫 Success message: {spoken_message}")
            elif command in ['print_ticket', 'print_tag']:
                spoken_message = result_message  # Already formatted
                logger.info(f"🖨️ Success message: {spoken_message}")
            elif command == 'whats_next':
                # result_message already contains formatted list
                spoken_message = result_message
                logger.info(f"📋 Pending tickets: {spoken_message}")
            else:
                spoken_message = result_message
            
            # Clear failed command on success
            conversation_manager.clear_failed_command(device_id)
        else:
            # Error occurred - use simple message
            spoken_message = f"Sorry, {command} failed. {result_message}"
            logger.warning(f"❌ Command failed: {command} - {result_message}")
            
            # Store failed command for retry
            conversation_manager.store_failed_command(device_id, command, params)
        
        # Generate TTS for both success AND failure
        audio_url = None
        if spoken_message:
            audio_file = tts_service.generate(spoken_message)
            audio_url = tts_service.get_url_for_file(audio_file, request.host_url.rstrip('/')) if audio_file else None
            if audio_url:
                logger.info(f"TTS generated for execute response: {audio_url}")
        
        response_data = {
            'status': 'success' if success else 'error',
            'message': spoken_message,  # Device displays this field
            'type': 'say',
            'transcription': spoken_message  # Also include for consistency
        }
        
        # Include ticket/invoice number if available
        if ticket_number:
            response_data['ticket_id'] = ticket_number
            if command == 'book':
                response_data['ticket_number'] = ticket_number
            elif command == 'sell':
                response_data['invoice_id'] = ticket_number
        
        # Always include audio URL if generated
        if audio_url:
            response_data['audio_url'] = audio_url
        
        return jsonify(response_data)
    
    except Exception as e:
        logger.error(f"Error in /execute: {e}", exc_info=True)
        if 'device_id' in locals() and 'conv_id' in locals():
            device_logger.log_error(device_id, conv_id, "EXCEPTION", str(e))
        return jsonify({'error': str(e)}), 500

@app.route('/status', methods=['GET'])
@require_device_auth(device_manager)
def status():
    """Server status and statistics (device auth required)"""
    return jsonify({
        'status': 'running',
        'active_conversations': len(conversation_manager.conversations),
        'openai_configured': bool(os.getenv('OPENAI_API_KEY'))
    })

@app.route('/audio/<filename>', methods=['GET'])
def serve_audio(filename):
    """Serve generated TTS audio files"""
    try:
        file_path = os.path.join(audio_temp_dir, secure_filename(filename))
        
        if not os.path.exists(file_path):
            logger.warning(f"Audio file not found: {filename}")
            return jsonify({'error': 'File not found'}), 404
        
        # Determine mimetype based on file extension
        mimetype = 'audio/wav' if filename.endswith('.wav') else 'audio/mpeg'
        logger.info(f"Serving audio file: {filename} ({mimetype})")
        
        return send_file(file_path, mimetype=mimetype)
    
    except Exception as e:
        logger.error(f"Error serving audio: {e}")
        return jsonify({'error': str(e)}), 500

# Web interface routes
@app.route('/')
def index():
    """Redirect to login or admin"""
    if session.get('admin_token'):
        return redirect(url_for('admin_page'))
    return redirect(url_for('login_page'))

@app.route('/login')
def login_page():
    """Login page"""
    return render_template('login.html')

@app.route('/admin')
def admin_page():
    """Admin dashboard (protected)"""
    if not session.get('admin_token'):
        return redirect(url_for('login_page'))
    return render_template('admin.html')

@app.route('/register')
def register_page():
    """Device registration web interface (admin only)"""
    if not session.get('admin_token'):
        return redirect(url_for('login_page'))
    return render_template('register.html')

@app.route('/api/admin/login', methods=['POST'])
def admin_login():
    """Admin login with brute-force protection"""
    try:
        # Check if login is currently locked
        if admin_login_attempts['locked_until']:
            if datetime.utcnow() < admin_login_attempts['locked_until']:
                remaining = (admin_login_attempts['locked_until'] - datetime.utcnow()).total_seconds()
                minutes = int(remaining / 60)
                seconds = int(remaining % 60)
                logger.warning(f"Admin login attempt while locked (remaining: {minutes}m {seconds}s)")
                return create_response({
                    'error': 'Too many failed attempts',
                    'locked_for_seconds': int(remaining),
                    'message': f'Login locked for {minutes} minutes {seconds} seconds'
                }), 429
            else:
                # Lock expired, reset
                admin_login_attempts['failed_count'] = 0
                admin_login_attempts['locked_until'] = None
                logger.info("Admin login lock expired, reset attempts")
        
        data = request.json
        password = data.get('password')
        
        if password == os.getenv('ADMIN_PASSWORD', 'admin123'):
            # Successful login - reset failed attempts
            admin_login_attempts['failed_count'] = 0
            admin_login_attempts['locked_until'] = None
            
            token = secrets.token_hex(32)
            session['admin_token'] = token
            logger.info("✅ Admin login successful")
            return jsonify({'status': 'success', 'token': token})
        else:
            # Failed login - increment counter
            admin_login_attempts['failed_count'] += 1
            attempts_left = 3 - admin_login_attempts['failed_count']
            
            logger.warning(f"❌ Failed admin login attempt ({admin_login_attempts['failed_count']}/3)")
            
            if admin_login_attempts['failed_count'] >= 3:
                # Lock for 15 minutes
                admin_login_attempts['locked_until'] = datetime.utcnow() + timedelta(minutes=15)
                logger.error("🔒 Admin login LOCKED for 15 minutes due to too many failed attempts")
                return create_response({
                    'error': 'Too many failed attempts',
                    'locked_for_seconds': 900,
                    'message': 'Login locked for 15 minutes'
                }), 429
            else:
                return jsonify({
                    'error': 'Invalid password',
                    'attempts_left': attempts_left
                }), 401
    
    except Exception as e:
        logger.error(f"Error in admin login: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/admin/logout', methods=['POST'])
def admin_logout():
    """Admin logout"""
    session.pop('admin_token', None)
    return jsonify({'status': 'success'})

@app.route('/api/devices', methods=['GET'])
def list_devices():
    """List all registered devices"""
    return jsonify(device_manager.devices)

@app.route('/api/register-device', methods=['POST'])
@require_admin
def register_device():
    """
    Register a new device (works for both ESP32 direct and web interface).
    
    ESP32 direct registration (minimal):
    {
        "device_id": "AA:BB:CC:DD:EE:FF"
    }
    
    Web interface registration (full):
    {
        "device_id": "AA:BB:CC:DD:EE:FF",
        "staff_name": "John Doe",
        "phoneshop_username": "john",
        "phoneshop_password": "pass123"
    }
    
    Always returns the device secret for HMAC authentication.
    """
    try:
        data = request.json
        
        # device_id is required (support both 'device_id' and 'deviceId' for compatibility)
        device_id = data.get('device_id') or data.get('deviceId')
        
        if not device_id:
            return jsonify({'error': 'device_id required'}), 400
        
        # Optional fields for web interface
        staff_name = data.get('staff_name')
        phoneshop_username = data.get('phoneshop_username')
        phoneshop_password = data.get('phoneshop_password')
        
        # Register the device and get/generate secret
        secret = device_manager.register_device(
            device_id,
            staff_name,
            phoneshop_username,
            phoneshop_password
        )
        
        logger.info(f"Device registered: {device_id} ({staff_name or 'Unknown'})")
        
        # Return format compatible with both ESP32 and web interface
        return create_response({
            'ok': True,
            'status': 'success',
            'deviceId': device_id,
            'device_id': device_id,  # Both formats for compatibility
            'secret': secret,
            'message': f'Device {device_id} registered' + (f' for {staff_name}' if staff_name else '')
        })
    
    except Exception as e:
        logger.error(f"Error registering device: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/device/<device_id>', methods=['PUT'])
@require_admin
def update_device(device_id):
    """Update device information (staff name and Phoneshop credentials only)"""
    try:
        if device_id not in device_manager.devices:
            return jsonify({'error': 'Device not found'}), 404
        
        data = request.json
        device = device_manager.devices[device_id]
        
        # Only allow updating these fields (NOT device_id or secret)
        if 'staff_name' in data:
            device['staff_name'] = data['staff_name']
        if 'phoneshop_username' in data:
            device['phoneshop_username'] = data['phoneshop_username']
        if 'phoneshop_password' in data:
            device['phoneshop_password'] = data['phoneshop_password']
        
        device_manager._save_devices()
        logger.info(f"Device {device_id} updated: {device['staff_name']}")
        
        return create_response({
            'status': 'success',
            'message': f'Device {device_id} updated successfully'
        })
    
    except Exception as e:
        logger.error(f"Error updating device: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/device/<device_id>', methods=['DELETE'])
@require_admin
def delete_device(device_id):
    """Delete a device"""
    try:
        if device_id in device_manager.devices:
            del device_manager.devices[device_id]
            device_manager._save_devices()
            return create_response({'status': 'success', 'message': 'Device deleted'})
        else:
            return jsonify({'error': 'Device not found'}), 404
    except Exception as e:
        logger.error(f"Error deleting device: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/logs', methods=['GET'])
@require_admin
def get_logs():
    """Get recent server logs"""
    try:
        log_file = config['logging']['file']
        if not os.path.exists(log_file):
            return jsonify([])
        
        # Read last 100 lines
        with open(log_file, 'r') as f:
            lines = f.readlines()
            recent_logs = lines[-100:] if len(lines) > 100 else lines
        
        return jsonify([line.strip() for line in recent_logs])
    
    except Exception as e:
        logger.error(f"Error reading logs: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/logs/download', methods=['GET'])
@require_admin
def download_logs():
    """Download full log file"""
    try:
        log_file = config['logging']['file']
        if not os.path.exists(log_file):
            return jsonify({'error': 'Log file not found'}), 404
        
        return send_file(log_file, as_attachment=True, download_name='server.log')
    
    except Exception as e:
        logger.error(f"Error downloading logs: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/update-env', methods=['POST'])
@require_admin
def update_env():
    """Update .env file with new API keys"""
    try:
        data = request.json
        env_file = os.path.join(os.path.dirname(__file__), '..', 'config', '.env')
        
        # Read current .env
        with open(env_file, 'r') as f:
            lines = f.readlines()
        
        # Update specific keys
        updated_lines = []
        for line in lines:
            if line.startswith('OPENAI_API_KEY=') and 'openai_key' in data:
                updated_lines.append(f"OPENAI_API_KEY={data['openai_key']}\n")
            elif line.startswith('PHONESHOP_USERNAME=') and 'phoneshop_username' in data:
                updated_lines.append(f"PHONESHOP_USERNAME={data['phoneshop_username']}\n")
            elif line.startswith('PHONESHOP_PASSWORD=') and 'phoneshop_password' in data:
                updated_lines.append(f"PHONESHOP_PASSWORD={data['phoneshop_password']}\n")
            else:
                updated_lines.append(line)
        
        # Write back
        with open(env_file, 'w') as f:
            f.writelines(updated_lines)
        
        logger.info("Environment variables updated via admin panel")
        return create_response({
            'status': 'success',
            'message': 'API keys updated. Restart server to apply changes.'
        })
    
    except Exception as e:
        logger.error(f"Error updating env: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/knowledge-base', methods=['GET'])
@require_admin
def get_knowledge_base():
    """Get learned brand/model corrections"""
    try:
        return jsonify({
            'examples': llm_parser.knowledge_base.get('examples', []),
            'rules': llm_parser.knowledge_base.get('parsing_rules', [])
        })
    except Exception as e:
        logger.error(f"Error reading knowledge base: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/cache-stats', methods=['GET'])
@require_admin
def get_cache_stats():
    """Get TTS cache statistics"""
    try:
        stats = tts_service.get_cache_stats()
        return jsonify(stats)
    except Exception as e:
        logger.error(f"Error getting cache stats: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/cache-cleanup', methods=['POST'])
@require_admin
def manual_cache_cleanup():
    """Manually trigger cache cleanup"""
    try:
        deleted_count = tts_service.cleanup_by_size()
        stats = tts_service.get_cache_stats()
        return jsonify({
            'status': 'success',
            'deleted_files': deleted_count,
            'cache_stats': stats
        })
    except Exception as e:
        logger.error(f"Error in manual cache cleanup: {e}")
        return jsonify({'error': str(e)}), 500

if __name__ == '__main__':
    host = config['server']['host']
    port = config['server']['port']
    debug = config['server']['debug']
    timeout = config['server'].get('request_timeout', 90)  # Default 90s
    
    logger.info(f"Starting server on {host}:{port}")
    logger.info(f"Request timeout: {timeout}s")
    
    # Use production server (Waitress) if not in debug mode
    if not debug:
        try:
            from waitress import serve
            logger.info("Using Waitress production server")
            serve(app, host=host, port=port, threads=4, channel_timeout=timeout)
        except ImportError:
            logger.warning("Waitress not installed, using Flask dev server")
            app.run(host=host, port=port, debug=False)
    else:
        logger.info("Using Flask development server (debug mode)")
        app.run(host=host, port=port, debug=True)


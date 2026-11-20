"""
EXAMPLE: Updated server.py with security enhancements

This file shows the minimal changes needed to add signature authentication.
Only the /transcribe endpoint is shown as an example.
Apply the same pattern to other API endpoints.
"""

# ============================================
# ADD THIS IMPORT
# ============================================
from auth_middleware import require_device_auth

# ... rest of your imports remain unchanged ...

# ============================================
# EXAMPLE: Updated /transcribe endpoint
# ============================================
@app.route('/transcribe', methods=['POST'])
@require_device_auth(device_manager)  # ✅ ADD THIS LINE
def transcribe():
    """
    Transcribe audio and parse command.
    
    Expected: multipart/form-data with 'audio' file
    Optional: 'conversation_id' header for multi-turn conversations
    """
    try:
        # ✅ Device is already authenticated by the decorator
        # No need for manual Device-ID validation - it's done in the decorator
        device_id = request.headers.get('Device-ID')
        device_info = device_manager.get_device(device_id)
        logger.info(f"Request from device {device_id} ({device_info.get('staff_name')})")
        
        # ❌ REMOVE THESE LINES (now handled by decorator):
        # if not device_id:
        #     return jsonify({'error': 'Device-ID header required'}), 400
        # if not device_manager.is_device_authorized(device_id):
        #     return jsonify({'error': 'Device not registered'}), 403
        
        # Get audio file
        if 'audio' not in request.files:
            return jsonify({'error': 'No audio file provided'}), 400
        
        audio_file = request.files['audio']
        if audio_file.filename == '':
            return jsonify({'error': 'Empty filename'}), 400
        
        # ... rest of your transcription logic remains unchanged ...
        
    except Exception as e:
        logger.error(f"Transcription error: {e}")
        return jsonify({'error': str(e)}), 500

# ============================================
# APPLY SAME PATTERN TO OTHER ENDPOINTS:
# ============================================

@app.route('/api/confirm', methods=['POST'])
@require_device_auth(device_manager)  # ✅ ADD THIS
def confirm_command():
    # ... existing code ...
    pass

@app.route('/api/cancel', methods=['POST'])
@require_device_auth(device_manager)  # ✅ ADD THIS
def cancel_conversation():
    # ... existing code ...
    pass

@app.route('/api/heartbeat', methods=['POST'])
@require_device_auth(device_manager)  # ✅ ADD THIS
def heartbeat():
    # ... existing code ...
    pass

@app.route('/api/ack', methods=['POST'])
@require_device_auth(device_manager)  # ✅ ADD THIS
def acknowledge_playback():
    # ... existing code ...
    pass

@app.route('/api/status', methods=['GET'])
@require_device_auth(device_manager)  # ✅ ADD THIS
def status():
    # ... existing code ...
    pass

# ============================================
# THAT'S IT! Just add the decorator to each endpoint.
# The middleware handles all the signature validation.
# ============================================

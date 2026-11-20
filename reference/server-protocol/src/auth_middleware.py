"""
Authentication middleware with HMAC-SHA256 signature validation and timestamp freshness.
No nonce, no replay cache - just HMAC + 60-second timestamp window.
"""
import hmac
import hashlib
import logging
from functools import wraps
from flask import request, jsonify
from datetime import datetime

logger = logging.getLogger(__name__)

def require_device_auth(device_manager):
    """
    ESP32 device authentication decorator.
    
    Requires exactly these headers:
    - X-Device-ID: MAC address (e.g., "AA:BB:CC:DD:EE:FF")
    - X-Device-Time: ISO 8601 UTC timestamp (e.g., "2025-10-26T11:09:19Z")
    - X-Device-Signature: HMAC-SHA256(secret, "{deviceId}|{deviceTime}") lowercase hex
    
    Validates:
    1. All 3 headers present → 400 if missing
    2. Device registered → 403 if not found
    3. Timestamp fresh (≤60s) → 401 if stale
    4. Signature matches → 401 if bad
    
    On success: Sets request.device = {'id': ..., 'time': ..., 'info': ...}
    """
    def decorator(f):
        @wraps(f)
        def decorated_function(*args, **kwargs):
            # 1. Get required headers
            device_id = request.headers.get('X-Device-ID')
            device_time = request.headers.get('X-Device-Time')
            device_sig = request.headers.get('X-Device-Signature')
            
            # Check all headers present
            if not device_id or not device_time or not device_sig:
                missing = []
                if not device_id: missing.append('X-Device-ID')
                if not device_time: missing.append('X-Device-Time')
                if not device_sig: missing.append('X-Device-Signature')
                return jsonify({'error': 'missing auth headers', 'missing': missing}), 400
            
            # 2. Lookup device
            dev = device_manager.get_device(device_id)
            if not dev:
                logger.warning(f"❌ Unregistered device: {device_id}")
                return jsonify({'error': 'Device not registered'}), 403
            
            # Get device secret
            secret = dev.get('secret')
            if not secret:
                logger.error(f"❌ Device {device_id} has no secret in devices.yaml")
                return jsonify({'error': 'Device configuration error'}), 500
            
            # 3. Timestamp freshness (60 second window)
            try:
                claimed_dt = datetime.fromisoformat(device_time.replace('Z', '+00:00'))
                # Make now_dt timezone-aware to match claimed_dt
                from datetime import timezone
                now_dt = datetime.now(timezone.utc)
                time_diff = abs((now_dt - claimed_dt).total_seconds())
                
                if time_diff > 60:
                    logger.warning(f"❌ Stale timestamp from {device_id}: {time_diff:.1f}s skew")
                    return jsonify({'error': 'stale timestamp', 'skew_seconds': round(time_diff, 1)}), 401
                
                logger.debug(f"⏱️  Timestamp OK: {device_id} ({time_diff:.1f}s skew)")
                
            except (ValueError, AttributeError):
                logger.warning(f"❌ Bad timestamp format from {device_id}: {device_time}")
                return jsonify({'error': 'bad timestamp'}), 400
            
            # 4. HMAC signature verification
            # Message format: "{deviceId}|{deviceTime}"
            message = f"{device_id}|{device_time}"
            
            # Decode hex secret to bytes
            try:
                secret_bytes = bytes.fromhex(secret)
            except ValueError:
                logger.error(f"❌ Invalid hex secret for device {device_id}")
                return jsonify({'error': 'Device configuration error - bad secret format'}), 500
            
            expected_sig = hmac.new(
                secret_bytes,
                message.encode('utf-8'),
                hashlib.sha256
            ).hexdigest().lower()
            
            # Constant-time comparison
            if not hmac.compare_digest(device_sig, expected_sig):
                logger.warning(f"❌ Signature mismatch for {device_id}")
                return jsonify({'error': 'bad signature'}), 401
            
            # ✅ Success!
            device_manager.update_last_seen(device_id)
            logger.info(f"✅ Auth OK: {device_id} ({dev.get('staff_name', 'Unknown')})")
            
            # Attach device info for downstream use
            request.device = {
                'id': device_id,
                'time': device_time,
                'info': dev
            }
            
            return f(*args, **kwargs)
        
        return decorated_function
    return decorator

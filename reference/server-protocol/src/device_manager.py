"""
Device management for multi-device support.
Each device has its own Phoneshop credentials for tracking.
"""
import os
import yaml
import logging
import secrets
from typing import Optional, Dict
from datetime import datetime

logger = logging.getLogger(__name__)

class DeviceManager:
    """Manages registered ESP32 devices and their credentials"""
    
    def __init__(self):
        self.devices_file = os.path.join(
            os.path.dirname(__file__), 
            '..', 
            'config', 
            'devices.yaml'
        )
        self.devices = self._load_devices()
    
    def _load_devices(self) -> dict:
        """Load device registry from YAML"""
        try:
            with open(self.devices_file, 'r') as f:
                data = yaml.safe_load(f)
                return data.get('devices', {})
        except Exception as e:
            logger.error(f"Failed to load devices: {e}")
            return {}
    
    def get_device(self, device_id: str) -> Optional[Dict]:
        """
        Get device information by ID.
        
        Args:
            device_id: Device MAC address (e.g., "AA:BB:CC:DD:EE:01")
            
        Returns:
            Device info dict or None if not found
        """
        device = self.devices.get(device_id)
        if device and device.get('enabled', True):
            return device
        return None
    
    def get_phoneshop_credentials(self, device_id: str) -> Optional[tuple]:
        """
        Get Phoneshop credentials for a device.
        
        Args:
            device_id: Device MAC address
            
        Returns:
            (username, password) tuple or None
        """
        device = self.get_device(device_id)
        if device:
            return (
                device.get('phoneshop_username'),
                device.get('phoneshop_password')
            )
        return None
    
    def register_device(self, device_id: str, staff_name: str = None, 
                       phoneshop_username: str = None, phoneshop_password: str = None) -> str:
        """
        Register a new device and generate a unique secret.
        
        Args:
            device_id: Device MAC address (e.g., "AA:BB:CC:DD:EE:FF")
            staff_name: Staff member name (optional)
            phoneshop_username: Phoneshop login username (optional)
            phoneshop_password: Phoneshop login password (optional)
            
        Returns:
            The generated secret (64-char hex string)
        """
        # If device already exists, return existing secret
        if device_id in self.devices:
            existing_device = self.devices[device_id]
            existing_device['last_seen'] = datetime.now().isoformat()
            self._save_devices()
            logger.info(f"Device {device_id} already registered, returning existing secret")
            return existing_device.get('secret', '')
        
        # Generate a new random secret (32 bytes = 64 hex chars)
        secret = secrets.token_hex(32)
        
        # Store device with secret
        self.devices[device_id] = {
            'device_id': device_id,
            'secret': secret,
            'registered_at': datetime.now().isoformat(),
            'last_seen': datetime.now().isoformat(),
            'staff_name': staff_name or 'Unknown',
            'phoneshop_username': phoneshop_username or '',
            'phoneshop_password': phoneshop_password or '',
            'enabled': True
        }
        
        # Save to file
        self._save_devices()
        logger.info(f"Registered new device: {device_id} with generated secret")
        
        return secret
    
    def _save_devices(self):
        """Save device registry to YAML"""
        try:
            with open(self.devices_file, 'r') as f:
                data = yaml.safe_load(f)
            
            data['devices'] = self.devices
            
            with open(self.devices_file, 'w') as f:
                yaml.dump(data, f, default_flow_style=False)
        
        except Exception as e:
            logger.error(f"Failed to save devices: {e}")
    
    def is_device_authorized(self, device_id: str) -> bool:
        """Check if device is registered and enabled"""
        device = self.get_device(device_id)
        return device is not None
    
    def update_last_seen(self, device_id: str):
        """Update the last_seen timestamp for a device"""
        if device_id in self.devices:
            self.devices[device_id]['last_seen'] = datetime.now().isoformat()
            # Don't save to file every time for performance - do periodic saves
    
    def get_secret(self, device_id: str) -> Optional[str]:
        """
        Get the HMAC secret for a device.
        
        Args:
            device_id: Device MAC address
            
        Returns:
            Secret hex string or None if device not found
        """
        device = self.get_device(device_id)
        if device:
            return device.get('secret')
        return None

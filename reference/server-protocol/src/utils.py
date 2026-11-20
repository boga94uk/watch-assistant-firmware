"""
Utility functions for the Voice Assistant Server.
"""
import os
import yaml
import logging
from typing import Dict, Any
from dotenv import load_dotenv

def load_config() -> Dict[str, Any]:
    """Load configuration from config.yaml"""
    config_path = os.path.join(os.path.dirname(__file__), '..', 'config', 'config.yaml')
    with open(config_path, 'r') as f:
        return yaml.safe_load(f)

def load_commands() -> Dict[str, Any]:
    """Load command definitions from commands.yaml"""
    commands_path = os.path.join(os.path.dirname(__file__), '..', 'config', 'commands.yaml')
    with open(commands_path, 'r') as f:
        return yaml.safe_load(f)

def load_env():
    """Load environment variables from .env"""
    env_path = os.path.join(os.path.dirname(__file__), '..', 'config', '.env')
    load_dotenv(env_path)

def setup_logging(config: Dict[str, Any]) -> logging.Logger:
    """Setup logging configuration"""
    log_config = config['logging']
    log_dir = os.path.join(os.path.dirname(__file__), '..', 'logs')
    os.makedirs(log_dir, exist_ok=True)
    
    # Configure handlers with UTF-8 encoding for emoji support on Windows
    import sys
    
    file_handler = logging.FileHandler(
        os.path.join(log_dir, 'server.log'),
        encoding='utf-8'
    )
    
    # StreamHandler with UTF-8 for console (fixes Windows cp1252 emoji issues)
    try:
        # Try to use UTF-8 for console output
        stream_handler = logging.StreamHandler(
            open(sys.stdout.fileno(), mode='w', encoding='utf-8', buffering=1, closefd=False)
        )
    except Exception:
        # Fallback: use default StreamHandler with error replacement
        stream_handler = logging.StreamHandler(sys.stdout)
        # This will replace unencodable characters with '?' instead of crashing
    
    logging.basicConfig(
        level=getattr(logging, log_config['level']),
        format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
        handlers=[file_handler, stream_handler]
    )
    return logging.getLogger(__name__)

def get_audio_temp_dir() -> str:
    """Get audio temporary directory path"""
    temp_dir = os.path.join(os.path.dirname(__file__), '..', 'audio_temp')
    os.makedirs(temp_dir, exist_ok=True)
    return temp_dir

def sanitize_for_display(text: str) -> str:
    """Convert Unicode quotes and symbols to ASCII for ESP32 display compatibility"""
    if not text:
        return text
    
    replacements = {
        '\u2018': "'",  # ' → '
        '\u2019': "'",  # ' → '
        '\u201C': '"',  # " → "
        '\u201D': '"',  # " → "
        '\u2013': '-',  # – → -
        '\u2014': '--', # — → --
        '\u2026': '...', # … → ...
        '\u00a3': 'GBP' # £ → GBP
    }
    
    for unicode_char, ascii_char in replacements.items():
        text = text.replace(unicode_char, ascii_char)
    
    return text

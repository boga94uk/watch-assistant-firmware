"""
Per-device logging system for conversation tracking and debugging.
Each device gets its own daily log file for easy analysis.
"""
import os
import logging
from datetime import datetime
from typing import Optional

class DeviceLogger:
    """Manages per-device log files"""
    
    def __init__(self, logs_dir: str = None):
        self.logs_dir = logs_dir or os.path.join(
            os.path.dirname(__file__),
            '..',
            'logs'
        )
        os.makedirs(self.logs_dir, exist_ok=True)
        self.device_loggers = {}
    
    def get_logger(self, device_id: str) -> logging.Logger:
        """
        Get or create a logger for a specific device.
        
        Args:
            device_id: Device identifier (MAC address)
            
        Returns:
            Logger instance for this device
        """
        # Return cached logger if exists
        if device_id in self.device_loggers:
            return self.device_loggers[device_id]
        
        # Sanitize device_id for filename (replace colons with hyphens for Windows)
        safe_device_id = device_id.replace(':', '-')
        
        # Create new logger
        logger_name = f"device_{device_id}"
        device_logger = logging.getLogger(logger_name)
        device_logger.setLevel(logging.INFO)
        device_logger.propagate = False  # Don't propagate to root logger
        
        # Remove existing handlers
        device_logger.handlers.clear()
        
        # Create daily log file for this device
        today = datetime.now().strftime("%Y-%m-%d")
        log_file = os.path.join(self.logs_dir, f"device_{safe_device_id}_{today}.log")
        
        # File handler with detailed format
        file_handler = logging.FileHandler(log_file, encoding='utf-8')
        file_handler.setLevel(logging.INFO)
        
        formatter = logging.Formatter(
            '%(asctime)s | %(levelname)-8s | %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        file_handler.setFormatter(formatter)
        device_logger.addHandler(file_handler)
        
        # Cache and return
        self.device_loggers[device_id] = device_logger
        return device_logger
    
    def log_request(self, device_id: str, conversation_id: str, 
                   transcription: str, parsed_command: dict):
        """
        Log incoming request from device.
        
        Args:
            device_id: Device identifier
            conversation_id: Conversation ID
            transcription: Transcribed text
            parsed_command: Parsed command dict
        """
        logger = self.get_logger(device_id)
        logger.info(f"[{conversation_id}] REQUEST")
        logger.info(f"[{conversation_id}] Transcription: {transcription}")
        logger.info(f"[{conversation_id}] Parsed: {parsed_command}")
    
    def log_api_call(self, device_id: str, conversation_id: str,
                    endpoint: str, params: dict, response_code: int, 
                    response_data: Optional[dict] = None):
        """
        Log Phoneshop API call.
        
        Args:
            device_id: Device identifier
            conversation_id: Conversation ID
            endpoint: API endpoint
            params: Request parameters
            response_code: HTTP response code
            response_data: Response data (optional)
        """
        logger = self.get_logger(device_id)
        logger.info(f"[{conversation_id}] API_CALL: {endpoint}")
        logger.info(f"[{conversation_id}] API_PARAMS: {params}")
        logger.info(f"[{conversation_id}] API_RESPONSE: {response_code}")
        if response_data:
            logger.info(f"[{conversation_id}] API_DATA: {response_data}")
    
    def log_tts(self, device_id: str, conversation_id: str,
               text: str, audio_file: str):
        """
        Log TTS generation.
        
        Args:
            device_id: Device identifier
            conversation_id: Conversation ID
            text: TTS text
            audio_file: Generated audio file path
        """
        logger = self.get_logger(device_id)
        logger.info(f"[{conversation_id}] TTS_TEXT: {text}")
        logger.info(f"[{conversation_id}] TTS_FILE: {audio_file}")
    
    def log_error(self, device_id: str, conversation_id: str,
                 error_type: str, error_message: str):
        """
        Log error.
        
        Args:
            device_id: Device identifier
            conversation_id: Conversation ID
            error_type: Type of error
            error_message: Error message
        """
        logger = self.get_logger(device_id)
        logger.error(f"[{conversation_id}] ERROR: {error_type} - {error_message}")
    
    def log_conversation_flow(self, device_id: str, conversation_id: str,
                             flow_type: str, details: str = ""):
        """
        Log conversation flow events (ASK, CONFIRM, SAY).
        
        Args:
            device_id: Device identifier
            conversation_id: Conversation ID
            flow_type: Flow type (ASK, CONFIRM, SAY, etc.)
            details: Additional details
        """
        logger = self.get_logger(device_id)
        logger.info(f"[{conversation_id}] FLOW: {flow_type} {details}")
    
    def log_llm_prompt(self, device_id: str, conversation_id: str,
                      prompt_type: str, messages: list, model: str = ""):
        """
        Log LLM prompt sent to OpenAI.
        
        Args:
            device_id: Device identifier
            conversation_id: Conversation ID
            prompt_type: Type of prompt (COMMAND_PARSE, CONVERSATION, etc.)
            messages: List of message dicts sent to LLM
            model: Model name (optional)
        """
        logger = self.get_logger(device_id)
        logger.info(f"[{conversation_id}] LLM_PROMPT ({prompt_type})")
        if model:
            logger.info(f"[{conversation_id}] LLM_MODEL: {model}")
        logger.info(f"[{conversation_id}] LLM_MESSAGES:")
        for i, msg in enumerate(messages, 1):
            role = msg.get('role', 'unknown')
            content = msg.get('content', '')
            # Log message with proper formatting
            logger.info(f"[{conversation_id}]   Message {i} [{role}]: {content}")
    
    def log_llm_response(self, device_id: str, conversation_id: str,
                        response_type: str, response_data: dict):
        """
        Log LLM response from OpenAI.
        
        Args:
            device_id: Device identifier
            conversation_id: Conversation ID
            response_type: Type of response (COMMAND_PARSE, CONVERSATION, etc.)
            response_data: Parsed response data
        """
        logger = self.get_logger(device_id)
        logger.info(f"[{conversation_id}] LLM_RESPONSE ({response_type})")
        logger.info(f"[{conversation_id}] LLM_DATA: {response_data}")

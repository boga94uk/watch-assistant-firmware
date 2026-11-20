"""
Multi-turn conversation management for incomplete commands.
"""
import uuid
import logging
import re
from typing import Dict, Optional
from datetime import datetime, timedelta

logger = logging.getLogger(__name__)

class ConversationManager:
    """Manages multi-turn conversations for incomplete commands"""
    
    def __init__(self, timeout_minutes: int = 5):
        self.conversations = {}
        self.timeout = timedelta(minutes=timeout_minutes)
        self.last_failed_command = {}  # device_id -> (command, params)
    
    def create(self, command: str, params: Dict, missing: list, original_transcription: str = None) -> str:
        """
        Create new conversation.
        
        Args:
            command: Command name
            params: Already collected parameters
            missing: List of missing parameters
            original_transcription: Original voice transcription for learning corrections
            
        Returns:
            Conversation ID
        """
        conv_id = str(uuid.uuid4())
        self.conversations[conv_id] = {
            'command': command,
            'params': params,
            'missing': missing,
            'created_at': datetime.now(),
            'history': [],
            'transcript': [],        # Full conversation transcript (user + assistant)
            'last_response': None,   # Last assistant response (for repeat_last)
            'correction_count': 0,   # Track number of corrections
            'question_count': 0,     # Track number of questions asked (max 3)
            'type': None,            # Conversation type: 'ask', 'confirm', 'incomplete'
            'original_transcription': original_transcription  # For learning transcription errors
        }
        logger.info(f"Created conversation {conv_id} for command '{command}'")
        return conv_id
    
    def update(self, conv_id: str, param_name: str, value: str) -> Optional[Dict]:
        """
        Update conversation with new parameter value.
        
        Args:
            conv_id: Conversation ID
            param_name: Parameter name to update
            value: Parameter value
            
        Returns:
            Updated conversation or None if not found
        """
        conv = self.conversations.get(conv_id)
        if not conv:
            logger.warning(f"Conversation {conv_id} not found")
            return None
        
        # Update parameter
        conv['params'][param_name] = value
        conv['history'].append(('user', value))
        
        # Remove from missing list
        if param_name in conv['missing']:
            conv['missing'].remove(param_name)
        
        logger.info(f"Updated conversation {conv_id}: {param_name}={value}")
        return conv
    
    def get(self, conv_id: str) -> Optional[Dict]:
        """Get conversation by ID"""
        return self.conversations.get(conv_id)
    
    def complete(self, conv_id: str) -> bool:
        """
        Mark conversation as complete and remove it.
        
        Args:
            conv_id: Conversation ID
            
        Returns:
            True if conversation existed
        """
        if conv_id in self.conversations:
            logger.info(f"Completed conversation {conv_id}")
            del self.conversations[conv_id]
            return True
        return False
    
    def add_transcript(self, conv_id: str, role: str, message: str):
        """
        Add message to conversation transcript.
        
        Args:
            conv_id: Conversation ID
            role: 'user' or 'assistant'
            message: Message text
        """
        conv = self.conversations.get(conv_id)
        if conv:
            conv['transcript'].append({
                'role': role,
                'message': message,
                'timestamp': datetime.now().isoformat()
            })
            if role == 'assistant':
                conv['last_response'] = message
            logger.debug(f"Added {role} message to conversation {conv_id}")
    
    def get_transcript(self, conv_id: str) -> list:
        """
        Get full conversation transcript.
        
        Args:
            conv_id: Conversation ID
            
        Returns:
            List of transcript entries or empty list
        """
        conv = self.conversations.get(conv_id)
        return conv['transcript'] if conv else []
    
    def get_last_response(self, conv_id: str) -> Optional[str]:
        """
        Get last assistant response.
        
        Args:
            conv_id: Conversation ID
            
        Returns:
            Last response text or None
        """
        conv = self.conversations.get(conv_id)
        return conv['last_response'] if conv else None
    
    def should_reset_context(self, text: str, conv_id: Optional[str] = None) -> bool:
        """
        Check if text contains a new command keyword that should reset context.
        
        Args:
            text: User input text
            conv_id: Current conversation ID (if any)
            
        Returns:
            True if context should be reset
        """
        # Command keywords that start new actions
        command_keywords = r'\b(book|sell|invoice|ticket)\b'
        
        if re.search(command_keywords, text.lower()):
            if conv_id and conv_id in self.conversations:
                conv = self.conversations[conv_id]
                logger.info(f"New command keyword detected in '{text}' - resetting conversation {conv_id} (was: {conv['command']})")
                # Don't delete yet - let server decide
            return True
        return False
    
    def merge_params(self, conv_id: str, new_params: Dict) -> Optional[Dict]:
        """
        Merge new parameters into existing conversation params.
        
        Args:
            conv_id: Conversation ID
            new_params: New parameters to merge
            
        Returns:
            Updated conversation or None
        """
        conv = self.conversations.get(conv_id)
        if not conv:
            return None
        
        # Merge params
        conv['params'].update(new_params)
        
        # Update missing list - remove params that are now filled
        conv['missing'] = [p for p in conv['missing'] if p not in new_params]
        
        logger.info(f"Merged params into conversation {conv_id}: {new_params}")
        return conv
    
    def store_failed_command(self, device_id: str, command: str, params: Dict):
        """Store last failed command for retry"""
        self.last_failed_command[device_id] = {
            'command': command,
            'params': params,
            'timestamp': datetime.now()
        }
        logger.info(f"Stored failed command for device {device_id}: {command}")
    
    def get_failed_command(self, device_id: str) -> Optional[Dict]:
        """Get last failed command for retry"""
        return self.last_failed_command.get(device_id)
    
    def clear_failed_command(self, device_id: str):
        """Clear failed command after successful retry"""
        if device_id in self.last_failed_command:
            del self.last_failed_command[device_id]
            logger.info(f"Cleared failed command for device {device_id}")
    
    def cleanup_expired(self):
        """Remove expired conversations"""
        now = datetime.now()
        expired = [
            conv_id for conv_id, conv in self.conversations.items()
            if now - conv['created_at'] > self.timeout
        ]
        for conv_id in expired:
            logger.info(f"Cleaning up expired conversation {conv_id}")
            del self.conversations[conv_id]

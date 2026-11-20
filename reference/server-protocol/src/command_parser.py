"""
Command parsing with regex pattern matching and parameter extraction.
"""
import re
import logging
from typing import Dict, Optional, Tuple, List

logger = logging.getLogger(__name__)

class CommandParser:
    """Parses voice commands using regex patterns with LLM assistance"""
    
    def __init__(self, commands_config: dict, llm_parser=None):
        self.commands = commands_config['commands']
        self.help_context = commands_config.get('help_context', '')
        self.llm_parser = llm_parser  # Optional LLM parser for smart extraction
    
    def parse_with_regex(self, text: str) -> Optional[Dict]:
        """
        Parse text using ONLY regex patterns (no LLM fallback).
        Used for Layer 1 routing - fast path for simple commands.
        
        Args:
            text: Transcribed text to parse
            
        Returns:
            Dict with parsing result if regex matched, None otherwise
        """
        text = text.strip().lower()
        
        # Try to match command patterns
        for cmd_name, cmd_info in self.commands.items():
            pattern = cmd_info.get('pattern')
            if pattern:  # Only try regex if pattern exists
                match = re.match(pattern, text, re.IGNORECASE)
                if match:
                    return self._process_match(cmd_name, cmd_info, match, text)
        
        # No regex match
        return None
        
    def parse(self, text: str, device_id: str = None, conversation_id: str = None) -> Dict:
        """
        Parse text into command structure.
        
        Args:
            text: Transcribed text to parse
            device_id: Device identifier for logging
            conversation_id: Conversation ID for logging
            
        Returns:
            Dict with parsing result
        """
        text = text.strip().lower()
        
        # Try to match command patterns FIRST (only for commands with patterns)
        for cmd_name, cmd_info in self.commands.items():
            pattern = cmd_info.get('pattern')
            if pattern:  # Only try regex if pattern exists
                match = re.match(pattern, text, re.IGNORECASE)
                if match:
                    return self._process_match(cmd_name, cmd_info, match, text)
        
        # Check if it's a general help/status question BEFORE trying LLM parse
        # This prevents "find my repair" from being interpreted as "book"
        if self._is_help_question(text):
            return {
                'type': 'help',
                'question': text
            }
        
        # Try LLM-assisted parsing for book/sell commands if available
        if self.llm_parser:
            llm_result = self._try_llm_parse(text, device_id, conversation_id)
            if llm_result:
                return llm_result
        
        # No match - unknown command
        return {
            'type': 'unknown',
            'text': text
        }
    
    def _is_help_question(self, text: str) -> bool:
        """Check if text is a help/status question"""
        help_indicators = [
            'how', 'what', 'why', 'when', 'where', 'can i', 'help',
            'explain', 'show me', 'teach', 'guide', 'instructions',
            'is the', 'is my', 'is it', 'are the', 'are my', 'was the', 'was my',
            'does the', 'do i', 'do you', 'will the', 'has the', 'have you',
            'status', 'ready', 'finished', 'done', 'complete', 'completed',
            'find my', 'find the', 'check my', 'check the', 'tell me',
            'any updates', 'update', 'progress', 'i left', 'i gave you'
        ]
        text_lower = text.lower()
        return any(indicator in text_lower for indicator in help_indicators) or text.endswith('?')
    
    def _try_llm_parse(self, text: str, device_id: str = None, conversation_id: str = None) -> Optional[Dict]:
        """
        Try to parse command using LLM for flexible natural language understanding.
        """
        if not self.llm_parser:
            return None
        
        # Check if it looks like a book command
        if any(word in text.lower() for word in ['book', 'repair', 'fix']):
            params = self.llm_parser.extract_book_params(text, device_id, conversation_id)
            
            # Check if LLM is asking for clarification
            if params.get('ask_clarification'):
                return {
                    'type': 'ask',
                    'question': params['ask_clarification'],
                    'command': 'book',
                    'params': {k: v for k, v in params.items() if k != 'ask_clarification' and v}
                }
            
            if params.get('brand') and params.get('problem'):
                missing = []
                if not params.get('model'):
                    missing.append('model')
                
                if missing:
                    return {
                        'type': 'incomplete',
                        'command': 'book',
                        'params': {k: v for k, v in params.items() if v},
                        'missing': missing,
                        'prompt': self.commands['book']['prompts'].get(missing[0])
                    }
                else:
                    return {
                        'type': 'command',
                        'command': 'book',
                        'params': params,
                        'requires_confirmation': True,
                        'confirmation_template': self.commands['book']['confirmation_template'],
                        'device_command': False,
                        'api_endpoint': self.commands['book']['api_endpoint'],
                        'api_method': self.commands['book']['api_method'],
                        'auto_print': False
                    }
        
        # Check if it looks like a sell command
        if any(word in text.lower() for word in ['sell', 'sale']):
            params = self.llm_parser.extract_sell_params(text, device_id, conversation_id)
            if params.get('brand'):
                return {
                    'type': 'command',
                    'command': 'sell',
                    'params': params,
                    'requires_confirmation': True,
                    'confirmation_template': self.commands['sell']['confirmation_template'],
                    'device_command': False,
                    'api_endpoint': self.commands['sell']['api_endpoint'],
                    'api_method': self.commands['sell']['api_method'],
                    'auto_print': True
                }
        
        return None
    
    def _get_smart_prompt(self, cmd_info: dict, missing: List[str]) -> str:
        """
        Get smart prompt based on missing parameters.
        Combines multiple missing params into one question when possible.
        """
        prompts = cmd_info.get('prompts', {})
        
        # Check for combined prompts
        if len(missing) >= 3 and 'all' in prompts:
            return prompts['all']
        
        if set(missing) == {'brand', 'model'} and 'brand_model' in prompts:
            return prompts['brand_model']
        
        # Default: ask for first missing parameter
        return prompts.get(missing[0], f"Please provide {missing[0]}")
    
    def _process_match(self, cmd_name: str, cmd_info: dict, match: re.Match, original_text: str = "") -> Dict:
        """Process a successful pattern match"""
        groups = match.groups()
        required_params = cmd_info.get('required_params', [])
        
        # Extract parameters
        params = {}
        missing = []
        
        for i, param_name in enumerate(required_params):
            if i < len(groups) and groups[i] and groups[i].strip():
                params[param_name] = groups[i].strip()
            else:
                missing.append(param_name)
        
        # Normalize ticket_id: strip non-digits (handles "12.27" → "1227")
        if 'ticket_id' in params:
            params['ticket_id'] = re.sub(r'[^\d]', '', params['ticket_id'])
        
        # Normalize brand and model for book/sell commands
        if cmd_name in ['book', 'sell'] and 'brand' in params and 'model' in params:
            params['brand'], params['model'] = self._normalize_brand_model(
                params['brand'], 
                params['model']
            )
        
        # Check if incomplete
        if missing:
            # Smart prompt selection based on what's missing
            prompt = self._get_smart_prompt(cmd_info, missing)
            
            return {
                'type': 'incomplete',
                'command': cmd_name,
                'params': params,
                'missing': missing,
                'prompt': prompt
            }
        
        # Complete command
        return {
            'type': 'command',
            'command': cmd_name,
            'params': params,
            'requires_confirmation': cmd_info.get('requires_confirmation', False),
            'confirmation_template': cmd_info.get('confirmation_template', ''),
            'device_command': cmd_info.get('device_command', False),
            'api_endpoint': cmd_info.get('api_endpoint'),
            'api_method': cmd_info.get('api_method'),
            'auto_print': cmd_info.get('auto_print', False)
        }
    
    def _normalize_brand_model(self, brand: str, model: str) -> tuple:
        """
        Normalize brand and model names.
        Apple products: iPhone, iPad, MacBook, iMac -> brand="Apple", model="iPhone 15"
        Samsung: Galaxy S24 -> brand="Samsung", model="Galaxy S24"
        """
        # Remove articles if captured as brand
        if brand.lower() in ['a', 'an', 'the']:
            # Article captured as brand - extract brand from model
            parts = model.split(None, 1)  # Split on first whitespace
            if len(parts) >= 2:
                brand = parts[0]  # First word becomes brand
                model = parts[1]  # Rest becomes model
            else:
                brand = 'Generic'  # Single word, no brand
        
        brand_lower = brand.lower()
        model_lower = model.lower()
        
        # Apple products
        apple_products = ['iphone', 'ipad', 'macbook', 'imac', 'airpods', 'watch', 'apple']
        if any(product in brand_lower for product in apple_products):
            # Brand is Apple, model includes the product name
            if brand_lower.startswith('i') or brand_lower == 'macbook' or brand_lower == 'airpods':
                return 'Apple', f"{brand} {model}"
            else:
                return 'Apple', model
        
        # Samsung - check if model starts with Galaxy
        if 'samsung' in brand_lower or 'galaxy' in brand_lower:
            if 'galaxy' in brand_lower:
                # "Galaxy S24" -> brand="Samsung", model="Galaxy S24"
                return 'Samsung', f"{brand} {model}"
            elif 'galaxy' in model_lower:
                # "Samsung" "Galaxy S24" -> brand="Samsung", model="Galaxy S24"
                return 'Samsung', model
            else:
                # "Samsung" "S24" -> brand="Samsung", model="S24"
                return 'Samsung', model
        
        # Other brands - keep as is
        return brand, model
    
    def parse_multi_param_response(self, text: str, missing: List[str], command: str) -> Dict[str, str]:
        """
        Parse a response that may contain multiple parameters.
        E.g., "iPhone 15" -> {brand: "Apple", model: "iPhone 15"}
        """
        text = text.strip()
        words = text.split()
        params = {}
        
        # For brand_model combination
        if set(missing) == {'brand', 'model'} and len(words) >= 2:
            # First word(s) = brand, last word(s) = model
            # Handle cases like "Samsung Galaxy S24" -> brand="Samsung", model="Galaxy S24"
            if len(words) == 2:
                brand = words[0]
                model = words[1]
            else:
                # Assume last word is model, rest is brand
                brand = ' '.join(words[:-1])
                model = words[-1]
            
            # Normalize brand and model
            brand, model = self._normalize_brand_model(brand, model)
            params['brand'] = brand
            params['model'] = model
            
        elif len(missing) == 1:
            # Single parameter
            params[missing[0]] = text
        else:
            # Multiple different params - just assign to first missing
            params[missing[0]] = text
        
        return params
    
    def format_confirmation(self, cmd_result: Dict) -> str:
        """Format confirmation message from command result"""
        template = cmd_result.get('confirmation_template', '')
        params = cmd_result.get('params', {}).copy()
        
        # Clean up trailing periods from LLM output (except notes)
        for field in ['brand', 'model', 'problem']:
            if params.get(field):
                params[field] = params[field].rstrip('.')
        
        # Add conditional text fields for cleaner templates
        # deviceCondition: "Cracked Screen" → ", cracked screen"
        if params.get('deviceCondition'):
            params['condition_text'] = f", {params['deviceCondition'].lower()}"
        else:
            params['condition_text'] = ''
        
        # totalPrice: 75.0 → ", price £75"
        if params.get('totalPrice'):
            params['price_text'] = f", price £{params['totalPrice']}"
        else:
            params['price_text'] = ''
        
        try:
            return template.format(**params)
        except KeyError as e:
            logger.error(f"Missing parameter in confirmation template: {e}")
            return f"Confirm: {cmd_result['command']} with {params}?"

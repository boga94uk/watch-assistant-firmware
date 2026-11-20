"""
Unified Command Extractor
Single entry point for all command extraction using LLM
"""
import json
import logging
import yaml
from typing import Dict, Optional, List
from pathlib import Path
from openai import OpenAI

logger = logging.getLogger(__name__)


class UnifiedCommandExtractor:
    """
    Unified command extraction using LLM.
    Handles: book, sell, invoice, ticket_add_note commands.
    """
    
    def __init__(self, config: Dict, config_dir: Path):
        """
        Initialize with OpenAI config and load schemas/prompts.
        
        Args:
            config: OpenAI configuration dict
            config_dir: Path to config directory
        """
        self.config = config
        self.config_dir = config_dir
        
        # Load command schema
        schema_path = config_dir / 'command_schema.yaml'
        with open(schema_path, 'r', encoding='utf-8') as f:
            self.command_schema = yaml.safe_load(f)
        
        # Load prompts
        prompts_path = config_dir / 'llm_prompts.yaml'
        with open(prompts_path, 'r', encoding='utf-8') as f:
            self.prompts = yaml.safe_load(f)
        
        # Load brand/model knowledge base
        knowledge_path = config_dir / 'brand_model_knowledge.yaml'
        if knowledge_path.exists():
            with open(knowledge_path, 'r', encoding='utf-8') as f:
                self.brand_knowledge = yaml.safe_load(f)
        else:
            self.brand_knowledge = {'examples': []}
        
        # Load transcription corrections
        tc_path = config_dir / 'transcription_corrections.yaml'
        if tc_path.exists():
            with open(tc_path, 'r', encoding='utf-8') as f:
                self.transcription_corrections = yaml.safe_load(f)
                logger.info(f"✅ Loaded {len(self.transcription_corrections.get('common_errors', []))} transcription correction rules")
        else:
            self.transcription_corrections = {'common_errors': []}
            logger.warning("No transcription corrections file found")
        
        # Initialize OpenAI client
        self.client = OpenAI(api_key=config.get('api_key'))
        
        logger.info("✅ Unified Command Extractor initialized")
    
    def extract_command(self, text: str, conversation_context: Optional[Dict] = None) -> Dict:
        """
        Extract command from natural language text.
        
        Args:
            text: User's natural language command
            conversation_context: Optional context from ongoing conversation
            
        Returns:
            {
                'command': 'book' | 'sell' | 'invoice' | 'ticket_add_note' | 'unknown',
                'params': dict of extracted parameters,
                'missing_params': list of required params not found,
                'confidence': float 0.0-1.0
            }
        """
        try:
            # Build prompt
            system_prompt = self._build_extraction_prompt()
            user_prompt = self.prompts['command_extraction']['user_template'].format(
                user_query=text
            )
            
            # Add conversation context if available
            if conversation_context:
                user_prompt += f"\n\nContext from previous turn: {json.dumps(conversation_context)}"
            
            messages = [
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt}
            ]
            
            # Call LLM
            response = self.client.chat.completions.create(
                model=self.config.get('model', 'gpt-4o-mini'),
                messages=messages,
                temperature=0.3,  # Lower for more consistent extraction
                max_tokens=500
            )
            
            # Parse JSON response
            result = json.loads(response.choices[0].message.content)
            
            # Validate and normalize
            result = self._validate_extraction(result)
            
            logger.info(f"✅ Extracted command: {result['command']} (confidence: {result['confidence']})")
            
            return result
            
        except json.JSONDecodeError as e:
            logger.error(f"❌ Failed to parse LLM response as JSON: {e}")
            return {
                'command': 'unknown',
                'params': {},
                'missing_params': [],
                'confidence': 0.0,
                'error': 'Failed to parse response'
            }
        except Exception as e:
            logger.error(f"❌ Command extraction failed: {e}")
            return {
                'command': 'unknown',
                'params': {},
                'missing_params': [],
                'confidence': 0.0,
                'error': str(e)
            }
    
    def _build_extraction_prompt(self) -> str:
        """Build the system prompt for command extraction."""
        base_prompt = self.prompts['command_extraction']['system']
        
        # Add command schemas
        schemas_text = "\n\nCOMMAND SCHEMAS:\n"
        for cmd_name, cmd_config in self.command_schema['commands'].items():
            # Skip regex-based commands
            if 'pattern' in cmd_config:
                continue
            
            schemas_text += f"\n{cmd_name}:\n"
            schemas_text += f"  Description: {cmd_config['description']}\n"
            schemas_text += f"  Required: {', '.join(cmd_config['required_params'])}\n"
            if cmd_config.get('optional_params'):
                schemas_text += f"  Optional: {', '.join(cmd_config['optional_params'])}\n"
        
        # Add allowed values
        schemas_text += "\n\nALLOWED VALUES:\n"
        for field, values in self.command_schema['allowed_values'].items():
            schemas_text += f"\n{field}:\n"
            for value in values:
                schemas_text += f"  - {value}\n"
        
        # Add transcription corrections
        tc_hint = ""
        if self.transcription_corrections.get('common_errors'):
            tc_hint = "\n\n⚠️ KNOWN TRANSCRIPTION ERRORS (Voice-to-text often makes these mistakes):\n"
            tc_hint += "If you see these words in user input, they are HIGHLY LIKELY transcription errors:\n"
            for error in self.transcription_corrections['common_errors'][:10]:  # Top 10
                tc_hint += f"  • '{error['incorrect']}' → should be '{error['correct']}' | Context: {error.get('context', 'Common error')}\n"
            tc_hint += "\n✅ ALWAYS check if any word in the user input matches these errors and use the correct version!\n"
        
        return base_prompt + schemas_text + tc_hint
    
    def _validate_extraction(self, result: Dict) -> Dict:
        """
        Validate and normalize extracted command.
        
        Args:
            result: Raw extraction result
            
        Returns:
            Validated and normalized result
        """
        command = result.get('command', 'unknown')
        params = result.get('params', {})
        
        # Get command schema
        if command not in self.command_schema['commands']:
            return result
        
        cmd_schema = self.command_schema['commands'][command]
        
        # Check for missing required params
        missing_params = []
        for required in cmd_schema.get('required_params', []):
            if required not in params or not params[required]:
                missing_params.append(required)
        
        # Normalize brand/model
        if 'brand' in params and params['brand']:
            params['brand'] = self._normalize_brand(params['brand'])
        
        if 'model' in params and params['model']:
            params['model'] = self._normalize_model(params['model'], params.get('brand'))
        
        # Clean up problem text (remove price mentions)
        if 'problem' in params and params['problem'] and 'totalPrice' not in params:
            params['problem'], extracted_price = self._extract_price_from_text(params['problem'])
            if extracted_price:
                params['totalPrice'] = extracted_price
        
        # Format accessories and deviceCondition with trailing ", "
        for field in ['accessories', 'deviceCondition']:
            if field in params and params[field]:
                if not params[field].endswith(', '):
                    params[field] = params[field] + ', '
        
        # Update result
        result['params'] = params
        result['missing_params'] = missing_params
        
        # Adjust confidence based on missing params
        if missing_params and result.get('confidence', 0) > 0.5:
            result['confidence'] = 0.5
        
        return result
    
    def _normalize_brand(self, brand: str) -> str:
        """Normalize brand name."""
        brand = brand.strip()
        
        # Common mappings
        brand_mappings = {
            'iphone': 'Apple',
            'ipad': 'Apple',
            'macbook': 'Apple',
            'airpods': 'Apple',
            'apple watch': 'Apple',
            'galaxy': 'Samsung',
            'xperia': 'Sony',
        }
        
        brand_lower = brand.lower()
        for key, normalized in brand_mappings.items():
            if key in brand_lower:
                return normalized
        
        # Capitalize first letter
        return brand.title()
    
    def _normalize_model(self, model: str, brand: Optional[str] = None) -> str:
        """Normalize model name."""
        model = model.strip()
        
        # Remove brand from model if it's there
        if brand and brand.lower() in model.lower():
            model = model.lower().replace(brand.lower(), '').strip()
        
        # Samsung Galaxy formatting
        if brand == 'Samsung' and 'galaxy' not in model.lower():
            if any(series in model.upper() for series in ['S', 'A', 'M', 'Z', 'NOTE']):
                model = f"Galaxy {model}"
        
        return model.strip()
    
    def _extract_price_from_text(self, text: str) -> tuple[str, Optional[float]]:
        """
        Extract price from text and return cleaned text + price.
        
        Args:
            text: Text that may contain price
            
        Returns:
            (cleaned_text, price or None)
        """
        import re
        
        # Patterns: "price 75", "£75", "75 pounds"
        patterns = [
            r'price\s*[£]?(\d+(?:\.\d{2})?)',
            r'[£](\d+(?:\.\d{2})?)',
            r'(\d+(?:\.\d{2})?)\s*pounds?'
        ]
        
        for pattern in patterns:
            match = re.search(pattern, text, re.IGNORECASE)
            if match:
                price = float(match.group(1))
                cleaned_text = re.sub(pattern, '', text, flags=re.IGNORECASE).strip()
                cleaned_text = re.sub(r'\s+', ' ', cleaned_text)  # Clean extra spaces
                cleaned_text = cleaned_text.rstrip(',').strip()
                return cleaned_text, price
        
        return text, None
    
    def get_clarification_prompt(self, missing_param: str, params: Dict) -> str:
        """
        Get clarification prompt for missing parameter.
        
        Args:
            missing_param: Name of missing parameter
            params: Currently extracted parameters
            
        Returns:
            Clarification question string
        """
        clarifications = self.prompts['clarification']
        
        # Get template
        template_key = f"missing_{missing_param}"
        if template_key in clarifications:
            template = clarifications[template_key]
            return template.format(**params)
        
        # Generic fallback
        return f"Could you provide the {missing_param.replace('_', ' ')}?"
    
    def get_command_config(self, command: str) -> Optional[Dict]:
        """Get command configuration from schema."""
        return self.command_schema['commands'].get(command)

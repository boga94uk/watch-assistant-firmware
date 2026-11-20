"""
LLM-powered parameter extraction for natural language commands.
Uses GPT-4o-mini with learned examples for accurate brand/model parsing.
"""
import os
import json
import logging
import yaml
import httpx
from typing import Dict, Optional
from openai import OpenAI
from tenacity import retry, stop_after_attempt, wait_exponential, retry_if_exception_type

logger = logging.getLogger(__name__)

class LLMParser:
    """Uses LLM to extract structured parameters from natural language"""
    
    def __init__(self, config: dict, openai_api_key: str, device_logger=None):
        self.config = config
        # Create httpx client with 30s timeout for OpenAI API calls
        http_client = httpx.Client(timeout=30.0)
        self.client = OpenAI(api_key=openai_api_key, http_client=http_client)
        self.knowledge_base = self._load_knowledge_base()
        self.transcription_corrections = self._load_transcription_corrections()
        self.device_logger = device_logger  # For logging LLM interactions
        
        # Command-specific caches of successful extractions (last 100 per command)
        self.success_caches = {
            'book': [],
            'sell': [],
            'status': []
        }
        self.max_cache_size = 100
        
    def _load_knowledge_base(self) -> dict:
        """Load brand/model knowledge base"""
        kb_path = os.path.join(
            os.path.dirname(__file__), 
            '..', 
            'config', 
            'brand_model_knowledge.yaml'
        )
        try:
            with open(kb_path, 'r') as f:
                return yaml.safe_load(f)
        except Exception as e:
            logger.warning(f"Could not load knowledge base: {e}")
            return {'examples': [], 'parsing_rules': []}
    
    def _load_transcription_corrections(self) -> dict:
        """Load common transcription errors/corrections"""
        self.tc_path = os.path.join(
            os.path.dirname(__file__), 
            '..', 
            'config', 
            'transcription_corrections.yaml'
        )
        try:
            with open(self.tc_path, 'r', encoding='utf-8') as f:
                corrections = yaml.safe_load(f)
                logger.info(f"✅ Loaded {len(corrections.get('common_errors', []))} transcription correction rules")
                return corrections
        except Exception as e:
            logger.warning(f"Could not load transcription corrections: {e}")
            return {'common_errors': []}
    
    def learn_from_correction(self, 
                                original_transcription: str, 
                                correction_transcription: str,
                                corrected_params: Dict, 
                                original_params: Dict,
                                command: str):
        """
        Detect transcription errors from user corrections and add to knowledge base.
        Uses edit distance to find phonetically similar word pairs only.
        
        Args:
            original_transcription: Full original voice transcription (e.g., "Book iPhone 13 for green")
            correction_transcription: Full correction transcription (e.g., "No, screen")
            corrected_params: Parameters after user correction
            original_params: Parameters before user correction
            command: Command type (book, sell, etc.) for context
        """
        try:
            # Find which parameters changed
            for param, corrected_value in corrected_params.items():
                if param in original_params:
                    original_value = original_params[param]
                    
                    # Parameter value changed
                    if original_value != corrected_value and isinstance(corrected_value, str) and isinstance(original_value, str):
                        # Simple case: single word change
                        orig_words = original_value.lower().split()
                        corr_words = corrected_value.lower().split()
                        
                        # Case 1: Both single words (e.g., "green" → "screen")
                        if len(orig_words) == 1 and len(corr_words) == 1:
                            orig_word = orig_words[0]
                            corr_word = corr_words[0]
                            
                            # Check if this is a likely transcription error
                            if self._is_transcription_error(orig_word, corr_word, 
                                                           original_transcription, 
                                                           correction_transcription):
                                self._add_if_new(orig_word, corr_word, original_transcription, command, param)
                        
                        # Case 2: Multi-word, find which word changed
                        elif len(orig_words) > 1 or len(corr_words) > 1:
                            # Find words that only appear in one side
                            orig_only = set(orig_words) - set(corr_words)
                            corr_only = set(corr_words) - set(orig_words)
                            
                            # If exactly one word changed on each side, that's our pair
                            if len(orig_only) == 1 and len(corr_only) == 1:
                                orig_word = list(orig_only)[0]
                                corr_word = list(corr_only)[0]
                                
                                # For multi-word, user only says the correction word
                                # e.g., "broken glass" → user says "No, screen" (not "No, broken screen")
                                # So we check if correction word appears in correction transcription
                                if self._is_transcription_error(orig_word, corr_word,
                                                               original_transcription,
                                                               correction_transcription):
                                    self._add_if_new(orig_word, corr_word, original_transcription, command, param)
                            
                            # Edge case: User says full phrase in correction
                            # e.g., "broken glass" → "No, broken screen"
                            elif len(orig_only) >= 1 and len(corr_only) >= 1:
                                # Try to find best match by edit distance
                                best_match = None
                                best_dist = float('inf')
                                
                                for o_word in orig_only:
                                    for c_word in corr_only:
                                        dist = self._levenshtein_distance(o_word, c_word)
                                        if dist < best_dist:
                                            best_dist = dist
                                            best_match = (o_word, c_word)
                                
                                if best_match:
                                    orig_word, corr_word = best_match
                                    if self._is_transcription_error(orig_word, corr_word,
                                                                   original_transcription,
                                                                   correction_transcription):
                                        self._add_if_new(orig_word, corr_word, original_transcription, command, param)
        
        except Exception as e:
            logger.error(f"Failed to learn from correction: {e}")
    
    def _is_transcription_error(self, orig_word: str, corr_word: str, 
                                 original_trans: str, correction_trans: str) -> bool:
        """Check if word pair is likely a transcription error"""
        # Must be different
        if orig_word == corr_word:
            return False
        
        # Must appear in respective transcriptions
        if orig_word not in original_trans.lower():
            return False
        if corr_word not in correction_trans.lower():
            return False
        
        # Not purely numeric (user intentionally changing numbers)
        if orig_word.isdigit() and corr_word.isdigit():
            return False
        
        # Calculate Levenshtein distance (edit distance)
        edit_dist = self._levenshtein_distance(orig_word, corr_word)
        max_len = max(len(orig_word), len(corr_word))
        
        # Allow up to 30% character differences (phonetic errors)
        # e.g., "green"→"screen" = 1 edit in 6 chars = 16%
        # e.g., "walter"→"water" = 2 edits in 6 chars = 33% (still acceptable)
        # e.g., "black"→"blue" = 4 edits in 5 chars = 80% (too different, user changed mind)
        if edit_dist / max_len > 0.4:  # More than 40% different → not transcription error
            return False
        
        return True
    
    def _levenshtein_distance(self, s1: str, s2: str) -> int:
        """Calculate edit distance between two strings"""
        if len(s1) < len(s2):
            return self._levenshtein_distance(s2, s1)
        
        if len(s2) == 0:
            return len(s1)
        
        previous_row = range(len(s2) + 1)
        for i, c1 in enumerate(s1):
            current_row = [i + 1]
            for j, c2 in enumerate(s2):
                insertions = previous_row[j + 1] + 1
                deletions = current_row[j] + 1
                substitutions = previous_row[j] + (c1 != c2)
                current_row.append(min(insertions, deletions, substitutions))
            previous_row = current_row
        
        return previous_row[-1]
    
    def _add_if_new(self, orig_word: str, corr_word: str, example: str, command: str, param: str):
        """Add correction if not already in knowledge base and LLM validates it"""
        already_exists = any(
            error.get('incorrect', '').lower() == orig_word.lower()
            for error in self.transcription_corrections.get('common_errors', [])
        )
        
        if not already_exists:
            # Ask LLM to validate if this is a real transcription error
            if self._llm_validate_correction(orig_word, corr_word, command, param, example):
                self._add_transcription_correction(
                    orig_word,
                    corr_word,
                    f"{example} → corrected to '{corr_word}'",
                    command,
                    param
                )
                logger.info(f"📚 Learned: '{orig_word}' → '{corr_word}' (context: {command}.{param}, edit distance: {self._levenshtein_distance(orig_word, corr_word)})")
            else:
                logger.info(f"🚫 LLM rejected: '{orig_word}' → '{corr_word}' (likely user changed mind or invalid pairing)")
    
    def _llm_validate_correction(self, orig_word: str, corr_word: str, command: str, param: str, example: str) -> bool:
        """Ask LLM to validate if this is a real transcription error"""
        try:
            validation_prompt = f"""You are validating potential voice transcription errors for a phone repair shop system.

Proposed correction:
- Original word: "{orig_word}"
- Corrected to: "{corr_word}"
- Context: {command} command, {param} parameter
- Example: {example}

Determine if this is a REAL transcription error (voice-to-text mistake) or if the user intentionally changed their mind.

Real transcription errors (VALID):
- Phonetically similar words: "scream" → "screen", "walter" → "water", "samson" → "samsung"
- Common voice mistakes: "chargin" → "charging", "bakery" → "battery"

NOT transcription errors (INVALID):
- User changing their mind: "black" → "blue" (both valid colors)
- Completely different meanings: "blue" → "battery" (unrelated words)
- Valid alternatives: "iPhone 14" → "iPhone 13" (intentional change)

Answer ONLY with "VALID" or "INVALID" followed by a brief reason.

Examples:
- "green" → "screen" in repair problem → VALID (phonetically similar, screen makes sense for repair)
- "black" → "blue" in device color → INVALID (both valid colors, user changed mind)
- "blue" → "battery" in repair problem → INVALID (completely unrelated, user clarifying)

Your answer:"""

            response = self.client.chat.completions.create(
                model=self.config['openai']['model'],
                messages=[
                    {"role": "system", "content": "You validate transcription errors. Be strict - only approve obvious phonetic mistakes."},
                    {"role": "user", "content": validation_prompt}
                ],
                temperature=0.3,
                max_tokens=100
            )
            
            result = response.choices[0].message.content.strip().upper()
            
            # Log LLM decision
            logger.info(f"🤖 LLM validation for '{orig_word}' → '{corr_word}': {result}")
            
            return result.startswith("VALID")
        
        except Exception as e:
            logger.error(f"LLM validation failed: {e}, defaulting to REJECT")
            return False  # Default to rejecting if LLM fails
    
    def _add_transcription_correction(self, incorrect: str, correct: str, example: str, command: str = None, param: str = None):
        """Add a new transcription correction to the YAML file with context"""
        try:
            # Read current file
            with open(self.tc_path, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # Build context string
            context_str = "Auto-learned from user correction"
            if command and param:
                context_str += f" (command: {command}, parameter: {param})"
            
            # Create new entry (replace arrow for YAML compatibility)
            example_safe = example.replace('→', 'to')
            new_entry = f"""
  # {correct.title()}/{incorrect.title()} confusion
  - incorrect: "{incorrect}"
    correct: "{correct}"
    context: "{context_str}"
    applies_to:
      command: "{command if command else 'any'}"
      parameter: "{param if param else 'any'}"
    examples:
      - "{example_safe}"
"""
            
            # Find insertion point (after common_errors section, before next section)
            if 'common_errors:' in content:
                # Insert before the next major section (number_patterns, device_models, etc.)
                # or at the end of common_errors
                lines = content.split('\n')
                insert_idx = None
                in_common_errors = False
                
                for i, line in enumerate(lines):
                    if 'common_errors:' in line:
                        in_common_errors = True
                    elif in_common_errors and line and not line.startswith(' ') and not line.startswith('\t'):
                        # Found next section
                        insert_idx = i
                        break
                
                if insert_idx:
                    lines.insert(insert_idx, new_entry)
                else:
                    # Add to end of file
                    lines.append(new_entry)
                
                # Write back
                with open(self.tc_path, 'w', encoding='utf-8') as f:
                    f.write('\n'.join(lines))
                
                # Reload knowledge base
                self.transcription_corrections = self._load_transcription_corrections()
                logger.info(f"✅ Added transcription correction to knowledge base")
        
        except Exception as e:
            logger.error(f"Failed to add transcription correction: {e}")
    
    def _store_success(self, command: str, input_text: str, result: Dict):
        """Store successful extraction in command-specific cache"""
        if command not in self.success_caches:
            self.success_caches[command] = []
        
        # Add to command-specific cache
        self.success_caches[command].append({
            'input': input_text,
            'output': result
        })
        
        # Keep only last N examples per command
        if len(self.success_caches[command]) > self.max_cache_size:
            self.success_caches[command].pop(0)
        
        logger.info(f"📚 Stored {command} success (cache: {len(self.success_caches[command])}/{self.max_cache_size})")
    
    def _get_command_schema(self, command: str) -> Dict:
        """Get validation schema for specific command"""
        schemas = {
            'book': {
                'required_fields': ['brand', 'model', 'problem'],
                'optional_fields': ['deviceCondition', 'accessories', 'totalPrice', 'deposit', 'devicePassword', 'notes'],
                'allowed_values': {
                    'deviceCondition': ["Dead device", "Cracked Screen", "Cracked Back"],
                    'accessories': ["With Bag", "With Charger", "With Case"]
                },
                'string_with_trailing': ['deviceCondition', 'accessories'],  # Must end with ", "
                'numeric_fields': ['totalPrice', 'deposit']
            },
            'sell': {
                'required_fields': ['brand', 'model', 'storage', 'color', 'price'],
                'optional_fields': ['serialNumber', 'imei', 'condition', 'notes'],
                'numeric_fields': ['price']
            },
            'status': {
                'required_fields': ['ticket_id'],
                'optional_fields': [],
                'numeric_fields': ['ticket_id']
            }
        }
        return schemas.get(command, {})
    
    def _validate_extraction(self, result: Dict, command: str = 'book') -> tuple[bool, str]:
        """Validate extracted parameters using command-specific schema"""
        errors = []
        schema = self._get_command_schema(command)
        
        if not schema:
            logger.warning(f"No validation schema for command '{command}', skipping validation")
            return True, ""
        
        # Validate required fields
        for field in schema.get('required_fields', []):
            value = result.get(field)
            if not value or value == 'null':
                errors.append(f"{field} is missing or null - required field")
            elif isinstance(value, str) and len(value) < 2:
                errors.append(f"{field} '{value}' is too short")
        
        # Validate fields with allowed values (exact match required)
        allowed_values = schema.get('allowed_values', {})
        for field, allowed_list in allowed_values.items():
            value = result.get(field)
            if value and value != 'null':
                # Check if it needs trailing ", "
                if field in schema.get('string_with_trailing', []):
                    if not value.endswith(', '):
                        errors.append(f"{field} must end with ', ' (comma and space). Got: '{value}'")
                    
                    # Extract individual values
                    items = [item.strip() for item in value.rstrip(', ').split(',') if item.strip()]
                    invalid_items = [item for item in items if item not in allowed_list]
                    if invalid_items:
                        errors.append(f"{field} has invalid values: {invalid_items}. ONLY allowed: {allowed_list}")
        
        # Validate numeric fields
        for field in schema.get('numeric_fields', []):
            value = result.get(field)
            if value and value != 'null':
                try:
                    num_val = float(value)
                    if num_val < 0:
                        errors.append(f"{field} {value} cannot be negative")
                    elif num_val > 100000:  # Reasonable upper limit
                        errors.append(f"{field} {value} seems unreasonably high")
                except (ValueError, TypeError):
                    errors.append(f"{field} '{value}' must be a number, got: {type(value).__name__}")
        
        if errors:
            return False, " | ".join(errors)
        return True, ""
    
    def extract_book_params(self, text: str, device_id: str = None, conversation_id: str = None) -> Dict[str, str]:
        """
        Extract repair ticket parameters from natural language with retry logic.
        
        Args:
            text: Natural language like "iPhone 7 broken screen for battery, price 75"
            
        Returns:
            Dict with brand, model, problem, price, condition, password, notes, etc.
        """
        # Build examples (show recent successes ONLY on retry)
        full_examples = self.knowledge_base.get('full_examples', [])
        all_examples = []
        
        # Always add static examples
        for ex in full_examples[:4]:
            all_examples.append(
                f"Input: \"{ex['input']}\"\nOutput: {json.dumps({k: v for k, v in ex.items() if k != 'input'}, indent=2)}"
            )
        
        full_examples_text = "\n\n".join(all_examples) if all_examples else "No examples available"
        
        # Build brand/model examples
        brand_examples_text = "\n".join([
            f"- '{ex['input']}' → brand: {ex['brand']}, model: {ex['model']}"
            for ex in self.knowledge_base.get('examples', [])[:10]
        ])
        
        rules_text = "\n".join([
            f"- {rule}"
            for rule in self.knowledge_base.get('parsing_rules', [])
        ])
        
        # Get exact allowed values
        allowed_conditions = self.knowledge_base.get('allowed_conditions', [])
        allowed_accessories = self.knowledge_base.get('allowed_accessories', [])
        
        allowed_conditions_text = "\n".join([f'  - "{c}"' for c in allowed_conditions])
        allowed_accessories_text = "\n".join([f'  - "{a}"' for a in allowed_accessories])
        
        condition_mapping_text = "\n".join([
            f"- {rule}"
            for rule in self.knowledge_base.get('condition_mapping', [])
        ])
        
        accessory_mapping_text = "\n".join([
            f"- {rule}"
            for rule in self.knowledge_base.get('accessory_mapping', [])
        ])
        
        # Max 3 attempts with error feedback
        max_attempts = 3
        previous_errors = []
        
        for attempt in range(max_attempts):
            error_feedback = ""
            success_examples_text = ""
            
            # On retry (attempt > 0), show error feedback AND recent successful extractions
            if previous_errors:
                error_feedback = f"""
⚠️ PREVIOUS ATTEMPT FAILED WITH ERRORS:
{chr(10).join(f'{i+1}. {err}' for i, err in enumerate(previous_errors))}

FIX THESE ERRORS and try again.
"""
                # Add recent successful 'book' extractions on retry
                recent_book_successes = self.success_caches.get('book', [])[-10:]
                if recent_book_successes:
                    success_examples_text = "\n\n=== RECENT SUCCESSFUL 'BOOK' EXTRACTIONS ===\n"
                    success_examples_text += "\n\n".join([
                        f"Input: \"{ex['input']}\"\nOutput: {json.dumps(ex['output'], indent=2)}"
                        for ex in recent_book_successes
                    ])

            prompt = f"""Extract device repair parameters from this text: "{text}"
{error_feedback}
COMPLETE EXTRACTION EXAMPLES:
{full_examples_text}
{success_examples_text}

BRAND/MODEL SHORTCUTS:
{brand_examples_text}

PARSING RULES:
{rules_text}

⚠️ CRITICAL RULES:
- NEVER use articles (a, an, the) as brand names!
- If brand is unclear or missing → set brand to "Generic" OR return ask_clarification
- ALWAYS extract brand, model, problem (required - cannot be null)
- Extract price/deposit from problem description and put in totalPrice/deposit fields
- Use EXACT capitalization for deviceCondition and accessories from allowed lists
- Be CONCISE - extract data only, no explanations or lectures

ALLOWED deviceCondition values (use EXACTLY as shown, including capitalization):
{allowed_conditions_text}

Condition mapping (what user says → what you MUST output):
{condition_mapping_text}

ALLOWED accessories values (use EXACTLY as shown):
{allowed_accessories_text}

Accessory mapping:
{accessory_mapping_text}

Extract and return ONLY a JSON object with these fields:

REQUIRED (MUST always extract):
- brand: Device manufacturer (e.g., "Apple", "Samsung", "HP", "Sony", "Nokia")
  * Infer from model if not explicit: "iPhone" → "Apple", "Galaxy" → "Samsung"
  * If brand CANNOT be determined → use "Generic" OR ask for clarification
  * NEVER use articles: "a smartwatch" → brand "Generic", model "smartwatch" (NOT brand "a"!)
  * NEVER use articles: "a Sony camera" → brand "Sony", model "camera" (NOT brand "a"!)
  * NO trailing periods
  
- model: Specific model (e.g., "iPhone 7", "Galaxy S21 Ultra", "smartwatch", "circuit board")
  * NO trailing periods
  * Remove articles: "a smartwatch" → "smartwatch"
  * If user explicitly says they don't know or model is unclear → use "Unknown"
  * NEVER ask for model more than ONCE
  
- problem: Primary repair issue - be specific (e.g., "battery replacement", "charging port repair")
  * NO trailing periods
  * Remove price mentions from problem and put in totalPrice field
  * Example: "relay replacement, price 75" → problem: "relay replacement", totalPrice: 75

UNCLEAR INPUT HANDLING:
- If brand is an article (a, an, the) or missing → ALWAYS ask for clarification or use "Generic"
- If the user says "quickly", "urgent", "don't know", "unknown" → Proceed with what you have, use "Unknown" for missing model
- If user explicitly states they don't know something → NEVER ask for it again, use "Unknown"
- If input is genuinely confusing and NO urgent keywords → Return special "ask_clarification" field (ONLY ONCE)
- Example unclear: "book for battery" → {{"ask_clarification": "Which device brand and model?"}}
- Example unclear: "book a smartwatch for screen" → {{"ask_clarification": "Which brand of smartwatch?"}}
- Example urgent: "book a smartwatch quickly" → {{"brand": "Generic", "model": "smartwatch", ...}}
- Example don't know: "I don't know the model" → {{"brand": "[extracted]", "model": "Unknown", ...}}

OPTIONAL (extract if mentioned):
- deviceCondition: String with EXACT values from allowed list, comma-separated with trailing ", "
  * Format: "Cracked Screen, " or "Dead device, Cracked Back, "
  * MUST end with ", " (comma space)
  * ONLY use values from allowed list above
  
- accessories: String with EXACT values from allowed list, comma-separated with trailing ", "
  * Format: "With Case, " or "With Bag, With Charger, "
  * MUST end with ", " (comma space)
  * ONLY use values from allowed list above
  
- totalPrice: Price as number - extract from problem description if present
  * Examples: "price 75" → 75, "£75" → 75, "150 pounds" → 150
  * Remove price mention from problem field: "relay replacement, price 75" → problem: "relay replacement", totalPrice: 75
  
- deposit: Deposit amount as number if mentioned
- devicePassword: PIN/passcode if mentioned (e.g., "1234", "password 5678")
- notes: Any additional context

If a field cannot be determined, set it to null.

Return ONLY the JSON, no explanation."""

            try:
                if attempt == 0:
                    logger.info(f"🤖 LLM INPUT (attempt {attempt + 1}/{max_attempts}): '{text}'")
                else:
                    logger.warning(f"🤖 LLM RETRY {attempt + 1}/{max_attempts}: Previous errors: {previous_errors[-1]}")
                
                messages = [
                    {"role": "system", "content": "Extract structured repair data. Be precise and concise. Always include brand, model, problem. Use exact allowed values. No trailing periods on brand, model, or problem. No explanations."},
                    {"role": "user", "content": prompt}
                ]
                
                # Log LLM prompt to device logger
                if self.device_logger and device_id and conversation_id:
                    self.device_logger.log_llm_prompt(
                        device_id, conversation_id, 
                        "EXTRACT_BOOK_PARAMS", 
                        messages, 
                        self.config['openai']['model']
                    )
                
                response = self.client.chat.completions.create(
                    model=self.config['openai']['model'],
                    messages=messages,
                    response_format={"type": "json_object"},
                    temperature=0.2 if attempt == 0 else 0.1,  # Lower temp on retries
                    max_tokens=250
                )
                
                result = json.loads(response.choices[0].message.content)
                
                # Log LLM response to device logger
                if self.device_logger and device_id and conversation_id:
                    self.device_logger.log_llm_response(
                        device_id, conversation_id,
                        "EXTRACT_BOOK_PARAMS",
                        result
                    )
                
                # Check if LLM is asking for clarification
                if result.get('ask_clarification'):
                    logger.info(f"💬 LLM requesting clarification: {result['ask_clarification']}")
                    return result  # Return with ask_clarification field
                
                # Validate the result (command-specific validation)
                is_valid, error_msg = self._validate_extraction(result, command='book')
                
                if is_valid:
                    logger.info(f"✅ LLM OUTPUT (attempt {attempt + 1}): {json.dumps(result, indent=2)}")
                    # Store successful extraction in 'book' cache for future learning
                    self._store_success('book', text, result)
                    return result
                else:
                    logger.warning(f"❌ LLM VALIDATION FAILED (attempt {attempt + 1}): {error_msg}")
                    logger.warning(f"   Invalid output: {json.dumps(result, indent=2)}")
                    previous_errors.append(error_msg)
                    
                    # If this was the last attempt, return anyway with warning
                    if attempt == max_attempts - 1:
                        logger.error(f"⚠️  Max attempts reached, returning invalid result")
                        return result
                    
                    # Otherwise continue to next attempt
                    continue
                
            except json.JSONDecodeError as e:
                logger.error(f"❌ LLM returned invalid JSON (attempt {attempt + 1}): {e}")
                previous_errors.append(f"JSON parsing error: {e}")
                if attempt == max_attempts - 1:
                    # Last attempt failed completely
                    return {
                        'brand': None, 
                        'model': None, 
                        'problem': None,
                        'deviceCondition': None,
                        'accessories': None,
                        'totalPrice': None,
                        'deposit': None,
                        'devicePassword': None,
                        'notes': None
                    }
            except Exception as e:
                logger.error(f"❌ LLM extraction error (attempt {attempt + 1}): {e}")
                previous_errors.append(f"Extraction error: {e}")
                if attempt == max_attempts - 1:
                    return {
                        'brand': None, 
                        'model': None, 
                        'problem': None,
                        'deviceCondition': None,
                        'accessories': None,
                        'totalPrice': None,
                        'deposit': None,
                        'devicePassword': None,
                        'notes': None
                    }
        
        # Shouldn't reach here but just in case
        return {
            'brand': None, 
            'model': None, 
            'problem': None,
            'deviceCondition': None,
            'accessories': None,
            'totalPrice': None,
            'deposit': None,
            'devicePassword': None,
            'notes': None
        }
    
    @retry(
        stop=stop_after_attempt(2),
        wait=wait_exponential(multiplier=1, min=1, max=5),
        retry=retry_if_exception_type((httpx.TimeoutException, httpx.NetworkError))
    )
    def extract_sell_params(self, text: str, device_id: str = None, conversation_id: str = None) -> Dict[str, str]:
        """
        Extract brand, model, storage, color, price from natural language with retry logic.
        
        NOTE: This uses the legacy simple extraction. Should be updated to use the 
        same validation/retry pattern as extract_book_params() when sell command is fully implemented.
        
        Args:
            text: Natural language like "S24 256GB black 600"
            
        Returns:
            Dict with brand, model, storage, color, price
        """
        examples_text = "\n".join([
            f"- '{ex['input']}' → brand: {ex['brand']}, model: {ex['model']}"
            for ex in self.knowledge_base.get('examples', [])[:10]
        ])
        
        prompt = f"""Extract device sale parameters from this text: "{text}"

LEARNED EXAMPLES:
{examples_text}

Extract and return ONLY a JSON object with these fields:
- brand: The device manufacturer
- model: The specific model
- storage: Storage capacity (e.g., "256GB", "128GB")
- color: Device color
- price: Price in GBP (number only, no currency symbol)

If a field cannot be determined, set it to null.

Return ONLY the JSON, no explanation."""

        try:
            messages = [
                {"role": "system", "content": "You are a device sales assistant that extracts structured data from natural language."},
                {"role": "user", "content": prompt}
            ]
            
            # Log LLM prompt to device logger
            if self.device_logger and device_id and conversation_id:
                self.device_logger.log_llm_prompt(
                    device_id, conversation_id,
                    "EXTRACT_SELL_PARAMS",
                    messages,
                    self.config['openai']['model']
                )
            
            response = self.client.chat.completions.create(
                model=self.config['openai']['model'],
                messages=messages,
                response_format={"type": "json_object"},
                temperature=0.3,
                max_tokens=150
            )
            
            result = json.loads(response.choices[0].message.content)
            
            # Log LLM response to device logger
            if self.device_logger and device_id and conversation_id:
                self.device_logger.log_llm_response(
                    device_id, conversation_id,
                    "EXTRACT_SELL_PARAMS",
                    result
                )
            logger.info(f"LLM extracted: {result} from '{text}'")
            
            return result
            
        except Exception as e:
            logger.error(f"LLM extraction failed: {e}")
            return {'brand': None, 'model': None, 'storage': None, 'color': None, 'price': None}
    
    @retry(
        stop=stop_after_attempt(2),
        wait=wait_exponential(multiplier=1, min=1, max=5),
        retry=retry_if_exception_type((httpx.TimeoutException, httpx.NetworkError))
    )
    def analyze_unknown_command(self, text: str, available_commands: list, context: dict = None, device_id: str = None, conversation_id: str = None) -> dict:
        """
        Analyze transcript that didn't match any command pattern using LLM.
        
        Args:
            text: User's transcribed speech
            available_commands: List of available command names
            context: Optional conversation context with history (for multi-turn)
            
        Returns:
            Dict with:
            - type: 'command' (LLM matched to command) or 'ask' (needs clarification)
            - If 'command': includes command, params, requires_confirmation
            - If 'ask': includes question
        """
        # Build command descriptions from schema (dynamically)
        commands_desc = {}
        
        # Load command schema to get required + optional params
        from pathlib import Path
        import yaml
        
        schema_path = Path(__file__).parent / '..' / 'config' / 'command_schema.yaml'
        try:
            with open(schema_path, 'r', encoding='utf-8') as f:
                command_schema = yaml.safe_load(f)
            
            for cmd_name, cmd_info in command_schema.get('commands', {}).items():
                required = cmd_info.get('required_params', [])
                optional = cmd_info.get('optional_params', [])
                
                desc = cmd_info.get('description', f'{cmd_name} command')
                param_desc = f"Required: {', '.join(required)}"
                if optional:
                    param_desc += f" | Optional: {', '.join(optional)}"
                
                commands_desc[cmd_name] = f"{desc} ({param_desc})"
        except Exception as e:
            logger.warning(f"Could not load command schema, using fallback: {e}")
            # Fallback to basic descriptions
            commands_desc = {
                'book': 'Book a device repair (Required: brand, model, problem | Optional: totalPrice, deposit)',
                'sell': 'Create sales invoice (Required: brand, model, storage, color, price)',
                'status': 'Check repair ticket status (Required: ticket_id)',
                'volume': 'Control device volume (Required: level up/down/0-100)',
                'brightness': 'Control screen brightness (Required: level up/down/0-100)',
            }
        
        available_desc = "\n".join([
            f"- {cmd}: {commands_desc.get(cmd, 'No description')}"
            for cmd in available_commands
        ])
        
        # Build transcription corrections hint
        tc_hint = ""
        if self.transcription_corrections.get('common_errors'):
            tc_hint = "\n\n⚠️ KNOWN TRANSCRIPTION ERRORS (Voice-to-text often makes these mistakes):\n"
            tc_hint += "If you see these words, they are HIGHLY LIKELY transcription errors:\n"
            for error in self.transcription_corrections['common_errors'][:5]:  # Top 5
                tc_hint += f"  • '{error['incorrect']}' → correct: '{error['correct']}' | Context: {error.get('context', 'Common error')}\n"
            tc_hint += "✅ ALWAYS check user input against these errors and use the correct version!\n"
        
        system_prompt = f"""You are a voice command analyzer for a phone repair shop. Extract structured commands from natural language.

AVAILABLE COMMANDS:
{available_desc}
{tc_hint}
TASK:
1. Analyze the conversation history and current user message
2. Be aware of common voice transcription errors (e.g., "scream" → "screen")
3. Extract ALL mentioned parameters (both required AND optional)
   - If user mentions price/cost → Extract as totalPrice
   - If user mentions deposit → Extract as deposit
   - Extract all relevant details mentioned
4. If the user is providing missing information for an ongoing command → Extract and update parameters
5. If you have all required parameters → Return complete command
6. If still missing required parameters → Ask a clarifying question
7. If completely unrelated → Ask what they want to do

Return ONLY a JSON object with one of these formats:

FORMAT 1 - Command complete with all required parameters (include optional if mentioned):
{{
  "type": "command",
  "command": "book",
  "params": {{"brand": "Samsung", "model": "S24", "problem": "screen", "totalPrice": 65}},
  "confidence": "high"
}}

FORMAT 2 - Command identified but needs more info:
{{
  "type": "ask",
  "possible_command": "book",
  "question": "What device do you need to repair?",
  "params_so_far": {{"problem": "screen"}},
  "confidence": "medium"
}}

FORMAT 3 - No clear command:
{{
  "type": "ask",
  "question": "I can help you book repairs, create invoices, or check ticket status. What would you like to do?",
  "confidence": "low"
}}

Return ONLY the JSON, no explanation."""

        try:
            # Build messages with conversation history
            messages = [
                {"role": "system", "content": system_prompt}
            ]
            
            # Add conversation history if available
            if context and 'history' in context:
                for role, content in context['history']:
                    messages.append({"role": role, "content": content})
            
            # Add current user message
            messages.append({"role": "user", "content": text})
            
            # Log LLM prompt to device logger
            if self.device_logger and device_id and conversation_id:
                self.device_logger.log_llm_prompt(
                    device_id, conversation_id,
                    "ANALYZE_UNKNOWN_COMMAND",
                    messages,
                    self.config['openai']['model']
                )
            
            response = self.client.chat.completions.create(
                model=self.config['openai']['model'],
                messages=messages,
                response_format={"type": "json_object"},
                temperature=0.3,
                max_tokens=300
            )
            
            result = json.loads(response.choices[0].message.content)
            
            # Log LLM response to device logger
            if self.device_logger and device_id and conversation_id:
                self.device_logger.log_llm_response(
                    device_id, conversation_id,
                    "ANALYZE_UNKNOWN_COMMAND",
                    result
                )
            
            logger.info(f"LLM analyzed unknown command: {result}")
            
            return result
            
        except Exception as e:
            logger.error(f"LLM analyze_unknown_command failed: {e}")
            return {
                'type': 'ask',
                'question': 'I didn\'t quite catch that. Could you rephrase?',
                'confidence': 'low'
            }
    
    def save_correction(self, original_input: str, corrected_brand: str, corrected_model: str):
        """
        Save a user correction to the knowledge base for future learning.
        Keeps only the last 100 corrections to prevent infinite growth.
        
        Args:
            original_input: What the user said
            corrected_brand: The correct brand
            corrected_model: The correct model
        """
        kb_path = os.path.join(
            os.path.dirname(__file__), 
            '..', 
            'config', 
            'brand_model_knowledge.yaml'
        )
        
        try:
            # Add new example
            new_example = {
                'input': original_input,
                'brand': corrected_brand,
                'model': corrected_model
            }
            
            self.knowledge_base['examples'].append(new_example)
            
            # Keep only last 100 corrections (prevent infinite growth)
            MAX_CORRECTIONS = 100
            if len(self.knowledge_base['examples']) > MAX_CORRECTIONS:
                # Keep the predefined examples + last 100 corrections
                # Assuming first 20 are predefined, rest are learned
                predefined_count = 20
                self.knowledge_base['examples'] = (
                    self.knowledge_base['examples'][:predefined_count] +
                    self.knowledge_base['examples'][-MAX_CORRECTIONS:]
                )
            
            # Save back to file
            with open(kb_path, 'w') as f:
                yaml.dump(self.knowledge_base, f, default_flow_style=False)
            
            logger.info(f"Saved correction: '{original_input}' → {corrected_brand} {corrected_model}")
            logger.info(f"Knowledge base size: {len(self.knowledge_base['examples'])} examples")
            
        except Exception as e:
            logger.error(f"Failed to save correction: {e}")

"""
Audio transcription using OpenAI Whisper API.
"""
import os
import logging
import httpx
from typing import Optional
from openai import OpenAI
from tenacity import retry, stop_after_attempt, wait_exponential, retry_if_exception_type

logger = logging.getLogger(__name__)

class TranscriptionService:
    """Handles audio transcription with timeout and retry logic"""
    
    def __init__(self, config: dict, openai_api_key: str):
        self.config = config
        # Create httpx client with 30s timeout for OpenAI API calls
        http_client = httpx.Client(timeout=30.0)
        self.client = OpenAI(api_key=openai_api_key, http_client=http_client)
        
    def transcribe(self, audio_file_path: str) -> Optional[str]:
        """
        Transcribe audio file using OpenAI with retry logic.
        
        Args:
            audio_file_path: Path to audio file
            
        Returns:
            Transcribed text or None if failed
        """
        try:
            text = self._transcribe_openai_with_retry(audio_file_path)
            logger.info("Transcription successful (OpenAI)")
            return text
        except Exception as e:
            logger.error(f"OpenAI transcription failed after retries: {e}")
            return None
    
    @retry(
        stop=stop_after_attempt(2),
        wait=wait_exponential(multiplier=1, min=1, max=5),
        retry=retry_if_exception_type((httpx.TimeoutException, httpx.NetworkError)),
        reraise=True
    )
    def _transcribe_openai_with_retry(self, audio_file_path: str) -> Optional[str]:
        """Transcribe using OpenAI Whisper API with retry on transient failures"""
        with open(audio_file_path, 'rb') as audio_file:
            transcript = self.client.audio.transcriptions.create(
                model=self.config['openai']['whisper_model'],
                file=audio_file,
                response_format="text",
                language="en"  # Force English transcription
            )
        return transcript.strip()

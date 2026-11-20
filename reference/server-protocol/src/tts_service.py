"""
Text-to-Speech service using OpenAI TTS.
"""
import os
import logging
import hashlib
import time
import subprocess
import httpx
import json
from typing import Optional
from openai import OpenAI
from tenacity import retry, stop_after_attempt, wait_exponential, retry_if_exception_type

logger = logging.getLogger(__name__)

class TTSService:
    """Handles text-to-speech generation with timeout and retry logic"""
    
    def __init__(self, config: dict, openai_api_key: str, audio_temp_dir: str):
        self.config = config
        # Create httpx client with 30s timeout for OpenAI API calls
        http_client = httpx.Client(timeout=30.0)
        self.client = OpenAI(api_key=openai_api_key, http_client=http_client)
        self.audio_temp_dir = audio_temp_dir
        self.metadata_file = os.path.join(audio_temp_dir, 'cache_metadata.json')
        self.max_cache_size_bytes = 1 * 1024 * 1024 * 1024  # 1GB
        self._load_metadata()
    
    def _load_metadata(self):
        """Load cache metadata from JSON file"""
        try:
            if os.path.exists(self.metadata_file):
                with open(self.metadata_file, 'r') as f:
                    self.cache_metadata = json.load(f)
                logger.info(f"Loaded cache metadata: {len(self.cache_metadata)} files tracked")
            else:
                self.cache_metadata = {}
                logger.info("No cache metadata found, starting fresh")
        except Exception as e:
            logger.error(f"Error loading cache metadata: {e}")
            self.cache_metadata = {}
    
    def _save_metadata(self):
        """Save cache metadata to JSON file"""
        try:
            with open(self.metadata_file, 'w') as f:
                json.dump(self.cache_metadata, f, indent=2)
        except Exception as e:
            logger.error(f"Error saving cache metadata: {e}")
    
    def _track_usage(self, filename: str, file_size: int = None):
        """Track file usage and update metadata"""
        if filename not in self.cache_metadata:
            # New file - get size if not provided
            if file_size is None:
                file_path = os.path.join(self.audio_temp_dir, filename)
                if os.path.exists(file_path):
                    file_size = os.path.getsize(file_path)
                else:
                    file_size = 0
            
            self.cache_metadata[filename] = {
                'usage_count': 1,
                'size_bytes': file_size,
                'created_at': time.time(),
                'last_accessed': time.time()
            }
        else:
            # Existing file - increment usage
            self.cache_metadata[filename]['usage_count'] += 1
            self.cache_metadata[filename]['last_accessed'] = time.time()
        
        self._save_metadata()
        
    def generate(self, text: str, voice: str = "alloy") -> Optional[str]:
        """
        Generate speech from text using OpenAI with retry logic.
        
        Args:
            text: Text to convert to speech
            voice: Voice to use (alloy, echo, fable, onyx, nova, shimmer)
            
        Returns:
            Path to generated audio file or None if failed
        """
        # Create filename from text hash (for caching)
        # Using WAV format for ESP32 compatibility
        text_hash = hashlib.md5(text.encode()).hexdigest()
        filename = f"tts_{text_hash}.wav"
        output_path = os.path.join(self.audio_temp_dir, filename)
        
        # Check if already cached
        if os.path.exists(output_path):
            logger.info(f"✅ Using cached TTS: {filename}")
            self._track_usage(filename)
            return output_path
        
        # Generate using OpenAI with retry
        try:
            result = self._generate_openai_with_retry(text, voice, output_path)
            if result:
                # Track new file
                file_size = os.path.getsize(output_path)
                
                # Check if we can cache this file
                total_size = sum(meta['size_bytes'] for meta in self.cache_metadata.values())
                cache_will_exceed = (total_size + file_size) > self.max_cache_size_bytes
                
                if cache_will_exceed:
                    # Try cleanup first
                    self._cleanup_if_needed()
                    
                    # Re-check after cleanup
                    total_size = sum(meta['size_bytes'] for meta in self.cache_metadata.values())
                    cache_still_exceeds = (total_size + file_size) > self.max_cache_size_bytes
                    
                    if cache_still_exceeds:
                        # Cache full, no single-use files to delete
                        # Keep as temporary file (don't cache)
                        logger.warning(f"⚠️  Cache full ({total_size / (1024*1024):.1f}MB), generating temporary file (won't be cached)")
                        logger.info(f"🔥 Temporary TTS: {filename} ({file_size} bytes) - will be deleted after use")
                        # Don't track usage - file will be cleaned up manually or by next cache check
                        return output_path  # Return path but don't cache
                
                # Cache has space - track it
                self._track_usage(filename, file_size)
                logger.info(f"✨ TTS generated (OpenAI): {filename} ({file_size} bytes) - cached")
                
            return result
        except Exception as e:
            logger.error(f"OpenAI TTS failed after retries: {e}")
            return None
    
    @retry(
        stop=stop_after_attempt(2),
        wait=wait_exponential(multiplier=1, min=1, max=5),
        retry=retry_if_exception_type((httpx.TimeoutException, httpx.NetworkError)),
        reraise=True
    )
    def _generate_openai_with_retry(self, text: str, voice: str, output_path: str) -> Optional[str]:
        """Generate TTS using OpenAI with retry on transient failures"""
        # Generate to temp file first (OpenAI outputs 24kHz)
        temp_path = output_path + ".temp.wav"
        
        response = self.client.audio.speech.create(
            model=self.config['openai']['tts_model'],
            voice=voice,
            input=text,
            response_format="wav"
        )
        
        # Save to temp file
        response.stream_to_file(temp_path)
        
        # Convert to ESP32 format (16kHz, mono, 16-bit PCM)
        converted = self._convert_to_esp32_format(temp_path, output_path)
        
        # Clean up temp file
        if os.path.exists(temp_path):
            os.remove(temp_path)
        
        return output_path if converted else None
    
    def _convert_to_esp32_format(self, input_path: str, output_path: str) -> bool:
        """
        Convert audio to ESP32-compatible format using ffmpeg.
        Target: 16kHz, mono, 16-bit PCM WAV
        
        Returns:
            True if conversion successful, False otherwise
        """
        try:
            # ffmpeg command to convert to ESP32 format
            cmd = [
                'ffmpeg',
                '-i', input_path,
                '-ar', '16000',        # Sample rate: 16kHz
                '-ac', '1',            # Channels: mono
                '-sample_fmt', 's16',  # Sample format: 16-bit signed PCM
                '-y',                  # Overwrite output file
                output_path
            ]
            
            result = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=15  # Increased to 15s for safety
            )
            
            if result.returncode == 0:
                logger.info(f"Audio converted to ESP32 format: {output_path}")
                return True
            else:
                logger.error(f"ffmpeg conversion failed: {result.stderr.decode()}")
                return False
        
        except FileNotFoundError:
            logger.error("ffmpeg not found - install with: apt install ffmpeg")
            # Fallback: copy original file (will sound wrong but won't crash)
            if os.path.exists(input_path):
                os.rename(input_path, output_path)
            return True
        except subprocess.TimeoutExpired:
            logger.error("ffmpeg conversion timeout (15s limit exceeded)")
            # Kill process and cleanup
            if os.path.exists(input_path):
                try:
                    os.remove(input_path)
                except:
                    pass
            return False
        except Exception as e:
            logger.error(f"Audio conversion error: {e}")
            return False
    
    def get_url_for_file(self, file_path: str, base_url: str) -> str:
        """
        Generate public URL for audio file.
        
        Args:
            file_path: Local path to audio file
            base_url: Server base URL (e.g., http://localhost:8080)
            
        Returns:
            Public URL to download the file (forced to HTTPS for ESP32)
        """
        filename = os.path.basename(file_path)
        # Force HTTPS for ESP32 compatibility (it uses WiFiClientSecure)
        base_url = base_url.replace('http://', 'https://')
        return f"{base_url}/audio/{filename}"
    
    def _cleanup_if_needed(self):
        """Check total cache size and cleanup if over limit"""
        total_size = sum(meta['size_bytes'] for meta in self.cache_metadata.values())
        
        if total_size > self.max_cache_size_bytes:
            logger.warning(f"🗑️  Cache size {total_size / (1024*1024):.1f}MB exceeds limit {self.max_cache_size_bytes / (1024*1024):.1f}MB")
            self.cleanup_by_size()
    
    def cleanup_by_size(self):
        """
        Delete single-use TTS files when cache exceeds 1GB.
        Strategy: Delete files with usage_count == 1, oldest first.
        
        Returns:
            Number of files deleted
        """
        try:
            # Calculate total size
            total_size = 0
            files_to_check = []
            
            for filename, meta in self.cache_metadata.items():
                file_path = os.path.join(self.audio_temp_dir, filename)
                # Verify file exists
                if os.path.exists(file_path):
                    actual_size = os.path.getsize(file_path)
                    total_size += actual_size
                    files_to_check.append((filename, meta, actual_size))
                else:
                    # File missing - remove from metadata
                    logger.warning(f"File {filename} in metadata but not on disk, removing")
            
            # Clean up missing files from metadata
            self.cache_metadata = {
                fname: meta for fname, meta, _ in files_to_check
            }
            
            logger.info(f"📊 Cache stats: {len(files_to_check)} files, {total_size / (1024*1024):.1f}MB total")
            
            # If under limit, nothing to do
            if total_size <= self.max_cache_size_bytes:
                return 0
            
            # Find single-use files (usage_count == 1)
            single_use_files = [
                (fname, meta, size) for fname, meta, size in files_to_check
                if meta['usage_count'] == 1
            ]
            
            if not single_use_files:
                logger.warning("⚠️  Cache over limit but no single-use files to delete!")
                return 0
            
            # Sort by creation time (oldest first)
            single_use_files.sort(key=lambda x: x[1]['created_at'])
            
            # Delete files until under limit
            deleted_count = 0
            freed_bytes = 0
            
            for filename, meta, size in single_use_files:
                if total_size - freed_bytes <= self.max_cache_size_bytes:
                    break  # Under limit now
                
                file_path = os.path.join(self.audio_temp_dir, filename)
                try:
                    os.remove(file_path)
                    del self.cache_metadata[filename]
                    freed_bytes += size
                    deleted_count += 1
                    logger.info(f"🗑️  Deleted single-use file: {filename} ({size} bytes)")
                except Exception as e:
                    logger.error(f"Error deleting {filename}: {e}")
            
            # Save updated metadata
            self._save_metadata()
            
            new_total = total_size - freed_bytes
            logger.info(f"✅ Cleanup complete: Deleted {deleted_count} files, freed {freed_bytes / (1024*1024):.1f}MB")
            logger.info(f"📊 New cache size: {new_total / (1024*1024):.1f}MB ({len(self.cache_metadata)} files)")
            
            return deleted_count
        
        except Exception as e:
            logger.error(f"Error in cleanup_by_size: {e}")
            return 0
    
    def get_cache_stats(self) -> dict:
        """
        Get cache statistics for monitoring.
        
        Returns:
            Dict with cache statistics
        """
        try:
            total_size = 0
            total_files = 0
            single_use_count = 0
            multi_use_count = 0
            
            for filename, meta in self.cache_metadata.items():
                file_path = os.path.join(self.audio_temp_dir, filename)
                if os.path.exists(file_path):
                    total_size += meta['size_bytes']
                    total_files += 1
                    if meta['usage_count'] == 1:
                        single_use_count += 1
                    else:
                        multi_use_count += 1
            
            return {
                'total_files': total_files,
                'total_size_mb': round(total_size / (1024 * 1024), 2),
                'max_size_mb': round(self.max_cache_size_bytes / (1024 * 1024), 2),
                'usage_percent': round((total_size / self.max_cache_size_bytes) * 100, 2),
                'single_use_files': single_use_count,
                'multi_use_files': multi_use_count,
                'most_used': self._get_most_used_files(5)
            }
        except Exception as e:
            logger.error(f"Error getting cache stats: {e}")
            return {}
    
    def _get_most_used_files(self, limit: int = 5) -> list:
        """Get list of most frequently used cached files"""
        try:
            # Sort by usage count
            sorted_files = sorted(
                self.cache_metadata.items(),
                key=lambda x: x[1]['usage_count'],
                reverse=True
            )
            
            return [
                {
                    'filename': fname,
                    'usage_count': meta['usage_count'],
                    'size_kb': round(meta['size_bytes'] / 1024, 2)
                }
                for fname, meta in sorted_files[:limit]
            ]
        except Exception as e:
            logger.error(f"Error getting most used files: {e}")
            return []
    
    def cleanup_temp_files(self):
        """
        Clean up temporary (uncached) TTS files.
        These are files that exist in audio_temp but not in metadata
        (generated when cache was full).
        """
        try:
            deleted_count = 0
            
            for filename in os.listdir(self.audio_temp_dir):
                if filename.startswith('tts_') and filename.endswith('.wav'):
                    # Check if file is in metadata (cached)
                    if filename not in self.cache_metadata:
                        file_path = os.path.join(self.audio_temp_dir, filename)
                        # Delete uncached files older than 5 minutes
                        file_age = time.time() - os.path.getmtime(file_path)
                        if file_age > 300:  # 5 minutes
                            os.remove(file_path)
                            deleted_count += 1
                            logger.info(f"🧹 Deleted temporary file: {filename}")
            
            if deleted_count > 0:
                logger.info(f"✅ Cleaned up {deleted_count} temporary TTS files")
            
            return deleted_count
        except Exception as e:
            logger.error(f"Error cleaning temp files: {e}")
            return 0

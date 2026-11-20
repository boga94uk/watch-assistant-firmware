"""
Background task for cleaning up old invoice PDFs.
Deletes invoices older than 7 days to save disk space.
"""
import os
import time
import logging
import threading
from datetime import datetime, timedelta
from pathlib import Path

logger = logging.getLogger(__name__)

class InvoiceCleanup:
    """Background thread for cleaning up old invoices"""
    
    def __init__(self, invoice_dir: str, max_age_days: int = 7, check_interval_hours: int = 24):
        """
        Initialize invoice cleanup service.
        
        Args:
            invoice_dir: Directory containing invoice PDFs
            max_age_days: Maximum age of invoices before deletion (default: 7 days)
            check_interval_hours: How often to check for old files (default: 24 hours)
        """
        self.invoice_dir = invoice_dir
        self.max_age_days = max_age_days
        self.check_interval_seconds = check_interval_hours * 3600
        self.running = False
        self.thread = None
        
        # Ensure invoice directory exists
        os.makedirs(self.invoice_dir, exist_ok=True)
    
    def start(self):
        """Start the cleanup background thread"""
        if self.running:
            logger.warning("Invoice cleanup already running")
            return
        
        self.running = True
        self.thread = threading.Thread(target=self._cleanup_loop, daemon=True)
        self.thread.start()
        logger.info(f"Started invoice cleanup (max age: {self.max_age_days} days)")
    
    def stop(self):
        """Stop the cleanup background thread"""
        self.running = False
        if self.thread:
            self.thread.join(timeout=5)
        logger.info("Stopped invoice cleanup")
    
    def _cleanup_loop(self):
        """Main cleanup loop - runs in background thread"""
        while self.running:
            try:
                self._cleanup_old_invoices()
            except Exception as e:
                logger.error(f"Invoice cleanup error: {e}")
            
            # Sleep for check interval (check periodically if we should stop)
            sleep_remaining = self.check_interval_seconds
            while sleep_remaining > 0 and self.running:
                time.sleep(min(60, sleep_remaining))  # Check every minute
                sleep_remaining -= 60
    
    def _cleanup_old_invoices(self):
        """Delete invoice PDFs older than max_age_days"""
        try:
            cutoff_time = datetime.now() - timedelta(days=self.max_age_days)
            deleted_count = 0
            
            # Find all PDF files in invoice directory
            for pdf_file in Path(self.invoice_dir).glob("**/*.pdf"):
                # Get file modification time
                file_mtime = datetime.fromtimestamp(pdf_file.stat().st_mtime)
                
                if file_mtime < cutoff_time:
                    try:
                        pdf_file.unlink()
                        deleted_count += 1
                        logger.info(f"Deleted old invoice: {pdf_file.name}")
                    except Exception as e:
                        logger.error(f"Failed to delete {pdf_file.name}: {e}")
            
            if deleted_count > 0:
                logger.info(f"Cleanup complete: Deleted {deleted_count} old invoice(s)")
            else:
                logger.debug("No old invoices to delete")
                
        except Exception as e:
            logger.error(f"Error during invoice cleanup: {e}")

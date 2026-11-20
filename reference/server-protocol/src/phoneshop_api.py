"""
Phoneshop API client for multi-device support.
Handles authentication, token management, and API requests.
"""
import os
import requests
import logging
from datetime import datetime, timedelta, timezone
from typing import Dict, Optional, Tuple

logger = logging.getLogger(__name__)

class PhoneshopAPIClient:
    """API client for Phoneshop system with per-device authentication"""
    
    BASE_URL = "https://YOUR_PHONESHOP_API_HOST"
    
    def __init__(self):
        self.device_tokens = {}  # {device_id: {'token': str, 'shop_id': int, 'expires_at': datetime}}
    
    def _ensure_token(self, device_id: str, username: str, password: str) -> bool:
        """
        Ensure we have a valid token for this device.
        Auto-refresh if expired.
        
        Args:
            device_id: Device identifier
            username: Phoneshop username
            password: Phoneshop password
            
        Returns:
            True if token is valid, False otherwise
        """
        # Check if we have a token and it's not expired
        if device_id in self.device_tokens:
            token_info = self.device_tokens[device_id]
            expires_at = token_info.get('expires_at')
            if expires_at and datetime.now(timezone.utc) < expires_at:
                return True
        
        # Need to login
        return self._login(device_id, username, password)
    
    def _login(self, device_id: str, username: str, password: str) -> bool:
        """
        Login to Phoneshop API and store token.
        
        Args:
            device_id: Device identifier
            username: Phoneshop username
            password: Phoneshop password
            
        Returns:
            True if login successful, False otherwise
        """
        try:
            # Use /YOUR_SHOP_API_PATH endpoint with query parameters (not /YOUR_SHOP_API_PATH with JSON)
            # Token comes back in Authorization header, not response body
            params = {"username": username, "password": password}
            response = requests.post(
                f"{self.BASE_URL}/YOUR_SHOP_API_PATH",
                params=params,
                timeout=10
            )
            
            if response.status_code == 200:
                # Token is in the Authorization header
                token = response.headers.get("Authorization")
                
                # User data and shop ID are in response body
                data = response.json()
                shop_id = data.get("shopId")
                
                if not token:
                    logger.error(f"Login response missing Authorization header for device {device_id}")
                    return False
                
                # Store token with 1 hour expiry (conservative)
                self.device_tokens[device_id] = {
                    'token': token,
                    'shop_id': shop_id,
                    'user_data': data,
                    'expires_at': datetime.now(timezone.utc) + timedelta(hours=1)
                }
                
                logger.info(f"Logged in device {device_id} to Phoneshop (shop_id: {shop_id})")
                return True
            else:
                logger.error(f"Login failed for device {device_id}: {response.status_code}")
                try:
                    error_body = response.text
                    logger.error(f"Response body: {error_body}")
                except:
                    pass
                return False
                
        except Exception as e:
            logger.error(f"Login error for device {device_id}: {e}")
            return False
    
    def _make_request(self, device_id: str, endpoint: str, method: str = "GET", 
                     params: dict = None, json_data: dict = None) -> Optional[requests.Response]:
        """
        Make an authenticated API request.
        
        Args:
            device_id: Device identifier
            endpoint: API endpoint (e.g., "/YOUR_SHOP_API_PATH")
            method: HTTP method (GET, POST, etc.)
            params: Query parameters
            json_data: JSON body data
            
        Returns:
            Response object or None if failed
        """
        token_info = self.device_tokens.get(device_id)
        if not token_info:
            logger.error(f"No token for device {device_id}")
            return None
        
        url = self.BASE_URL + endpoint
        headers = {
            "Authorization": f"Bearer {token_info['token']}",
            "Accept": "application/json",
            "Content-Type": "application/json"
        }
        
        try:
            logger.debug(f"Making {method} request to {url}")
            if method == "GET":
                response = requests.get(url, headers=headers, params=params, timeout=10)
            elif method == "POST":
                # Log the exact JSON that will be sent
                import json as json_lib
                json_str = json_lib.dumps(json_data, indent=2)
                logger.info(f"📤 Exact JSON payload being sent:\n{json_str}")
                response = requests.post(url, headers=headers, json=json_data, timeout=10)
            elif method == "PUT":
                response = requests.put(url, headers=headers, params=params, json=json_data, timeout=10)
            elif method == "DELETE":
                response = requests.delete(url, headers=headers, params=params, timeout=10)
            else:
                logger.error(f"Unsupported HTTP method: {method}")
                return None
            
            logger.debug(f"Response status: {response.status_code}")
            return response
            
        except requests.exceptions.Timeout as e:
            logger.error(f"API request timed out after 10s: {url} - {e}")
            return None
        except requests.exceptions.ConnectionError as e:
            logger.error(f"API connection error: {url} - {e}")
            return None
        except Exception as e:
            logger.error(f"API request failed: {url} - {type(e).__name__}: {e}")
            return None
    
    def _get_case_matched_model(self, device_id: str, brand: str, model: str) -> str:
        """
        Get the exact case-matched model name from the database.
        If model exists with different casing, return the database version.
        Otherwise, return the input model (will be created by backend).
        
        Args:
            device_id: Device identifier
            brand: Device brand (case-insensitive)
            model: Device model (case-insensitive)
            
        Returns:
            Case-matched model name from database, or original if not found
        """
        logger.info(f"🔍 Checking model case for: '{model}' (brand: '{brand}')")
        try:
            # Fetch all brands with their models
            logger.info(f"🌐 Fetching brands from /YOUR_SHOP_API_PATH")
            response = self._make_request(device_id, "/YOUR_SHOP_API_PATH", method="GET")
            logger.info(f"📡 Brand API response received: status={response.status_code if response else 'None'}")
            
            if not response or response.status_code != 200:
                logger.warning(f"❌ Failed to fetch brands (status={response.status_code if response else 'None'}), using model as-is: {model}")
                return model
            
            brands = response.json()
            logger.debug(f"📋 Fetched {len(brands)} brands from API")
            
            # Find matching brand (case-insensitive)
            logger.debug(f"🔎 Searching for brand '{brand}' (case-insensitive)")
            matching_brand = next(
                (b for b in brands if b.get('value', '').lower() == brand.lower()),
                None
            )
            
            if not matching_brand:
                logger.info(f"❌ Brand '{brand}' not found in database, model will be created")
                return model
            
            logger.debug(f"✅ Found brand: {matching_brand.get('value')}, checking models...")
            
            # Find matching model in brand's models (case-insensitive)
            models = matching_brand.get('models', [])
            logger.debug(f"📱 Brand has {len(models)} models")
            
            matching_model = next(
                (m for m in models if m.get('value', '').lower() == model.lower()),
                None
            )
            
            if matching_model:
                exact_model = matching_model['value']
                if exact_model != model:
                    logger.info(f"✅ Model case-matched: '{model}' → '{exact_model}'")
                else:
                    logger.info(f"✅ Model case already correct: '{model}'")
                return exact_model
            else:
                logger.info(f"❌ Model '{model}' not found for brand '{brand}', will be created")
                return model
                
        except Exception as e:
            logger.error(f"💥 Error matching model case: {type(e).__name__}: {e}", exc_info=True)
            return model
    
    def create_ticket(self, device_id: str, username: str, password: str,
                     brand: str, model: str, problem: str,
                     device_condition: Optional[str] = None,
                     accessories: Optional[str] = None,
                     total_price: Optional[float] = None,
                     deposit: Optional[float] = None,
                     device_password: Optional[str] = None,
                     notes: Optional[str] = None) -> Tuple[bool, str, Optional[int]]:
        """
        Create a repair ticket.
        
        Args:
            device_id: Device identifier
            username: Phoneshop username
            password: Phoneshop password
            brand: Device brand
            model: Device model
            problem: Problem description
            
        Returns:
            (success, message, ticket_id)
        """
        # Ensure we have a valid token
        if not self._ensure_token(device_id, username, password):
            return False, "Failed to authenticate with Phoneshop", None
        
        # Match model casing with database to avoid duplicate issues
        model = self._get_case_matched_model(device_id, brand, model)
        
        # Prepare ticket data
        now_utc = datetime.now(timezone.utc)
        deadline_utc = now_utc + timedelta(hours=1)
        
        def to_iso_z(dt):
            # Web interface uses 3 digits for milliseconds (e.g., "2025-11-16T20:11:07.751Z")
            return dt.strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
        
        # Build base ticket data matching web interface format
        # CRITICAL: Web interface ALWAYS sends totalPrice=0 and deposit=0 if not provided
        # Backend requires these fields - missing them causes 500 error!
        ticket_data = {
            "deviceBrand": brand,
            "deviceModel": model,
            "problemExplanation": problem,
            "status": "PENDING",
            "deviceLocation": "IN_THE_FRONT",
            "withClient": False,
            "client": None,  # Explicitly null (matches web interface)
            "timestamp": to_iso_z(now_utc),
            "deadline": to_iso_z(deadline_utc),
            "customerRequest": None,
            "deviceCondition": None,
            "devicePassword": None,
            "serialNumberOrImei": None,
            "accessories": None,
            "notes": None,
            "totalPrice": 0,  # Default to 0 (matches web interface behavior)
            "deposit": 0      # Default to 0 (matches web interface behavior)
        }
        
        # Override optional fields if provided
        if device_condition:
            ticket_data["deviceCondition"] = str(device_condition)
        if accessories:
            ticket_data["accessories"] = str(accessories)
        if total_price is not None:
            try:
                ticket_data["totalPrice"] = float(total_price)
            except (ValueError, TypeError):
                logger.warning(f"Invalid totalPrice: {total_price}, using 0")
        if deposit is not None:
            try:
                ticket_data["deposit"] = float(deposit)
            except (ValueError, TypeError):
                logger.warning(f"Invalid deposit: {deposit}, using 0")
        if device_password:
            ticket_data["devicePassword"] = str(device_password)  # API expects string
        if notes:
            ticket_data["notes"] = str(notes)  # API expects string
        
        # Make request
        logger.info(f"Creating ticket with data: {ticket_data}")
        response = self._make_request(
            device_id,
            "/YOUR_SHOP_API_PATH",
            method="POST",
            json_data=ticket_data
        )
        
        if response is None:
            logger.error(f"create_ticket: _make_request returned None for device {device_id}")
        else:
            logger.info(f"create_ticket: Got response status {response.status_code}")
            if response.status_code != 200:
                logger.error(f"❌ API Error Response:")
                logger.error(f"📋 Headers: {dict(response.headers)}")
                try:
                    error_json = response.json()
                    logger.error(f"📝 Body (JSON): {error_json}")
                except:
                    logger.error(f"📝 Body (text): {response.text[:500]}")
        
        # Handle 401 (token expired) - retry once
        if response and response.status_code == 401:
            logger.info(f"Token expired for device {device_id}, re-authenticating...")
            if self._login(device_id, username, password):
                response = self._make_request(
                    device_id,
                    "/YOUR_SHOP_API_PATH",
                    method="POST",
                    json_data=ticket_data
                )
        
        # Parse response
        logger.info(f"🔍 About to parse response: response is not None = {response is not None}")
        try:
            response_bool = bool(response)
            logger.info(f"🔍 bool(response) = {response_bool}")
        except Exception as e:
            logger.error(f"⚠️ Exception when calling bool(response): {e}")
            response_bool = False
        
        if response_bool:
            logger.info(f"🔍 Response type: {type(response)}, has status_code: {hasattr(response, 'status_code')}")
            try:
                logger.info(f"🔍 Response status_code: {response.status_code}")
            except Exception as e:
                logger.error(f"⚠️ Cannot access response.status_code: {e}")
        
        if response and response.status_code == 200:
            try:
                result = response.json()
                ticket_id = result.get("id")
                if ticket_id:
                    logger.info(f"Created ticket {ticket_id} for device {device_id}")
                    return True, f"Ticket number {ticket_id}", ticket_id
                else:
                    logger.error(f"Ticket creation response missing ID. Response: {result}")
                    return False, "Ticket creation response missing ID", None
            except Exception as e:
                logger.error(f"Failed to parse ticket response: {e}. Response text: {response.text}")
                return False, "Failed to parse ticket response", None
        elif response:
            logger.error(f"❌ Ticket creation failed: HTTP {response.status_code}")
            logger.error(f"📋 Response headers: {dict(response.headers)}")
            logger.error(f"🔍 Attempting to parse error response...")
            try:
                error_body = response.json()
                logger.error(f"📝 Response body (JSON): {error_body}")
                error_msg = error_body.get('message', error_body.get('error', str(error_body)))
            except Exception as parse_error:
                logger.error(f"⚠️ Failed to parse JSON: {parse_error}")
                error_body = response.text[:500]
                logger.error(f"📝 Response body (text): {error_body}")
                error_msg = f"HTTP {response.status_code}"
            
            logger.error(f"💬 Final error message: {error_msg}")
            return False, f"Ticket creation failed: {error_msg}", None
        else:
            logger.error("❌ Ticket creation failed: No response received from API (response is None)")
            return False, "Failed to create ticket. Could not reach the ticketing system", None
    
    def create_invoice(self, device_id: str, username: str, password: str,
                      brand: str, model: str, total_price: float, 
                      serial_number: str, payment_method: str) -> Tuple[bool, str, Optional[str]]:
        """
        Create a sales invoice.
        
        Args:
            device_id: Device identifier
            username: Phoneshop username
            password: Phoneshop password
            brand: Device brand
            model: Device model
            total_price: Sale price
            serial_number: IMEI/serial number
            payment_method: CARD, CASH, or COMBINED
            
        Returns:
            (success, message, invoice_id)
        """
        # Ensure we have a valid token
        if not self._ensure_token(device_id, username, password):
            return False, "Failed to authenticate with Phoneshop", None
        
        # Prepare invoice data
        now_utc = datetime.now(timezone.utc)
        timestamp_str = now_utc.strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"
        
        invoice_data = {
            "deviceBrand": brand,
            "deviceModel": model,
            "deviceName": f"{brand} {model}",
            "warrantyPeriod": "ONE_MONTH",
            "paymentMethod": payment_method.upper(),
            "type": "SELL",
            "count": 1,
            "timestamp": timestamp_str,
            "totalPrice": float(total_price),
            "serialNumber": serial_number
        }
        
        # Make request
        response = self._make_request(
            device_id,
            "/YOUR_SHOP_API_PATH",
            method="POST",
            json_data=invoice_data
        )
        
        # Handle 401 (token expired) - retry once
        if response and response.status_code == 401:
            logger.info(f"Token expired for device {device_id}, re-authenticating...")
            if self._login(device_id, username, password):
                response = self._make_request(
                    device_id,
                    "/YOUR_SHOP_API_PATH",
                    method="POST",
                    json_data=invoice_data
                )
        
        # Parse response
        if response and response.status_code == 200:
            # API returns invoice ID as plain text
            invoice_id = response.text.strip()
            if invoice_id:
                logger.info(f"Created invoice {invoice_id} for device {device_id}")
                return True, f"Invoice {invoice_id} created successfully", invoice_id
            else:
                return False, "Invoice creation response missing ID", None
        elif response:
            logger.error(f"Invoice creation failed: {response.status_code} - {response.text}")
            return False, f"Failed to create invoice (error {response.status_code})", None
        else:
            return False, "Failed to create invoice (network error)", None
    
    def download_invoice_pdf(self, device_id: str, invoice_id: str, 
                            output_dir: str) -> Optional[str]:
        """
        Download invoice PDF.
        
        Args:
            device_id: Device identifier
            invoice_id: Invoice ID to download
            output_dir: Directory to save PDF
            
        Returns:
            Path to downloaded PDF or None if failed
        """
        token_info = self.device_tokens.get(device_id)
        if not token_info:
            logger.error(f"No token for device {device_id}")
            return None
        
        url = f"{self.BASE_URL}/YOUR_SHOP_API_PATH"
        headers = {
            "Authorization": f"Bearer {token_info['token']}",
            "Accept": "application/pdf"
        }
        
        try:
            response = requests.post(
                url,
                headers=headers,
                params={"invoiceId": invoice_id},
                timeout=30
            )
            
            if response.status_code == 200:
                # Save PDF
                os.makedirs(output_dir, exist_ok=True)
                filename = os.path.join(output_dir, f"invoice_{invoice_id}.pdf")
                
                with open(filename, "wb") as f:
                    f.write(response.content)
                
                logger.info(f"Downloaded invoice PDF: {filename}")
                return filename
            else:
                logger.error(f"PDF download failed: {response.status_code}")
                return None
                
        except Exception as e:
            logger.error(f"PDF download error: {e}")
            return None
    
    def get_ticket_status(self, device_id: str, username: str, password: str,
                         ticket_id: str) -> Tuple[bool, str, Optional[dict]]:
        """
        Get ticket status.
        
        Args:
            device_id: Device identifier
            username: Phoneshop username
            password: Phoneshop password
            ticket_id: Ticket ID to query
            
        Returns:
            (success, message, ticket_data)
        """
        # Ensure we have a valid token
        if not self._ensure_token(device_id, username, password):
            return False, "Failed to authenticate with Phoneshop", None
        
        # Make request
        response = self._make_request(
            device_id,
            f"/YOUR_SHOP_API_PATH",
            method="GET",
            params={"id": ticket_id}
        )
        
        # Handle 401 (token expired) - retry once
        if response and response.status_code == 401:
            logger.info(f"Token expired for device {device_id}, re-authenticating...")
            if self._login(device_id, username, password):
                response = self._make_request(
                    device_id,
                    f"/YOUR_SHOP_API_PATH",
                    method="GET",
                    params={"id": ticket_id}
                )
        
        # Parse response
        if response and response.status_code == 200:
            ticket_data = response.json()
            logger.info(f"Retrieved ticket {ticket_id} for device {device_id}")
            
            # Format response
            status = ticket_data.get('status', 'Unknown')
            brand = ticket_data.get('deviceBrand', 'Unknown')
            model = ticket_data.get('deviceModel', 'Unknown')
            problem = ticket_data.get('problemExplanation', 'No description')
            
            message = f"Ticket {ticket_id}: {status}. Device: {brand} {model}. Problem: {problem}"
            return True, message, ticket_data
        elif response:
            logger.error(f"Ticket query failed: {response.status_code}")
            return False, f"Failed to get ticket status (error {response.status_code})", None
        else:
            return False, "Failed to get ticket status (network error)", None
    
    def update_ticket_status(self, device_id: str, username: str, password: str,
                            ticket_id: str, status_endpoint: str) -> Tuple[bool, str]:
        """
        Update ticket status (start, complete, freeze, cancel, etc.).
        
        Args:
            device_id: Device identifier
            username: Phoneshop username
            password: Phoneshop password
            ticket_id: Ticket ID to update
            status_endpoint: Full endpoint with params (e.g., "/YOUR_SHOP_API_PATH?id=123")
            
        Returns:
            (success, message)
        """
        # Ensure we have a valid token
        if not self._ensure_token(device_id, username, password):
            return False, "Failed to authenticate with Phoneshop"
        
        # Make request
        response = self._make_request(
            device_id,
            status_endpoint,
            method="PUT"
        )
        
        # Handle 401 (token expired) - retry once
        if response and response.status_code == 401:
            logger.info(f"Token expired for device {device_id}, re-authenticating...")
            if self._login(device_id, username, password):
                response = self._make_request(
                    device_id,
                    status_endpoint,
                    method="PUT"
                )
        
        # Parse response
        if response and response.status_code == 200:
            logger.info(f"Updated ticket {ticket_id} status for device {device_id}")
            return True, f"Ticket {ticket_id} updated successfully"
        elif response:
            logger.error(f"Ticket status update failed: {response.status_code} - {response.text}")
            return False, f"Failed to update ticket (error {response.status_code})"
        else:
            return False, "Failed to update ticket (network error)"
    
    def print_ticket_document(self, device_id: str, username: str, password: str,
                             ticket_id: str, document_type: str = "ticket",
                             output_dir: str = "invoices") -> Tuple[bool, str, Optional[str]]:
        """
        Print ticket document (receipt or repair tag).
        
        Args:
            device_id: Device identifier
            username: Phoneshop username
            password: Phoneshop password
            ticket_id: Ticket ID to print
            document_type: "ticket" for receipt or "tag" for repair tag
            output_dir: Directory to save PDF
            
        Returns:
            (success, message, pdf_path)
        """
        # Ensure we have a valid token
        if not self._ensure_token(device_id, username, password):
            return False, "Failed to authenticate with Phoneshop", None
        
        # Determine endpoint
        if document_type == "tag":
            endpoint = "/YOUR_SHOP_API_PATH"
        else:
            endpoint = "/YOUR_SHOP_API_PATH"
        
        token_info = self.device_tokens.get(device_id)
        url = f"{self.BASE_URL}{endpoint}"
        headers = {
            "Authorization": f"Bearer {token_info['token']}",
            "Accept": "application/pdf"
        }
        
        try:
            response = requests.post(
                url,
                headers=headers,
                params={"ticketId": ticket_id},
                timeout=30
            )
            
            if response.status_code == 200:
                # Save PDF
                os.makedirs(output_dir, exist_ok=True)
                filename = os.path.join(output_dir, f"ticket_{ticket_id}_{document_type}.pdf")
                
                with open(filename, "wb") as f:
                    f.write(response.content)
                
                logger.info(f"Downloaded ticket {document_type} PDF: {filename}")
                return True, f"Ticket {ticket_id} {document_type} ready to print", filename
            else:
                logger.error(f"PDF download failed: {response.status_code}")
                return False, f"Failed to generate {document_type} (error {response.status_code})", None
                
        except Exception as e:
            logger.error(f"PDF download error: {e}")
            return False, f"Failed to generate {document_type}", None
    
    def get_pending_tickets(self, device_id: str, username: str, password: str) -> Tuple[bool, str, Optional[list]]:
        """
        Get list of pending/active tickets.
        
        Args:
            device_id: Device identifier
            username: Phoneshop username
            password: Phoneshop password
            
        Returns:
            (success, message, tickets_list)
        """
        # Ensure we have a valid token
        if not self._ensure_token(device_id, username, password):
            return False, "Failed to authenticate with Phoneshop", None
        
        # Get shop ID from token info
        token_info = self.device_tokens.get(device_id)
        shop_id = token_info.get('shop_id') if token_info else None
        
        # Make request with filter for active/pending tickets
        params = {
            'status': 'PENDING,IN_PROGRESS',  # Filter for active tickets
            'page': 0,
            'size': 10,  # Get top 10 pending tickets
            'sort': 'id,asc'  # Oldest first
        }
        
        if shop_id:
            params['shopId'] = shop_id
        
        response = self._make_request(
            device_id,
            "/YOUR_SHOP_API_PATH",
            method="GET",
            params=params
        )
        
        # Handle 401 (token expired) - retry once
        if response and response.status_code == 401:
            logger.info(f"Token expired for device {device_id}, re-authenticating...")
            if self._login(device_id, username, password):
                response = self._make_request(
                    device_id,
                    "/YOUR_SHOP_API_PATH",
                    method="GET",
                    params=params
                )
        
        # Parse response
        if response and response.status_code == 200:
            try:
                tickets = response.json()
                
                if not tickets or len(tickets) == 0:
                    return True, "No pending tickets.", []
                
                # Format message: "123 iPhone 11 for screen, 124 iPhone 12 for charge port"
                formatted_lines = []
                for ticket in tickets:
                    ticket_id = ticket.get('id', 'Unknown')
                    brand = ticket.get('deviceBrand', '')
                    model = ticket.get('deviceModel', '')
                    problem = ticket.get('problemExplanation', '')
                    
                    # Extract just the main issue (before comma or period)
                    if ',' in problem:
                        problem = problem.split(',')[0]
                    if '.' in problem:
                        problem = problem.split('.')[0]
                    
                    formatted_lines.append(f"{ticket_id} {brand} {model} for {problem}")
                
                message = ", ".join(formatted_lines)
                logger.info(f"Retrieved {len(tickets)} pending tickets for device {device_id}")
                return True, message, tickets
                
            except Exception as e:
                logger.error(f"Failed to parse pending tickets: {e}")
                return False, "Failed to parse tickets list", None
        elif response:
            logger.error(f"Get pending tickets failed: {response.status_code}")
            return False, f"Failed to get pending tickets (error {response.status_code})", None
        else:
            return False, "Failed to get pending tickets (network error)", None


def print_pdf_windows(filename: str) -> bool:
    """
    Print PDF on Windows using default printer.
    
    Args:
        filename: Path to PDF file
        
    Returns:
        True if print successful, False otherwise
    """
    try:
        from win32 import win32print, win32api
        
        printer_name = win32print.GetDefaultPrinter()
        win32api.ShellExecute(
            0,
            "print",
            filename,
            f'/d:"{printer_name}"',
            ".",
            0
        )
        logger.info(f"Sent PDF to printer: {printer_name}")
        return True
        
    except ImportError:
        logger.warning("pywin32 not installed - cannot print PDF")
        return False
    except Exception as e:
        logger.error(f"Print error: {e}")
        return False

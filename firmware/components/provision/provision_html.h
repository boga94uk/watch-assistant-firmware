#pragma once

/* Old ESP Assistant captive portal, trimmed to what this firmware can actually do. */

static const char PORTAL_HEAD[] =
    "<!doctype html><html><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>ESP Assistant</title><style>"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
    "background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:16px}"
    ".wrap{max-width:560px;margin:0 auto;background:#fff;border-radius:20px;"
    "box-shadow:0 20px 60px rgba(0,0,0,.3);overflow:hidden}"
    ".head{background:linear-gradient(135deg,#667eea,#764ba2);color:#fff;padding:24px;text-align:center}"
    ".head h1{font-size:24px;font-weight:700}"
    ".head p{opacity:.85;font-size:13px;margin-top:6px}"
    ".mac{font-family:ui-monospace,monospace;font-size:12px;opacity:.8;margin-top:4px}"
    ".tabs{display:flex;background:#f8f9fa;border-bottom:2px solid #dee2e6}"
    ".tab{flex:1;padding:14px 8px;border:0;background:none;font-size:15px;color:#666;cursor:pointer}"
    ".tab.on{color:#667eea;font-weight:700;background:#fff;border-bottom:3px solid #667eea}"
    ".pane{display:none;padding:22px}"
    ".pane.on{display:block}"
    ".card{background:#f8f9fa;border-radius:12px;padding:16px;margin:0 0 14px;border-left:4px solid #667eea}"
    ".card h2{font-size:16px;margin-bottom:10px;color:#333}"
    ".row{display:flex;justify-content:space-between;gap:12px;padding:8px 0;border-bottom:1px solid #dee2e6;font-size:14px}"
    ".row span{color:#666}"
    "label{display:block;font-size:12px;color:#666;margin:12px 0 6px;letter-spacing:.04em;text-transform:uppercase}"
    "input{width:100%;padding:12px;border-radius:10px;border:1px solid #ddd;font-size:16px}"
    "button,.btn{display:inline-block;width:100%;margin-top:12px;padding:14px;border:0;border-radius:12px;"
    "background:#667eea;color:#fff;font-weight:700;font-size:16px;text-align:center;text-decoration:none}"
    ".ok{background:#28a745}.warn{background:#dc3545}"
    ".ghost{background:#6c757d}"
    ".okbox{background:#d4edda;color:#155724;padding:10px;border-radius:8px;margin-bottom:12px}"
    ".infobox{background:#d1ecf1;color:#0c5460;padding:10px;border-radius:8px;margin-bottom:12px;font-size:14px}"
    "</style><script>"
    "function tab(n){document.querySelectorAll('.pane').forEach(p=>p.classList.remove('on'));"
    "document.querySelectorAll('.tab').forEach(t=>t.classList.remove('on'));"
    "document.getElementById(n).classList.add('on');"
    "document.getElementById('t'+n).classList.add('on')}"
    "</script></head><body><div class=wrap>";

static const char PORTAL_TAIL[] = "</div></body></html>";

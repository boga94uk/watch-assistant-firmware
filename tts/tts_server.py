#!/usr/bin/env python3
"""Kyutai Pocket TTS sidecar for the watch shop (CPU, int8).

Copy of the Pocket Voice engine, owned by D:\\Grok. Speaks WAV at 16 kHz
mono 16-bit so the ESP32 can play it without a second convert step.
"""

from __future__ import annotations

import io
import json
import os
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from math import gcd

os.environ.setdefault("OMP_NUM_THREADS", "2")
os.environ.setdefault("MKL_NUM_THREADS", "2")
os.environ.setdefault("TOKENIZERS_PARALLELISM", "false")

import numpy as np
import scipy.io.wavfile
from scipy.signal import resample_poly
import torch

torch.set_num_threads(2)

from pocket_tts import TTSModel

HOST = os.environ.get("POCKET_TTS_HOST", "127.0.0.1")
PORT = int(os.environ.get("POCKET_TTS_PORT", "8090"))
TARGET_RATE = int(os.environ.get("POCKET_TTS_RATE", "16000"))

STATE: dict = {
    "model": None,
    "voices": {},
    "ready": False,
    "error": None,
    "loading": True,
    "sample_rate": TARGET_RATE,
}
LOCK = threading.Lock()


def load_model() -> None:
    try:
        model = TTSModel.load_model(language="english", quantize=True)
        with LOCK:
            STATE["model"] = model
            STATE["ready"] = True
            STATE["loading"] = False
            STATE["error"] = None
            STATE["sample_rate"] = TARGET_RATE or int(model.sample_rate)
        print("[pocket-tts] model ready", flush=True)
    except Exception as exc:  # noqa: BLE001
        with LOCK:
            STATE["loading"] = False
            STATE["error"] = str(exc)
        print(f"[pocket-tts] load failed: {exc}", flush=True)


def _to_wav(array: np.ndarray, src_rate: int) -> bytes:
    if array.ndim > 1:
        array = np.mean(array, axis=-1)
    if array.dtype != np.float32:
        array = array.astype(np.float32)
    out_rate = src_rate
    if TARGET_RATE and TARGET_RATE != src_rate and array.size:
        g = gcd(int(src_rate), int(TARGET_RATE))
        array = resample_poly(array, TARGET_RATE // g, src_rate // g).astype(np.float32)
        out_rate = TARGET_RATE
    peak = float(np.max(np.abs(array))) if array.size else 0.0
    if peak > 1.0:
        array = array / peak
    pcm = np.clip(array * 32767.0, -32768, 32767).astype(np.int16)
    buf = io.BytesIO()
    scipy.io.wavfile.write(buf, int(out_rate), pcm)
    return buf.getvalue()


def synthesize(text: str, voice: str) -> bytes:
    with LOCK:
        model: TTSModel | None = STATE["model"]
        if model is None:
            raise RuntimeError(STATE["error"] or "Pocket TTS is still loading")
        cached = STATE["voices"].get(voice)
    if cached is None:
        cached = model.get_state_for_audio_prompt(voice)
        with LOCK:
            STATE["voices"][voice] = cached
    audio = model.generate_audio(cached, text)
    array = audio.detach().cpu().numpy()
    return _to_wav(array, int(model.sample_rate))


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt: str, *args) -> None:  # noqa: A003
        print("[pocket-tts]", fmt % args, flush=True)

    def _cors(self) -> None:
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")

    def do_OPTIONS(self) -> None:  # noqa: N802
        self.send_response(204)
        self._cors()
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        if self.path.split("?", 1)[0] not in ("/health", "/"):
            self.send_error(404)
            return
        with LOCK:
            payload = {
                "ok": True,
                "engine": "pocket-tts",
                "ready": STATE["ready"],
                "loading": STATE["loading"],
                "error": STATE["error"],
                "sample_rate": STATE["sample_rate"],
            }
        body = json.dumps(payload).encode()
        self.send_response(200)
        self._cors()
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self) -> None:  # noqa: N802
        if self.path.split("?", 1)[0] != "/tts":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length") or 0)
        try:
            data = json.loads(self.rfile.read(length) or b"{}")
        except json.JSONDecodeError:
            self.send_error(400, "invalid json")
            return
        text = str(data.get("text") or "").strip()[:800]
        voice = str(data.get("voice") or "alba").strip() or "alba"
        if not text:
            self.send_error(400, "text required")
            return
        try:
            wav = synthesize(text, voice)
        except Exception as exc:  # noqa: BLE001
            body = json.dumps({"ok": False, "error": str(exc)}).encode()
            self.send_response(503)
            self._cors()
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        self.send_response(200)
        self._cors()
        self.send_header("Content-Type", "audio/wav")
        self.send_header("Content-Length", str(len(wav)))
        self.end_headers()
        self.wfile.write(wav)


def main() -> None:
    threading.Thread(target=load_model, daemon=True).start()
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    print(f"[pocket-tts] listening on {HOST}:{PORT} rate={TARGET_RATE}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()

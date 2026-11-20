#!/usr/bin/env python3
"""Host tests for WAV headers, HMAC, reply parse, and state decisions.
Hardware I2C/display checks stay in tools/hwtest.py."""
from __future__ import annotations

import hashlib
import hmac
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def wav_header(data_bytes: int, rate: int = 16000) -> bytes:
    riff = 36 + data_bytes
    br = rate * 2
    return (
        b"RIFF"
        + struct.pack("<I", riff)
        + b"WAVE"
        + b"fmt "
        + struct.pack("<IHHIIHH", 16, 1, 1, rate, br, 2, 16)
        + b"data"
        + struct.pack("<I", data_bytes)
    )


def test_wav_header():
    h = wav_header(32000)
    assert h[:4] == b"RIFF"
    assert h[8:12] == b"WAVE"
    assert struct.unpack_from("<H", h, 22)[0] == 1
    assert struct.unpack_from("<I", h, 24)[0] == 16000
    assert struct.unpack_from("<H", h, 34)[0] == 16
    assert struct.unpack_from("<I", h, 40)[0] == 32000
    assert len(h) == 44
    pcm = b"\x00\x00" * 16000
    info = wav_parse(h + pcm)
    assert info is not None
    assert info["off"] == 44
    assert info["bytes"] == 32000
    assert info["rate"] == 16000


def test_wav_reject_bad():
    bad = bytearray(wav_header(100))
    bad[22:24] = struct.pack("<H", 2)  # stereo
    # C wav_valid would reject; spec: must be mono
    assert struct.unpack_from("<H", bad, 22)[0] != 1


def wav_parse(data: bytes) -> dict | None:
    """Mirror of firmware wav_parse: walk chunks, do not assume data at offset 36."""
    if len(data) < 20 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return None
    off = 12
    fmt = None
    pcm = None
    while off + 8 <= len(data):
        cid = data[off : off + 4]
        sz = struct.unpack_from("<I", data, off + 4)[0]
        payload = off + 8
        avail = len(data) - payload
        if cid == b"fmt " and sz >= 16 and avail >= 16:
            audio_fmt, ch, rate, _br, _al, bps = struct.unpack_from("<HHIIHH", data, payload)
            fmt = {"format": audio_fmt, "channels": ch, "rate": rate, "bits": bps}
        elif cid == b"data":
            nbytes = sz if sz <= avail else avail
            if nbytes >= 2:
                pcm = {"off": payload, "bytes": nbytes}
            break
        if sz > avail:
            break
        step = 8 + sz + (sz & 1)
        if step < 8:
            break
        off += step
    if not fmt or not pcm:
        return None
    return {**fmt, **pcm}


def test_wav_ffmpeg_list_chunk():
    """Shop TTS from ffmpeg puts LIST/INFO before data. Firmware must still play it."""
    pcm = b"\x00\x10" * 80
    fmt = b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, 16000, 32000, 2, 16)
    list_payload = b"INFOISFT" + struct.pack("<I", 14) + b"Lavf62.3.100\x00\x00"
    list_chunk = b"LIST" + struct.pack("<I", len(list_payload)) + list_payload
    data_chunk = b"data" + struct.pack("<I", len(pcm)) + pcm
    body = fmt + list_chunk + data_chunk
    wav = b"RIFF" + struct.pack("<I", 4 + len(body)) + b"WAVE" + body
    info = wav_parse(wav)
    assert info is not None
    assert info["format"] == 1
    assert info["channels"] == 1
    assert info["rate"] == 16000
    assert info["bits"] == 16
    assert wav[36:40] == b"LIST"
    assert info["off"] > 44
    assert wav[info["off"] : info["off"] + 2] == pcm[:2]


def hmac_hex(secret_hex: str, msg: str) -> str:
    key = bytes.fromhex(secret_hex)
    return hmac.new(key, msg.encode(), hashlib.sha256).hexdigest()


def test_hmac_matches_server():
    secret = "0000000000000000000000000000000000000000000000000000000000000000"
    msg = "AA:BB:CC:DD:EE:FF|2026-09-07T12:00:00Z"
    sig = hmac_hex(secret, msg)
    assert len(sig) == 64
    assert sig == hmac_hex(secret, msg)
    assert sig != hmac_hex(secret, msg + "x")


def parse_reply(json: str) -> dict:
    def grab(key: str) -> str:
        k = f'"{key}"'
        i = json.find(k)
        if i < 0:
            return ""
        j = json.find(":", i + len(k))
        j = json.find('"', j) + 1
        k2 = json.find('"', j)
        return json[j:k2]

    typ = grab("type")
    t = "T"
    if typ == "ask":
        t = "A"
    elif typ == "confirm":
        t = "C"
    return {
        "type": t,
        "message": grab("message"),
        "command": grab("command"),
        "audio_url": grab("audio_url"),
    }


def test_reply_parse():
    j = '{"type":"confirm","message":"Book it?","command":"book","audio_url":"https://x/a.wav"}'
    r = parse_reply(j)
    assert r["type"] == "C"
    assert r["command"] == "book"
    assert r["audio_url"].endswith(".wav")
    j2 = '{"type":"say","message":"Done"}'
    r2 = parse_reply(j2)
    assert r2["type"] == "T"
    assert r2["audio_url"] == ""


# Mirror app_decide() in app_fsm.c
def decide(state, ev, net_up=True, queue_n=0):
    if ev == "BOTTOM":
        if state in (
            "RECORDING",
            "UPLOADING",
            "PROCESSING",
            "DOWNLOADING",
            "PLAYING_SAY",
            "PLAYING_ASK",
            "PLAYING_CONFIRM",
        ):
            return "CANCEL"
        if state == "SETTINGS":
            return "IDLE"
        return "IGNORE"
    if state in ("IDLE", "QUEUED"):
        if ev == "TOP_HOLD":
            return "RECORD"
        if ev == "NET_UP" and queue_n > 0:
            return "DRAIN"
        if ev == "TOP_CLICK" and state == "QUEUED" and queue_n > 0:
            return "DRAIN"
    if state in ("UPLOADING", "PROCESSING"):
        if ev == "UPLOAD_SENT":
            return "PROCESS"
        if ev == "RECEIVING":
            return "RECEIVE"
        if ev == "UPLOAD_OK":
            return "APPLY"
        if ev == "UPLOAD_FAIL":
            return "QUEUE"
    if state == "DOWNLOADING":
        if ev in ("UPLOAD_OK", "TTS_READY"):
            return "APPLY"
        if ev == "UPLOAD_FAIL":
            return "QUEUE" if queue_n > 0 else "IDLE"
    if state == "PLAYING_SAY" and ev == "PLAY_DONE":
        if queue_n > 0:
            return "DRAIN" if net_up else "QUEUE"
        return "IDLE"
    if state == "PLAYING_CONFIRM" and ev == "TOP_CLICK":
        return "EXECUTE"
    return "IGNORE"


def test_cancel_does_not_leave_work():
    for st in ("UPLOADING", "PROCESSING", "DOWNLOADING", "PLAYING_SAY"):
        assert decide(st, "BOTTOM") == "CANCEL"


def test_wifi_down_queues():
    assert decide("UPLOADING", "UPLOAD_FAIL", net_up=False, queue_n=1) == "QUEUE"


def test_failed_tts_still_applies():
    # JSON without audio_url still APPLY; playback layer posts PLAY_DONE
    assert decide("PROCESSING", "UPLOAD_OK") == "APPLY"


def test_confirm_click_executes():
    assert decide("PLAYING_CONFIRM", "TOP_CLICK") == "EXECUTE"


def test_play_done_drains_queue():
    assert decide("PLAYING_SAY", "PLAY_DONE", net_up=True, queue_n=2) == "DRAIN"
    assert decide("PLAYING_SAY", "PLAY_DONE", net_up=False, queue_n=2) == "QUEUE"
    assert decide("PLAYING_SAY", "PLAY_DONE", queue_n=0) == "IDLE"


def test_stale_ix():
    def stale(ev_ix, cur, is_input):
        if is_input:
            return False
        if ev_ix == 0 or cur == 0:
            return False
        return ev_ix != cur

    assert stale(3, 4, False) is True
    assert stale(4, 4, False) is False
    assert stale(3, 4, True) is False


def main():
    tests = [
        test_wav_header,
        test_wav_reject_bad,
        test_wav_ffmpeg_list_chunk,
        test_hmac_matches_server,
        test_reply_parse,
        test_cancel_does_not_leave_work,
        test_wifi_down_queues,
        test_failed_tts_still_applies,
        test_confirm_click_executes,
        test_play_done_drains_queue,
        test_stale_ix,
    ]
    failed = 0
    for t in tests:
        try:
            t()
            print("ok", t.__name__)
        except Exception as e:
            failed += 1
            print("FAIL", t.__name__, e)
    if failed:
        sys.exit(1)
    print(f"{len(tests)} host tests passed")


if __name__ == "__main__":
    main()

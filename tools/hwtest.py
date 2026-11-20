"""Serial hardware self-test for watch_assistant.

Usage:
  python D:\\Grok\\tools\\hwtest.py --port COM8
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import time

try:
    import serial
except ImportError:
    print("pip install pyserial", file=sys.stderr)
    sys.exit(2)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM8")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=8)
    args = parser.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    time.sleep(0.3)
    ser.reset_input_buffer()
    ser.write(b"\r\ntest\r\n")
    deadline = time.time() + args.timeout
    buf = ""
    result = None
    while time.time() < deadline:
        chunk = ser.read(1024).decode("utf-8", errors="replace")
        if chunk:
            buf += chunk
            sys.stdout.write(chunk)
            sys.stdout.flush()
            match = re.search(r"TEST_RESULT (\{.*\})", buf)
            if match:
                result = json.loads(match.group(1))
                break
    ser.close()
    if not result:
        print("\nFAIL: no TEST_RESULT (is the watch on this COM port?)", file=sys.stderr)
        return 1
    print("\nparsed:", json.dumps(result))
    return 0 if result.get("pass") else 1


if __name__ == "__main__":
    raise SystemExit(main())

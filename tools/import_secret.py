"""Import an API key from a dropped file into shop .env. Never prints the key."""
from __future__ import annotations

import argparse
import os
import sys

ENV_PATH = r"YOUR_VOICE_ASSISTANT_SERVER_DIR\config\.env"


def read_key(path: str) -> str:
    with open(path, "rb") as fh:
        raw = fh.read()
    if raw.startswith(b"\xff\xfe") or raw.startswith(b"\xfe\xff"):
        text = raw.decode("utf-16")
    else:
        text = raw.decode("utf-8-sig")
    text = text.strip().strip("\x00")
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if "=" in line and not line.startswith("sk-") and not line.startswith("xai-"):
            line = line.split("=", 1)[1].strip().strip('"').strip("'")
        return line
    raise SystemExit("key file is empty")


def upsert_env(env_path: str, var: str, value: str) -> None:
    os.makedirs(os.path.dirname(env_path), exist_ok=True)
    lines: list[str] = []
    if os.path.exists(env_path):
        with open(env_path, "r", encoding="utf-8-sig") as fh:
            lines = fh.read().splitlines()
    found = False
    out: list[str] = []
    prefix = f"{var}="
    for line in lines:
        if line.startswith(prefix) or line.startswith(f"{var} ="):
            out.append(prefix + value)
            found = True
        else:
            out.append(line)
    if not found:
        if out and out[-1] != "":
            out.append("")
        out.append(prefix + value)
    with open(env_path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(out) + "\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--key-file", required=True)
    parser.add_argument("--kind", choices=("openai", "xai"), default="openai")
    args = parser.parse_args()
    if not os.path.isfile(args.key_file):
        print(f"FAILED missing {args.key_file}", file=sys.stderr)
        return 1
    key = read_key(args.key_file)
    if len(key) < 20:
        print("FAILED key too short", file=sys.stderr)
        return 1
    var = "XAI_API_KEY" if args.kind == "xai" else "OPENAI_API_KEY"
    upsert_env(ENV_PATH, var, key)
    try:
        os.remove(args.key_file)
    except OSError:
        pass
    print(f"IMPORTED {args.kind} chars={len(key)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

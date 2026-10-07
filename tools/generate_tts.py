#!/usr/bin/env python3
# Copyright (c) 2026 DankBuild - RadarAura. RadarAura Build Licence 1.0 - see LICENSE.md
"""
Generate TTS alert audio files using Replicate's minimax/speech-02-hd model.

Usage:
    # Reads token from REPLICATE_API_TOKEN env var, or from .env file at repo root.
    python3 tools/generate_tts.py

Produces .mp3 files in tools/tts_output/voiceN/ matching the filenames the firmware expects.
Copy the contents of tools/tts_output/ to /alerts/ on the microSD card so the layout becomes:
    /alerts/voice1/co2.mp3
    /alerts/voice1/pm1.mp3
    ...
    /alerts/voice2/co2.mp3
    ...
"""

import json
import os
import pathlib
import re
import sys
import time
import urllib.request
import urllib.error


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
ENV_FILE = REPO_ROOT / ".env"
OUT_DIR = REPO_ROOT / "tools" / "tts_output"

REPLICATE_API_BASE = "https://api.replicate.com/v1"
MODEL = "minimax/speech-02-hd"

# Voice options exposed in the firmware dropdown — index here MUST match firmware enum order
VOICES = [
    ("voice1", "English_CalmWoman",      "Calm"),
    ("voice2", "English_LovelyGirl",     "Lovely"),
    ("voice3", "English_Kind-heartedGirl","Kind"),
    ("voice4", "English_Graceful_Lady",  "Graceful"),
]

# (filename without extension, phrase) — must match metric_tts_files[] in main.c
PHRASES = [
    ("co2",   "Warning. Carbon dioxide level is high. Please open a window."),
    ("pm1",   "Warning. Ultra fine particles detected. Air quality is poor."),
    ("pm25",  "Warning. Fine particles detected. Air quality is poor."),
    ("pm4",   "Warning. Fine particles detected. Air quality is degraded."),
    ("pm10",  "Warning. Particulate matter detected. Please ventilate the room."),
    ("voc",   "Warning. Volatile organic compounds detected. Check for solvents or chemicals."),
    ("nox",   "Warning. Nitrogen oxide level is elevated. Check for combustion sources."),
    ("temp",  "Warning. Temperature is too high."),
    ("humid", "Warning. Humidity is too high."),
    ("fire",  "Attention. Possible fire or smoke detected. Check immediately."),
]


def load_token() -> str:
    # Prefer .env file (user-controlled), fall back to env var
    if ENV_FILE.exists():
        for line in ENV_FILE.read_text().splitlines():
            m = re.match(r'^\s*replicateAPIKey\s*=\s*"?([^"]+)"?\s*$', line)
            if m:
                tok = m.group(1).strip()
                if tok and tok.startswith("r8_"):
                    return tok
    tok = os.getenv("REPLICATE_API_TOKEN", "").strip()
    if tok and tok.startswith("r8_"):
        return tok
    print("Error: replicateAPIKey not found in .env (or REPLICATE_API_TOKEN env var). "
          "Replicate API tokens start with 'r8_'.", file=sys.stderr)
    sys.exit(1)


def http_post(url: str, token: str, body: dict, headers_extra: dict | None = None) -> dict:
    headers = {
        "Authorization": f"Bearer {token}",
        "Content-Type": "application/json",
    }
    if headers_extra:
        headers.update(headers_extra)
    req = urllib.request.Request(
        url,
        data=json.dumps(body).encode("utf-8"),
        headers=headers,
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=120) as resp:
        return json.loads(resp.read())


def http_get(url: str, token: str) -> dict:
    req = urllib.request.Request(
        url,
        headers={"Authorization": f"Bearer {token}"},
    )
    with urllib.request.urlopen(req, timeout=60) as resp:
        return json.loads(resp.read())


def http_get_bytes(url: str) -> bytes:
    req = urllib.request.Request(url)
    with urllib.request.urlopen(req, timeout=120) as resp:
        return resp.read()


def synth_one(token: str, voice_id: str, text: str, out_path: pathlib.Path) -> bool:
    url = f"{REPLICATE_API_BASE}/models/{MODEL}/predictions"
    body = {
        "input": {
            "text": text,
            "voice_id": voice_id,
            "speed": 1.0,
            "volume": 1.0,
            "pitch": 0,
            "emotion": "neutral",
            "english_normalization": True,
            "sample_rate": 32000,
            "bitrate": 128000,
            "channel": "mono",
            "language_boost": "English",
        },
    }

    try:
        # Use Prefer: wait so the call is synchronous (no polling needed when fast).
        prediction = http_post(url, token, body, {"Prefer": "wait=60"})
    except urllib.error.HTTPError as e:
        print(f"    HTTP {e.code}: {e.read().decode('utf-8', 'ignore')[:300]}", file=sys.stderr)
        return False
    except Exception as e:
        print(f"    Error: {e}", file=sys.stderr)
        return False

    # Poll if not finished yet
    status = prediction.get("status")
    pred_url = prediction.get("urls", {}).get("get")
    while status not in ("succeeded", "failed", "canceled"):
        time.sleep(1.5)
        try:
            prediction = http_get(pred_url, token)
            status = prediction.get("status")
        except Exception as e:
            print(f"    Poll error: {e}", file=sys.stderr)
            return False

    if status != "succeeded":
        print(f"    Prediction {status}: {prediction.get('error')}", file=sys.stderr)
        return False

    output = prediction.get("output")
    if isinstance(output, list):
        output = output[0] if output else None
    if not isinstance(output, str):
        print(f"    Unexpected output type: {type(output)}", file=sys.stderr)
        return False

    try:
        audio_bytes = http_get_bytes(output)
    except Exception as e:
        print(f"    Download failed: {e}", file=sys.stderr)
        return False

    out_path.write_bytes(audio_bytes)
    return True


def main() -> int:
    token = load_token()
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    print(f"Output dir: {OUT_DIR}")
    print(f"Voices: {[v[2] for v in VOICES]}\n")

    failed = []
    for slug, voice_id, label in VOICES:
        voice_dir = OUT_DIR / slug
        voice_dir.mkdir(exist_ok=True)
        print(f"=== {label} ({voice_id}) ===")
        for name, text in PHRASES:
            out = voice_dir / f"{name}.mp3"
            if out.exists() and out.stat().st_size > 0:
                print(f"  [{name}] (skipping, exists)")
                continue
            print(f"  [{name}] {text[:50]}...")
            if synth_one(token, voice_id, text, out):
                print(f"    -> {out.name} ({out.stat().st_size} bytes)")
            else:
                failed.append(f"{slug}/{name}")
        print()

    if failed:
        print(f"Failed: {failed}", file=sys.stderr)
        return 2

    print("All files generated.")
    print(f"\nNext steps:")
    print(f"  1. Insert a FAT32-formatted microSD card into the JC4880P443 board")
    print(f"  2. Create a directory called 'alerts' at the SD root")
    print(f"  3. Copy the voiceN/ subfolders from {OUT_DIR} into /alerts/ on the SD card")
    print(f"     Final layout: /alerts/voice1/co2.mp3, /alerts/voice2/co2.mp3, etc.")
    print(f"  4. Power-cycle the board, open Settings, pick a voice from the dropdown")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

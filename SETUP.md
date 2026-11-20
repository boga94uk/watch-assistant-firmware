# Watch assistant and voice assistant server

These two repositories are one system.

- [watch-assistant-firmware](https://github.com/boga94uk/watch-assistant-firmware) is the ESP32-S3 watch. It records speech, signs the request, and posts it to the server. It plays the spoken reply.
- [voice-assistant-server](https://github.com/boga94uk/voice-assistant-server) runs on the shop PC. It checks the signature, transcribes the audio, calls the Businessflow phoneshop API, and sends text and audio back.

The watch does not call Businessflow itself. It only talks to this server. The server calls Businessflow with the username and password stored for that watch.

```
watch  --HTTPS /transcribe and /execute-->  voice-assistant-server :8080
                                              |-- Businessflow API
                                              |-- Pocket TTS :8090 (optional, spoken replies)
shop PC                                       |-- Cloudflare tunnel (optional, when the watch is not on the LAN)
```

The phoneshop API paths in this tree are the real Businessflow endpoints. Sample tickets and staff notes in the repo are filler. The values below are not filler. Replace each one before you run a watch against a real shop.

## What you need

- Windows 10/11. Linux can run the server. Opening printed PDFs is Windows-only.
- Python 3.10 and ffmpeg on `PATH` (the server converts WAV for the watch).
- ESP-IDF 5.5.x if you build the firmware. A prebuilt app image is `firmware/dist/watch_assistant.bin` in the firmware repo. Prefer `idf.py flash` so the bootloader and partition table match.
- A Businessflow login for the shop.
- Optional: [cloudflared](https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/downloads/) when the watch is away from the shop LAN.
- Optional: Pocket TTS, in the firmware repo under `tts/`, on `127.0.0.1:8090`.

## Run the server

From a clone of voice-assistant-server:

```powershell
python -m venv venv
.\venv\Scripts\pip install -r requirements.txt
copy config\env.example config\.env
copy config\devices.example.yaml config\devices.yaml
notepad config\.env
notepad config\devices.yaml
.\venv\Scripts\python.exe src\server.py
```

Health check: http://127.0.0.1:8080/health  
Admin page: http://127.0.0.1:8080/admin

`config/.env` and `config/devices.yaml` are gitignored. Leave the real values only on the shop PC.

`config/config.yaml` `phoneshop.base_url` is `https://YOUR_PHONESHOP_API_HOST`. Write your shop API origin there. Every `YOUR_SHOP_API_PATH` in the server is a shop API path with the real path removed. In the function or config entry where it sits, write the path your shop uses for that action.

## Run the watch

Hardware: ESP32-S3-N16R8, SH8601 368×448 QSPI AMOLED, ES8311, AXP2101, QMI8658, PCF85063, TCA9554, microSD.

From a clone of watch-assistant-firmware, with ESP-IDF exported:

```powershell
cd firmware
idf.py -p YOUR_SERIAL_PORT flash
```

`YOUR_SERIAL_PORT` is the watch's COM port in Device Manager, such as `COM8`.

On Windows with ESP-IDF 5.5.1 in the default Espressif folder:

```powershell
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.5_py3.10_env"
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5.1"
cmd /c "C:\Espressif\frameworks\esp-idf-v5.5.1\export.bat && cd /d YOUR_FIRMWARE_DIR\firmware && idf.py -p YOUR_SERIAL_PORT flash"
```

`YOUR_FIRMWARE_DIR` is the folder where you cloned watch-assistant-firmware. Change the Espressif paths if IDF is installed somewhere else.

Host tests, no hardware:

```powershell
python firmware\tests\test_host.py
```

First boot: hold the bottom (power) button until **Setup** appears. Join the Wi-Fi network `ESP Assistant`, open http://192.168.4.1, and on the Config tab set:

- Shop Wi-Fi name and password (the network the watch uses in the shop).
- Server URL: `https://your-server.example.com` with no path on the end. The firmware adds `/transcribe` and `/execute`.
- Device secret: the same 64 hex characters you put in `devices.yaml` for this watch. The MAC on that setup page is the `device_id`.

A public HTTPS name does not need a custom root CA. Paste a PEM only for a private certificate.

Buttons: top hold talks, top tap confirms or sends the oldest queued clip, top triple-click opens settings. Bottom tap sleeps or cancels. Bottom hold for 3 seconds, then confirm, powers off.

## Reach the server from outside the shop

1. Install cloudflared and create a tunnel: `cloudflared tunnel create YOUR_TUNNEL_NAME`
2. `cloudflared tunnel list` prints the tunnel UUID. That UUID is `YOUR_CLOUDFLARE_TUNNEL_ID`.
3. Point the tunnel's public hostname at `http://127.0.0.1:8080`.
4. Run it with `cloudflared tunnel run YOUR_TUNNEL_NAME`
5. Provision the watch with that public `https://` origin.

`start_server.bat` in the server repo starts the Flask app and `cloudflared tunnel run YOUR_TUNNEL_NAME`. Edit the tunnel name in that file first.

The firmware repo can start all three shop processes. Edit the paths at the top of `start-shop.bat` and `watch-shop-stack.ps1`, then run `watch-shop-stack.bat`. The watchdog keeps these up:

1. This server on port 8080
2. Pocket TTS on port 8090
3. The Cloudflare tunnel

Pocket TTS, from the firmware clone:

```powershell
cd tts
.\install-tts.ps1
.\start-tts.bat
```

OpenAI or xAI keys stay out of git. Put the key alone in `secrets\openai.key` or `secrets\xai.key` (see `secrets/INSTRUCTIONS.txt`), or set `OPENAI_API_KEY` in the server's `config/.env`.

## Placeholders

Replace every token below. The same secret and the same MAC must be on the watch and in `config/devices.yaml`.

| Placeholder | Where | What to write |
|---|---|---|
| `YOUR_PHONESHOP_API_HOST` | `config/config.yaml` `phoneshop.base_url`, and `BASE_URL` in `src/phoneshop_api.py`. | The shop API origin, with `https://` and no path. |
| `YOUR_SHOP_API_PATH` | Shop calls in `src/phoneshop_api.py`, `src/server.py`, `config/commands.yaml`, `config/command_schema.yaml`, and `llm_api_schema.json`. | The path for that action on your shop API. The same token is used in every spot, so set each one for the action in that function. |
| `YOUR_PHONESHOP_USERNAME` | `config/devices.yaml` `phoneshop_username`. Also `PHONESHOP_USERNAME` in `config/.env` if you set it there. | The shop login username for this watch. |
| `YOUR_PHONESHOP_PASSWORD` | `config/devices.yaml` `phoneshop_password`. Also `PHONESHOP_PASSWORD` in `config/.env` if you set it there. | That Businessflow account's password. |
| `AA:BB:CC:DD:EE:FF` | The device key and `device_id` in `config/devices.yaml`. A second example device uses `AA:BB:CC:DD:EE:01`. | That watch's Wi-Fi MAC, uppercase, with colons, from the setup page at http://192.168.4.1. One block per watch. |
| `0000000000000000000000000000000000000000000000000000000000000000` | `secret` in `devices.yaml`, and the default in `firmware/components/settings/settings.c`. Also the Device secret field on the setup page. | A new 64-character hex secret, different for each watch. Generate one with `python -c "import secrets; print(secrets.token_hex(32))"`. Put the same value in `devices.yaml` and on that watch. The server and the watch both treat it as hex. Do not leave the zeros. |
| `your-server.example.com` | Default server URL in `settings.c`, tunnel health checks, and the docs. | The public hostname of this server, no path. Use the Cloudflare hostname, or `http://192.168.x.x:8080` when the watch and the PC are on the same LAN. |
| `YOUR_CLOUDFLARE_TUNNEL_ID` | The tunnel UUID in the cloudflared config example. Credentials file: `%USERPROFILE%\.cloudflared\YOUR_CLOUDFLARE_TUNNEL_ID.json` | The UUID from `cloudflared tunnel list`. |
| `YOUR_TUNNEL_NAME` | `cloudflared tunnel run`, `start_server.bat`, `start-shop.bat`, `watch-shop-stack.ps1`. | The name you passed to `cloudflared tunnel create`. |
| `YOUR_VOICE_ASSISTANT_SERVER_DIR` | `start-shop.bat`, `watch-shop-stack.ps1`, `push-to-github.ps1`. | Full path of your voice-assistant-server clone. |
| `YOUR_FIRMWARE_DIR` | Those same scripts, and the ESP-IDF flash example. | Full path of your watch-assistant-firmware clone. |
| `YOUR_SERIAL_PORT` | `idf.py -p` | The watch COM port, such as `COM8`. |
| `YOUR_WINDOWS_USER` | Example path `C:\Users\YOUR_WINDOWS_USER\.cloudflared\` | Your Windows account name. `%USERPROFILE%\.cloudflared` is the same folder. |
| `sk-your-key-here` | `OPENAI_API_KEY` in `config/.env` | Your OpenAI API key. An xAI key, if you use one, is `XAI_API_KEY` and starts with `xai-`. |
| `change-this-admin-secret` | `SECRET_KEY` in `config/.env` | A long random string for the admin session cookie. |
| `admin123` | `ADMIN_PASSWORD` in `config/.env` | The password for http://127.0.0.1:8080/admin. Change it. |
| `your-secret-token-here` | `SERVER_AUTH_TOKEN` in `config/.env.example` | Only if you still use that older token. Watch auth is the HMAC secret above, not this token. |

No OpenAI API key is stored in this repo. Put yours only in `config/.env` as `OPENAI_API_KEY`. That file is gitignored.

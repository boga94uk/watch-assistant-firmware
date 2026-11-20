@echo off
setlocal enabledelayedexpansion

:: ============================================
:: ESP32-S3 Complete Firmware Flasher
:: ============================================
:: This script flashes a complete PlatformIO build to ESP32-S3:
:: - Bootloader (0x0)
:: - Partition table (0x8000)
:: - Application firmware (0x10000)
:: - SPIFFS filesystem with audio files (0x610000)
::
:: Self-contained: Downloads all dependencies automatically
:: ============================================

echo.
echo ============================================
echo ESP32-S3 COMPLETE FIRMWARE FLASHER
echo ============================================
echo.

:: Set script directory
set "SCRIPT_DIR=%~dp0"
cd /d "%SCRIPT_DIR%"

:: Check if .pio build directory exists
if not exist ".pio\build\esp32-s3-n16r8" (
    echo [ERROR] Build directory not found!
    echo Please run "pio run" to build the project first, or ensure you have:
    echo - .pio\build\esp32-s3-n16r8\bootloader.bin
    echo - .pio\build\esp32-s3-n16r8\partitions.bin
    echo - .pio\build\esp32-s3-n16r8\firmware.bin
    echo - .pio\build\esp32-s3-n16r8\spiffs.bin
    echo.
    pause
    exit /b 1
)

:: Check for required binaries
set "BOOTLOADER_BIN=.pio\build\esp32-s3-n16r8\bootloader.bin"
set "PARTITIONS_BIN=.pio\build\esp32-s3-n16r8\partitions.bin"
set "FIRMWARE_BIN=.pio\build\esp32-s3-n16r8\firmware.bin"
set "SPIFFS_BIN=.pio\build\esp32-s3-n16r8\spiffs.bin"

echo [INFO] Checking for required binaries...
if not exist "%BOOTLOADER_BIN%" (
    echo [ERROR] Bootloader not found: %BOOTLOADER_BIN%
    echo Run "pio run" to build the project first.
    pause
    exit /b 1
)
if not exist "%PARTITIONS_BIN%" (
    echo [ERROR] Partition table not found: %PARTITIONS_BIN%
    echo Run "pio run" to build the project first.
    pause
    exit /b 1
)
if not exist "%FIRMWARE_BIN%" (
    echo [ERROR] Firmware not found: %FIRMWARE_BIN%
    echo Run "pio run" to build the project first.
    pause
    exit /b 1
)

echo [OK] Bootloader: %BOOTLOADER_BIN%
echo [OK] Partitions: %PARTITIONS_BIN%
echo [OK] Firmware:   %FIRMWARE_BIN%

:: Check if SPIFFS image exists, if not try to build it
if not exist "%SPIFFS_BIN%" (
    echo [WARN] SPIFFS image not found: %SPIFFS_BIN%
    echo [INFO] Attempting to build SPIFFS image...
    
    if exist "data" (
        echo [INFO] Found data folder, building SPIFFS image...
        call pio run --target buildfs
        if errorlevel 1 (
            echo [ERROR] Failed to build SPIFFS image!
            echo You can continue without SPIFFS ^(audio files won't be flashed^).
            set /p "CONTINUE=Continue anyway? (y/N): "
            if /i not "!CONTINUE!"=="y" exit /b 1
            set "SKIP_SPIFFS=1"
        ) else (
            echo [OK] SPIFFS:    %SPIFFS_BIN%
        )
    ) else (
        echo [WARN] No data folder found. Skipping SPIFFS flash.
        set "SKIP_SPIFFS=1"
    )
) else (
    echo [OK] SPIFFS:    %SPIFFS_BIN%
)

echo.
echo ============================================
echo CHECKING PYTHON AND DEPENDENCIES
echo ============================================

:: Check if Python is installed
where python >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python not found in PATH!
    echo.
    echo Please install Python 3.7+ from https://www.python.org/downloads/
    echo Make sure to check "Add Python to PATH" during installation.
    echo.
    pause
    exit /b 1
)

:: Get Python version
for /f "tokens=2" %%i in ('python --version 2^>^&1') do set PYTHON_VERSION=%%i
echo [OK] Found Python %PYTHON_VERSION%

:: Check/install esptool
echo [INFO] Checking for esptool...
python -c "import esptool" >nul 2>&1
if errorlevel 1 (
    echo [INFO] Installing esptool...
    python -m pip install --upgrade esptool
    if errorlevel 1 (
        echo [ERROR] Failed to install esptool!
        pause
        exit /b 1
    )
    echo [OK] esptool installed successfully
) else (
    echo [OK] esptool is already installed
)

echo.
echo ============================================
echo DETECTING ESP32-S3 DEVICE
echo ============================================

:: Try to detect COM port
echo [INFO] Scanning for ESP32-S3 device...
set "COM_PORT="

:: Try platformio.ini port first
if exist "platformio.ini" (
    for /f "tokens=2 delims== " %%a in ('findstr /c:"monitor_port" platformio.ini 2^>nul') do (
        set "COM_PORT=%%a"
    )
)

:: If found in platformio.ini
if defined COM_PORT (
    echo [INFO] Found port in platformio.ini: !COM_PORT!
    
    :: Verify it exists
    mode !COM_PORT! >nul 2>&1
    if errorlevel 1 (
        echo [WARN] Port !COM_PORT! is not available
        set "COM_PORT="
    )
)

:: If still not found, try to auto-detect
if not defined COM_PORT (
    echo [INFO] Auto-detecting COM port...
    for /f "tokens=*" %%a in ('python -m esptool chip_id 2^>^&1 ^| findstr /c:"Serial port"') do (
        for /f "tokens=3" %%b in ("%%a") do set "COM_PORT=%%b"
    )
)

:: If still not found, ask user
if not defined COM_PORT (
    echo.
    echo [WARN] Could not auto-detect COM port.
    echo.
    echo Please check Device Manager for your ESP32-S3 device.
    echo It should appear as "USB Serial Device" or "USB-SERIAL CH340".
    echo.
    set /p "COM_PORT=Enter COM port (e.g., COM3): "
    
    if "!COM_PORT!"=="" (
        echo [ERROR] No COM port specified!
        pause
        exit /b 1
    )
)

echo.
echo [INFO] Using COM port: !COM_PORT!

:: Verify device connection
echo [INFO] Verifying device connection...
python -m esptool --port !COM_PORT! chip_id >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Cannot communicate with device on !COM_PORT!
    echo.
    echo Troubleshooting:
    echo 1. Make sure the device is connected via USB
    echo 2. Check Device Manager for the correct COM port
    echo 3. Try pressing and holding BOOT button while connecting
    echo 4. Install CP210x or CH340 USB driver if needed
    echo.
    pause
    exit /b 1
)

echo [OK] Device detected on !COM_PORT!

echo.
echo ============================================
echo READY TO FLASH
echo ============================================
echo.
echo The following will be flashed:
echo   Bootloader:      0x0000   (%BOOTLOADER_BIN%)
echo   Partition Table: 0x8000   (%PARTITIONS_BIN%)
echo   Application:     0x10000  (%FIRMWARE_BIN%)
if not defined SKIP_SPIFFS (
    echo   SPIFFS Data:     0x610000 (%SPIFFS_BIN%)
)
echo.
echo Target: ESP32-S3 on !COM_PORT!
echo.
echo NOTE: NVS partition (WiFi/server settings) will be PRESERVED
echo       Only bootloader, partitions, firmware, and SPIFFS will be updated
echo.
set /p "CONFIRM=Continue with flash? (Y/n): "
if /i "!CONFIRM!"=="n" (
    echo [INFO] Flash cancelled by user.
    pause
    exit /b 0
)

echo.
echo ============================================
echo FLASHING FIRMWARE
echo ============================================
echo.

:: Build esptool command (updated syntax for esptool v5+)
set "FLASH_CMD=python -m esptool"
set "FLASH_CMD=!FLASH_CMD! --chip esp32s3"
set "FLASH_CMD=!FLASH_CMD! --port !COM_PORT!"
set "FLASH_CMD=!FLASH_CMD! --baud 921600"
set "FLASH_CMD=!FLASH_CMD! --before default-reset"
set "FLASH_CMD=!FLASH_CMD! --after hard-reset"
set "FLASH_CMD=!FLASH_CMD! write-flash"
set "FLASH_CMD=!FLASH_CMD! -z"
set "FLASH_CMD=!FLASH_CMD! --flash-mode dio"
set "FLASH_CMD=!FLASH_CMD! --flash-freq 80m"
set "FLASH_CMD=!FLASH_CMD! --flash-size 16MB"
set "FLASH_CMD=!FLASH_CMD! 0x0 %BOOTLOADER_BIN%"
set "FLASH_CMD=!FLASH_CMD! 0x8000 %PARTITIONS_BIN%"
set "FLASH_CMD=!FLASH_CMD! 0x10000 %FIRMWARE_BIN%"
if not defined SKIP_SPIFFS (
    set "FLASH_CMD=!FLASH_CMD! 0x610000 %SPIFFS_BIN%"
)

echo [INFO] Flashing...
echo.

:: Execute flash
!FLASH_CMD!

if errorlevel 1 (
    echo.
    echo [ERROR] Flash failed!
    echo.
    echo Troubleshooting:
    echo 1. Hold BOOT button and press RESET, then try again
    echo 2. Try a different USB cable
    echo 3. Check if another program is using the COM port
    echo 4. Try reducing baud rate ^(edit this script, change 921600 to 115200^)
    echo.
    pause
    exit /b 1
)

echo.
echo ============================================
echo FLASH COMPLETE!
echo ============================================
echo.
echo Your ESP32-S3 has been programmed with:
echo   - Bootloader
echo   - Partition table
echo   - Application firmware
if not defined SKIP_SPIFFS (
    echo   - SPIFFS filesystem ^(audio files^)
)
echo.
echo The device should now reboot and start running.
echo.
echo To view serial output, use: pio device monitor
echo Or any serial terminal at 115200 baud.
echo.
pause

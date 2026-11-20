@echo off
setlocal enabledelayedexpansion

echo ============================================
echo ESP32-S3 Project Export Tool
echo ============================================
echo.

set OUTPUT_FILE=export.txt
set PROJECT_ROOT=%~dp0

:: Clear previous export
if exist "%OUTPUT_FILE%" del "%OUTPUT_FILE%"

:: Header
echo ============================================ >> "%OUTPUT_FILE%"
echo ESP32-S3 LVGL Native PlatformIO Project Export >> "%OUTPUT_FILE%"
echo Generated: %DATE% %TIME% >> "%OUTPUT_FILE%"
echo ============================================ >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"

:: Project Structure
echo [PROJECT STRUCTURE] >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"
tree /F /A >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"

:: Core Configuration Files
echo ============================================ >> "%OUTPUT_FILE%"
echo [platformio.ini] >> "%OUTPUT_FILE%"
echo ============================================ >> "%OUTPUT_FILE%"
type platformio.ini >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"

echo ============================================ >> "%OUTPUT_FILE%"
echo [sdkconfig.defaults] >> "%OUTPUT_FILE%"
echo ============================================ >> "%OUTPUT_FILE%"
type sdkconfig.defaults >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"

echo ============================================ >> "%OUTPUT_FILE%"
echo [CMakeLists.txt] >> "%OUTPUT_FILE%"
echo ============================================ >> "%OUTPUT_FILE%"
type CMakeLists.txt >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"

echo ============================================ >> "%OUTPUT_FILE%"
echo [custom_partitions.csv] >> "%OUTPUT_FILE%"
echo ============================================ >> "%OUTPUT_FILE%"
type custom_partitions.csv >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"

:: Source Files
echo ============================================ >> "%OUTPUT_FILE%"
echo [SOURCE FILES] >> "%OUTPUT_FILE%"
echo ============================================ >> "%OUTPUT_FILE%"
echo. >> "%OUTPUT_FILE%"

for /R src %%f in (*.cpp *.c *.h *.hpp) do (
    echo ============================================ >> "%OUTPUT_FILE%"
    echo [%%~nxf - %%f] >> "%OUTPUT_FILE%"
    echo ============================================ >> "%OUTPUT_FILE%"
    type "%%f" >> "%OUTPUT_FILE%"
    echo. >> "%OUTPUT_FILE%"
    echo. >> "%OUTPUT_FILE%"
)

:: Include Files
if exist "include" (
    echo ============================================ >> "%OUTPUT_FILE%"
    echo [INCLUDE FILES] >> "%OUTPUT_FILE%"
    echo ============================================ >> "%OUTPUT_FILE%"
    echo. >> "%OUTPUT_FILE%"
    
    for /R include %%f in (*.h *.hpp) do (
        echo ============================================ >> "%OUTPUT_FILE%"
        echo [%%~nxf - %%f] >> "%OUTPUT_FILE%"
        echo ============================================ >> "%OUTPUT_FILE%"
        type "%%f" >> "%OUTPUT_FILE%"
        echo. >> "%OUTPUT_FILE%"
        echo. >> "%OUTPUT_FILE%"
    )
)

:: Generated sdkconfig (if exists)
if exist "sdkconfig.esp32-s3-n16r8" (
    echo ============================================ >> "%OUTPUT_FILE%"
    echo [sdkconfig.esp32-s3-n16r8- GENERATED CONFIG] >> "%OUTPUT_FILE%"
    echo ============================================ >> "%OUTPUT_FILE%"
    type sdkconfig.esp32-s3-n16r8 >> "%OUTPUT_FILE%"
    echo. >> "%OUTPUT_FILE%"
    echo. >> "%OUTPUT_FILE%"
)

echo.
echo ============================================
echo Export Complete!
echo Output: %OUTPUT_FILE%
echo.
echo File size:
for %%A in ("%OUTPUT_FILE%") do echo %%~zA bytes
echo ============================================
echo.
pause

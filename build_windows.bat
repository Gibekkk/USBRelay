@echo off
setlocal enabledelayedexpansion

REM =================================================================
REM Build USB Relay Auto-Control untuk Windows (GUI-only) -> .exe.
REM
REM TIDAK PAKAI "make" SAMA SEKALI -- cukup g++ (dari MSYS2 MinGW64),
REM karena tidak semua instalasi Windows/MSYS2 punya "make" terpasang.
REM
REM CARA PAKAI:
REM   1) Install MSYS2 dari https://www.msys2.org/
REM   2) Buka terminal "MSYS2 MinGW64" (BUKAN "MSYS2 MSYS"), lalu:
REM        pacman -Syu
REM        pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf ^
REM                            mingw-w64-x86_64-hidapi mingw-w64-x86_64-gtk3
REM   3) Jalankan build_windows.bat ini dari Command Prompt / double-click.
REM      (Script otomatis menambahkan C:\msys64\mingw64\bin ke PATH kalau
REM      belum ada -- ubah MSYS2_ROOT di bawah kalau lokasi instalasi beda.)
REM =================================================================

set MSYS2_ROOT=C:\msys64
if not "%MSYS2_MINGW64_BIN%"=="" set MSYS2_ROOT=

set MINGW_BIN=%MSYS2_ROOT%\mingw64\bin
if not "%MSYS2_MINGW64_BIN%"=="" set MINGW_BIN=%MSYS2_MINGW64_BIN%

REM Paksa pkg-config (siapa pun yang aktif di PATH -- termasuk pkg-config
REM bawaan Conda/Anaconda yang tidak tahu-menahu soal MSYS2) supaya tetap
REM mencari file .pc di folder MSYS2 mingw64.
set "PKG_CONFIG_PATH=%MINGW_BIN%\..\lib\pkgconfig;%MINGW_BIN%\..\share\pkgconfig;%PKG_CONFIG_PATH%"

REM Pakai pkg-config.exe milik MSYS2 secara eksplisit kalau ada, daripada
REM mengandalkan urutan PATH.
set PKGCONFIG=pkg-config
if exist "%MINGW_BIN%\pkg-config.exe" set "PKGCONFIG=%MINGW_BIN%\pkg-config.exe"

where g++ >nul 2>nul
if errorlevel 1 (
    echo [*] g++ belum ada di PATH, coba tambahkan dari %MINGW_BIN% ...
    REM Pakai !PATH! (delayed expansion), BUKAN %PATH% -- PATH bawaan Windows
    REM hampir selalu berisi tanda kurung (mis. "Program Files (x86)",
    REM "Common Files"), dan %PATH% di dalam blok if(...) bikin parser cmd.exe
    REM salah baca kurung itu sebagai penutup blok -> error
    REM "...\Common was unexpected at this time."
    set "PATH=%MINGW_BIN%;!PATH!"
)

where g++ >nul 2>nul
if errorlevel 1 (
    echo [ERROR] g++ tetap tidak ditemukan.
    echo         Install MSYS2 dari https://www.msys2.org/ lalu jalankan:
    echo           pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf mingw-w64-x86_64-hidapi mingw-w64-x86_64-gtk3
    echo         Kalau MSYS2 diinstall bukan di C:\msys64, set env var MSYS2_MINGW64_BIN
    echo         ke folder mingw64\bin sebelum menjalankan script ini.
    exit /b 1
)

echo [*] Memakai g++ dari:
where g++

set CXXFLAGS=-std=c++17 -Wall -Wextra -O2
set SRCDIR=src
set DISTDIR=dist

REM Config (config\device_map.conf, config\channels.conf) TIDAK lagi
REM disalin ke dist\ -- usbrelay-gui.exe membacanya langsung dari
REM ..\config\ (relatif dist\, folder config\ di root project).
REM data.csv juga TIDAK disalin -- sudah permanen ada di dist\data.csv.
if not exist %DISTDIR% mkdir %DISTDIR%

set GUI_SRCS=%SRCDIR%\relay_controller.cpp %SRCDIR%\usb_monitor.cpp %SRCDIR%\device_mapper.cpp %SRCDIR%\app_core.cpp %SRCDIR%\gui_ui.cpp %SRCDIR%\main.cpp

echo [*] pkg-config yang dipakai: %PKGCONFIG%
echo [*] PKG_CONFIG_PATH: %PKG_CONFIG_PATH%

REM --- Cari flag hidapi lewat pkg-config (nama package: hidapi) ---
set HIDAPI_CFLAGS=
set HIDAPI_LIBS=-lhidapi -lsetupapi -lhid -lcfgmgr32
for /f "delims=" %%i in ('"%PKGCONFIG%" --cflags hidapi 2^>nul') do set HIDAPI_CFLAGS=%%i
for /f "delims=" %%i in ('"%PKGCONFIG%" --libs hidapi 2^>nul') do set HIDAPI_LIBS=%%i -lsetupapi -lhid -lcfgmgr32

if "%HIDAPI_CFLAGS%"=="" (
    echo [WARNING] pkg-config tidak menemukan paket "hidapi".
    echo           Pastikan sudah diinstall di MSYS2 MinGW64:
    echo             pacman -S --needed mingw-w64-x86_64-hidapi
    echo           Lanjut coba compile pakai default include path...
)

REM --- Cari flag GTK3 lewat pkg-config (wajib, karena sekarang GUI-only) ---
set GTK_CFLAGS=
set GTK_LIBS=
for /f "delims=" %%i in ('"%PKGCONFIG%" --cflags gtk+-3.0 2^>nul') do set GTK_CFLAGS=%%i
for /f "delims=" %%i in ('"%PKGCONFIG%" --libs gtk+-3.0 2^>nul') do set GTK_LIBS=%%i

if "%GTK_LIBS%"=="" (
    echo [ERROR] pkg-config tidak menemukan paket "gtk+-3.0".
    echo         Install dulu di MSYS2 MinGW64:
    echo           pacman -S --needed mingw-w64-x86_64-gtk3
    exit /b 1
)

REM usb_monitor.cpp di Windows pakai SetupAPI (bawaan Windows SDK / MinGW,
REM tidak perlu package tambahan) -> tinggal -lsetupapi (sudah di HIDAPI_LIBS)

echo.
echo [1/1] Build GUI (GTK3) -^> %DISTDIR%\usbrelay-gui.exe
g++ %CXXFLAGS% %HIDAPI_CFLAGS% %GTK_CFLAGS% -DUSE_GUI -mwindows -o %DISTDIR%\usbrelay-gui.exe %GUI_SRCS% %HIDAPI_LIBS% %GTK_LIBS%
if errorlevel 1 (
    echo [ERROR] Build GUI gagal.
    exit /b 1
)
echo       -^> %DISTDIR%\usbrelay-gui.exe selesai.

echo.
echo [OK] Build selesai. File ada di folder %DISTDIR%\ (exe + data.csv).
echo      Config dibaca dari ..\config\ (relatif dist\, folder config\ di root project).
echo      Jalankan dobel-klik %DISTDIR%\usbrelay-gui.exe

endlocal

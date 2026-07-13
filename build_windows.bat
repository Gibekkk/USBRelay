@echo off
setlocal enabledelayedexpansion

REM =================================================================
REM Build USB Relay Auto-Control untuk Windows -> langsung jadi .exe.
REM
REM TIDAK PAKAI "make" SAMA SEKALI -- cukup g++ (dari MSYS2 MinGW64),
REM karena tidak semua instalasi Windows/MSYS2 punya "make" terpasang.
REM Script ini memanggil g++ langsung, persis seperti isi Makefile di
REM Linux/macOS, tapi ditulis ulang sebagai batch murni.
REM
REM CARA PAKAI:
REM   1) Install MSYS2 dari https://www.msys2.org/
REM   2) Buka terminal "MSYS2 MinGW64" (BUKAN "MSYS2 MSYS"), lalu:
REM        pacman -Syu
REM        pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf ^
REM                            mingw-w64-x86_64-hidapi mingw-w64-x86_64-pdcurses ^
REM                            mingw-w64-x86_64-gtk3
REM   3) Jalankan build_windows.bat ini dari Command Prompt / double-click.
REM      (Script otomatis menambahkan C:\msys64\mingw64\bin ke PATH kalau
REM      belum ada -- ubah MSYS2_ROOT di bawah kalau lokasi instalasi beda.)
REM =================================================================

set MSYS2_ROOT=C:\msys64
if not "%MSYS2_MINGW64_BIN%"=="" set MSYS2_ROOT=

set MINGW_BIN=%MSYS2_ROOT%\mingw64\bin
if not "%MSYS2_MINGW64_BIN%"=="" set MINGW_BIN=%MSYS2_MINGW64_BIN%

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
    echo           pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf mingw-w64-x86_64-hidapi mingw-w64-x86_64-pdcurses mingw-w64-x86_64-gtk3
    echo         Kalau MSYS2 diinstall bukan di C:\msys64, set env var MSYS2_MINGW64_BIN
    echo         ke folder mingw64\bin sebelum menjalankan script ini.
    exit /b 1
)

echo [*] Memakai g++ dari:
where g++

set CXXFLAGS=-std=c++17 -Wall -Wextra -O2
set SRCDIR=src
set DISTDIR=dist

if not exist %DISTDIR% mkdir %DISTDIR%
xcopy /Y /I /E /Q config %DISTDIR%\config >nul

set COMMON_SRCS=%SRCDIR%\relay_controller.cpp %SRCDIR%\usb_monitor.cpp %SRCDIR%\device_mapper.cpp %SRCDIR%\app_core.cpp
set CLI_SRCS=%COMMON_SRCS% %SRCDIR%\cli_ui.cpp %SRCDIR%\main.cpp
set GUI_SRCS=%COMMON_SRCS% %SRCDIR%\gui_ui.cpp %SRCDIR%\main.cpp

REM --- Cari flag hidapi lewat pkg-config (nama package: hidapi) ---
set HIDAPI_CFLAGS=
set HIDAPI_LIBS=-lhidapi -lsetupapi -lhid -lcfgmgr32
for /f "delims=" %%i in ('pkg-config --cflags hidapi 2^>nul') do set HIDAPI_CFLAGS=%%i
for /f "delims=" %%i in ('pkg-config --libs hidapi 2^>nul') do set HIDAPI_LIBS=%%i -lsetupapi -lhid -lcfgmgr32

REM --- Cari flag GTK3 lewat pkg-config ---
set GTK_CFLAGS=
set GTK_LIBS=
for /f "delims=" %%i in ('pkg-config --cflags gtk+-3.0 2^>nul') do set GTK_CFLAGS=%%i
for /f "delims=" %%i in ('pkg-config --libs gtk+-3.0 2^>nul') do set GTK_LIBS=%%i

REM --- PDCurses: TIDAK ada file pkg-config untuk paket ini di MSYS2, dan
REM header-nya terpasang di mingw64\include\pdcurses\curses.h (BUKAN
REM langsung di mingw64\include\curses.h). Tanpa -I ini, build CLI gagal
REM dengan error "curses.h: No such file or directory" -- ini penyebab
REM utama build Windows sebelumnya tidak jalan.
set PDCURSES_CFLAGS=-I"%MINGW_BIN%\..\include\pdcurses"

REM usb_monitor.cpp di Windows pakai SetupAPI (bawaan Windows SDK / MinGW,
REM tidak perlu package tambahan) -> tinggal -lsetupapi (sudah di HIDAPI_LIBS)

echo.
echo [1/2] Build CLI (ncurses via PDCurses) -^> %DISTDIR%\usbrelay-cli.exe
g++ %CXXFLAGS% %HIDAPI_CFLAGS% %PDCURSES_CFLAGS% -o %DISTDIR%\usbrelay-cli.exe %CLI_SRCS% %HIDAPI_LIBS% -lpdcurses
if errorlevel 1 (
    echo [ERROR] Build CLI gagal.
    exit /b 1
)
echo       -^> %DISTDIR%\usbrelay-cli.exe selesai.

echo.
if "%GTK_LIBS%"=="" goto :skip_gui

echo [2/2] Build GUI ^(GTK3^) -^> %DISTDIR%\usbrelay-gui.exe
g++ %CXXFLAGS% %HIDAPI_CFLAGS% %GTK_CFLAGS% -DUSE_GUI -mwindows -o %DISTDIR%\usbrelay-gui.exe %GUI_SRCS% %HIDAPI_LIBS% %GTK_LIBS%
if errorlevel 1 (
    echo [ERROR] Build GUI gagal.
    exit /b 1
)
echo       -^> %DISTDIR%\usbrelay-gui.exe selesai.
goto :after_gui

:skip_gui
echo [SKIP] GTK3 tidak ditemukan lewat pkg-config, build GUI dilewati.
echo        Install dengan: pacman -S mingw-w64-x86_64-gtk3

:after_gui
echo.
echo [OK] Build selesai. File ada di folder %DISTDIR%\ (exe + config\).
echo      Jalankan: %DISTDIR%\usbrelay-cli.exe --help
endlocal
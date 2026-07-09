# USB Relay Auto-Control

Aplikasi C++ untuk kontrol USB relay secara otomatis berdasarkan deteksi USB device.
Tersedia mode CLI (ncurses/PDCurses) dan GUI (GTK3).

Sudah bisa dibangun (build) untuk **Linux**, **macOS**, dan **Windows**. Kode
inti (relay_controller, device_mapper, app_core) sama persis di ketiga OS;
yang beda hanya lapisan deteksi USB (`usb_monitor.cpp`, otomatis pilih
implementasi lewat `#if defined(...)` -- libudev di Linux, IOKit di macOS,
SetupAPI di Windows) dan build system.

## Cara Kerja

```
USB Device dipasang
      │
      ▼
USBMonitor mendeteksi event   (Linux: libudev · macOS: IOKit · Windows: SetupAPI)
      │
      ▼
DeviceMapper mencocokkan VID:PID dengan aturan di config
      │
      ▼
RelayController (hidapi) mengeksekusi perintah ke relay
      │
      ▼
UI (CLI/GUI) update tampilan status
```

---

## Build di Linux

```bash
sudo apt-get install -y \
    libhidapi-dev libhidapi-hidraw0 \
    libudev-dev \
    libncurses-dev \
    libgtk-3-dev \
    pkg-config build-essential
# atau: make deps

make all      # Build keduanya (CLI + GUI)
make cli      # Hanya CLI
make gui      # Hanya GUI
```

### Pasang udev Rule (agar tidak perlu sudo)

```bash
make install-udev
# Lalu cabut dan pasang kembali relay USB
```

---

## Build di macOS

Pakai `Makefile.macos` (bukan `Makefile` biasa), karena path library dan
framework yang dipakai beda dari Linux (IOKit, bukan libudev).

```bash
# 1) Install Homebrew kalau belum ada: https://brew.sh
# 2) Install dependency:
make -f Makefile.macos deps
#    (ini setara: brew install hidapi gtk+3 pkg-config)

# 3) Build
make -f Makefile.macos all      # CLI + GUI
make -f Makefile.macos cli      # Hanya CLI
make -f Makefile.macos gui      # Hanya GUI
```

Kalau mau bisa ketik `make` polos, symlink dulu:
```bash
ln -sf Makefile.macos Makefile
make all
```

**Catatan macOS:** hidapi di Mac mengakses HID device lewat IOKit, jadi saat
`usbrelay-cli`/`usbrelay-gui` pertama kali dijalankan, macOS bisa minta izin
**Input Monitoring** (System Settings → Privacy & Security → Input
Monitoring). Aktifkan izinnya lalu jalankan ulang programnya.

---

## Build di Windows

**Tidak butuh `make` sama sekali** -- cukup `g++` dari MSYS2. Ini sengaja
supaya tidak error seperti kalau pakai Makefile langsung di Windows (banyak
instalasi Windows tidak punya `make`, dan path library GTK/hidapi/ncurses
juga beda formatnya dari Unix).

1. Install [MSYS2](https://www.msys2.org/).
2. Buka terminal **"MSYS2 MinGW64"** (bukan "MSYS2 MSYS" / "MSYS2 UCRT64"),
   lalu jalankan:
   ```bash
   pacman -Syu
   # tutup & buka lagi terminal kalau diminta, lalu:
   pacman -S --needed \
       mingw-w64-x86_64-gcc \
       mingw-w64-x86_64-pkgconf \
       mingw-w64-x86_64-hidapi \
       mingw-w64-x86_64-pdcurses \
       mingw-w64-x86_64-gtk3
   ```
3. Dari Command Prompt / File Explorer (boleh di luar MSYS2), jalankan
   `build_windows.bat` di folder project ini (double-click atau
   `build_windows.bat` di cmd).
   - Script otomatis mendeteksi `g++` dari `C:\msys64\mingw64\bin`.
   - Kalau MSYS2 diinstall bukan di `C:\msys64`, set environment variable
     `MSYS2_MINGW64_BIN` ke folder `...\mingw64\bin` sebelum menjalankan.
4. Hasil build: `usbrelay-cli.exe` dan `usbrelay-gui.exe` langsung muncul di
   folder yang sama -- tinggal dijalankan / didistribusikan.

**Catatan Windows:**
- Kalau `usbrelay-gui.exe` dijalankan lewat double-click dan gagal start
  karena "DLL tidak ditemukan", jalankan dari dalam terminal MSYS2 MinGW64
  (`./usbrelay-gui.exe`) supaya semua DLL GTK/hidapi ketemu lewat PATH, atau
  copy DLL yang relevan dari `C:\msys64\mingw64\bin` ke folder yang sama
  dengan .exe sebelum didistribusikan ke komputer lain.
- Tidak perlu udev rule (itu khusus Linux) -- device HID pada Windows sudah
  bisa diakses lewat hidapi tanpa driver tambahan untuk board `16c0:05df`.

---

## Jalankan (semua platform)

```bash
# Mode CLI (default)
./usbrelay-cli                 # Windows: usbrelay-cli.exe

# Mode CLI dengan config custom
./usbrelay-cli --config /path/ke/rules.conf

# Mode GUI
./usbrelay-gui --gui           # Windows: usbrelay-gui.exe --gui

# Paksa jumlah channel (opsional, kalau auto-detect salah tebak)
./usbrelay-cli --channels 4

# Tampilkan bantuan
./usbrelay-cli --help
```

## Deteksi Jumlah Channel Relay (Otomatis)

Saat relay terhubung, jumlah channel dideteksi **otomatis** dari serial
number / product string HID -- tombolnya di GUI/CLI dibuat dinamis sesuai
hasil deteksi ini (tidak ada jumlah channel yang di-hardcode). Urutan
deteksi, dari yang paling dipercaya:

1. Pola eksplisit di product string, mis. `USBRelay4`, `LCUS-2` → langsung
   diambil angkanya.
2. Pola yang sama dicoba di serial number.
3. Konvensi firmware asli dcttech: digit terakhir pada serial number.
4. Digit terakhir pada product string.
5. Kalau semua gagal (string tidak mengandung petunjuk sama sekali) →
   fallback ke 1 channel.

**Catatan jujur:** protokol HID board relay `16c0:05df` (termasuk board
clone) memang tidak punya field "jumlah channel" di hardware-nya —
tidak ada cara membaca itu langsung dari device. Heuristik di atas
menutup sebagian besar kasus tanpa perlu edit file apa pun. Untuk kasus
langka yang tetap salah tebak, pakai flag `--channels N` sekali saat
menjalankan program (bukan file config yang perlu di-maintain).

## Tombol CLI

| Tombol | Fungsi                   |
|--------|--------------------------|
| Q      | Keluar                   |
| R      | Scan & connect relay     |
| C      | Connect relay            |
| D      | Disconnect relay         |
| A      | Semua channel ON         |
| Z      | Semua channel OFF        |
| 1-8    | Toggle channel 1-8       |

## Konfigurasi Device Map

Edit `config/device_map.conf`:

```
# Format: VID:PID  CHANNEL  ACTION  [LABEL]

# Mouse Logitech dipasang -> relay ch1 ON
046d:c52b  1  open_on_connect  Mouse Logitech

# USB LAN dipasang -> relay ch2 ON
0bda:8153  2  open_on_connect  USB LAN Adapter

# Flash drive tertentu -> semua relay ON
0781:5567  all  open_on_connect  SanDisk Flash Drive
```

Cari VID:PID device:
```bash
lsusb
# Contoh output:
# Bus 001 Device 003: ID 046d:c52b Logitech, Inc.
#                        ^^^^ ^^^^
#                        VID  PID
```

## Hardware USB Relay yang Didukung

Hardware dengan VID:PID `16c0:05df` (ICSTATION, SainSmart, dll).
Protokol HID standar:
- Write byte 2 = `0x01` → OPEN relay
- Write byte 2 = `0x02` → CLOSE relay
- Write byte 3 = channel (1-8) atau `0xFF` untuk semua

Jika relay kamu pakai VID:PID berbeda, ubah konstanta di:
```cpp
// src/relay_controller.h
#define USB_RELAY_VID 0x16C0
#define USB_RELAY_PID 0x05DF
```

## Struktur Proyek

```
usbrelay-autocontrol/
├── Makefile              ← build Linux
├── Makefile.macos        ← build macOS
├── build_windows.bat     ← build Windows (tanpa "make")
├── README.md
├── config/
│   └── device_map.conf      ← aturan VID:PID -> relay channel
└── src/
    ├── main.cpp              ← entry point, parse --cli/--gui
    ├── relay_controller.h/cpp ← kontrol relay via hidapi (sama di 3 OS)
    ├── usb_monitor.h/cpp     ← monitor USB (libudev/IOKit/SetupAPI sesuai OS)
    ├── device_mapper.h/cpp   ← parsing config & rule matching
    ├── app_core.h/cpp        ← business logic utama
    ├── cli_ui.cpp            ← TUI ncurses (Linux/macOS) / PDCurses (Windows)
    └── gui_ui.cpp            ← GUI GTK3
```

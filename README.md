# USB Relay Auto-Control

Aplikasi C++ untuk kontrol USB relay secara otomatis berdasarkan deteksi USB device.
Tersedia mode CLI (ncurses) dan GUI (GTK3).

## Cara Kerja

```
USB Device dipasang
      │
      ▼
USBMonitor (libudev) mendeteksi event
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

## Instalasi Dependencies

```bash
# Ubuntu / Debian / Raspberry Pi
sudo apt-get install -y \
    libhidapi-dev libhidapi-hidraw0 \
    libudev-dev \
    libncurses-dev \
    libgtk-3-dev \
    pkg-config build-essential

# Atau pakai Makefile:
make deps
```

## Build

```bash
make all      # Build keduanya (CLI + GUI)
make cli      # Hanya CLI
make gui      # Hanya GUI
```

## Pasang udev Rule (agar tidak perlu sudo)

```bash
make install-udev
# Lalu cabut dan pasang kembali relay USB
```

## Jalankan

```bash
# Mode CLI (default)
./usbrelay-cli

# Mode CLI dengan config custom
./usbrelay-cli --config /path/ke/rules.conf

# Mode GUI
./usbrelay-gui --gui

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
├── Makefile
├── README.md
├── config/
│   └── device_map.conf      ← aturan VID:PID -> relay channel
└── src/
    ├── main.cpp              ← entry point, parse --cli/--gui
    ├── relay_controller.h/cpp ← kontrol relay via hidapi
    ├── usb_monitor.h/cpp     ← monitor USB via libudev
    ├── device_mapper.h/cpp   ← parsing config & rule matching
    ├── app_core.h/cpp        ← business logic utama
    ├── cli_ui.cpp            ← TUI ncurses
    └── gui_ui.cpp            ← GUI GTK3
```

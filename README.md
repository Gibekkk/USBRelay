# USB Relay Auto-Control

Aplikasi C++ untuk kontrol USB relay secara otomatis berdasarkan deteksi USB device.
GUI (GTK3).

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
GUI update tampilan status
```

## Instalasi Dependencies

```bash
# Linux (Ubuntu/Debian/Raspberry Pi)
make deps-linux

# macOS (butuh Homebrew)
make deps-mac

# Windows (butuh MSYS2/MinGW-w64)
make deps-windows
```

## Build

Build dijalankan native di masing-masing OS (bukan cross-compile):

```bash
make linux     # di Linux   -> dist/gui-app-linux
make mac       # di macOS   -> dist/gui-app-mac
make windows   # di Windows -> dist/gui-app-win.exe
make all       # jalankan ketiganya sekaligus (hanya berhasil penuh kalau
               # toolchain ketiga OS tersedia di mesin yang sama, mis. CI matrix)
```

Backend deteksi USB otomatis dipilih sesuai OS: `libudev` (Linux, push-event),
polling `hidapi` ringan tiap 300ms (macOS & Windows, tanpa dependency tambahan).

## Pasang udev Rule (agar tidak perlu sudo, khusus Linux)

```bash
make install-udev
# Lalu cabut dan pasang kembali relay USB
```

## Jalankan

```bash
# Linux
./dist/gui-app-linux

# macOS
./dist/gui-app-mac

# Windows
dist\gui-app-win.exe

# Config custom
./dist/gui-app-linux --config /path/ke/rules.conf

# Paksa jumlah channel (opsional, kalau auto-detect salah tebak)
./dist/gui-app-linux --channels 4

# Tampilkan bantuan
./dist/gui-app-linux --help
```

## Deteksi Jumlah Channel Relay (Otomatis)

Saat relay terhubung, jumlah channel dideteksi **otomatis** dari serial
number / product string HID -- tombolnya di GUI dibuat dinamis sesuai
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
    ├── main.cpp              ← entry point
    ├── relay_controller.h/cpp ← kontrol relay via hidapi
    ├── usb_monitor.h/usb_monitor_{linux,mac,win}.cpp ← monitor USB per-OS
    ├── device_mapper.h/cpp   ← parsing config & rule matching
    ├── app_core.h/cpp        ← business logic utama
    └── gui_ui.cpp            ← GUI GTK3
```

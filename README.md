# USB Relay Auto-Control

Aplikasi C++ untuk kontrol USB relay, dipakai untuk menyalakan/mematikan
"box" (channel relay) berdasarkan scan NIM. Tersedia dalam mode GUI (GTK3).

Sudah bisa dibangun (build) untuk **Linux**, **macOS**, dan **Windows**. Kode
inti (relay_controller, device_mapper, app_core) sama persis di ketiga OS;
yang beda hanya lapisan deteksi USB (`usb_monitor.cpp`, otomatis pilih
implementasi lewat `#if defined(...)` -- libudev di Linux, IOKit di macOS,
SetupAPI di Windows) dan build system.

**Hasil build (binary) masuk ke folder `dist/`** -- tidak ada langkah
"install" ke sistem. Tinggal jalankan langsung dari `dist/`. Folder `dist/`
juga menyimpan `data.csv` (database NIM → nama) secara permanen, supaya
folder ini gampang di-copy/porting sebagai satu paket lengkap ke komputer
lain -- lihat bagian **"File Data Runtime"** di bawah untuk detail lengkap
soal tata letak file.

## Fitur Utama

- **Round-robin otomatis**: tidak perlu pilih channel manual lagi. Setiap
  ada NIM baru discan, aplikasi otomatis memakai channel **kosong bernomor
  terkecil** (mis. CH1 dulu, baru CH2, dst) dari daftar channel yang
  diaktifkan di `config/channels.conf`.
- **Nama pengguna ditampilkan**, bukan cuma NIM -- tombol channel yang
  aktif menampilkan nama orang yang sedang memakainya (diambil dari
  `data.csv`).
- **Durasi pemakaian real-time** -- tombol channel yang aktif juga
  menampilkan sudah berapa lama channel itu dipakai, dihitung dari
  timestamp `waktu_on` di `usage.csv`.
- **Channel yang dipakai bisa diatur lewat config** (1-16 channel) --
  lihat `config/channels.conf`. Jumlah tombol di GUI otomatis mengikuti
  berapa banyak channel yang kamu aktifkan di sana.

## Cara Kerja

Alur auto-control relay berdasarkan USB device lain yang dipasang/dicabut
(mis. mouse, USB LAN) diatur lewat `config/device_map.conf`:

```
USB Device dipasang
      │
      ▼
USBMonitor mendeteksi event   (Linux: libudev · macOS: IOKit · Windows: SetupAPI)
      │
      ▼
DeviceMapper mencocokkan VID:PID dengan aturan di config/device_map.conf
      │
      ▼
RelayController (hidapi) mengeksekusi perintah ke relay
      │
      ▼
GUI update tampilan status
```

Alur utama sehari-hari, scan NIM lewat panel "Cari NIM" di GUI, mengikuti
sistem round-robin:

```
NIM discan
      │
      ▼
Cari NIM di data.csv -> dapat nama
      │
      ├── NIM ini SUDAH pakai sebuah channel (tercatat di usage.csv)?
      │     └── YA -> channel itu dimatikan, baris dihapus dari usage.csv
      │               (dicatat sebagai "OUT" di logs/ddmmyy.csv)
      │
      └── NIM ini BELUM pakai channel manapun?
            └── YA -> pickNextAvailableChannel(): ambil channel KOSONG
                      bernomor TERKECIL dari config/channels.conf,
                      nyalakan, catat ke usage.csv (waktu_on = sekarang)
                      (dicatat sebagai "IN" di logs/ddmmyy.csv)
```

---

## Build di Linux

```bash
sudo apt-get install -y \
    libhidapi-dev libhidapi-hidraw0 \
    libudev-dev \
    libgtk-3-dev \
    pkg-config build-essential
# atau: make deps

make all      # Build GUI -> dist/usbrelay-gui
make clean    # Hapus folder build/ dan binary dist/usbrelay-gui
```

`make clean` **tidak lagi menghapus seluruh folder `dist/`** -- cuma hasil
kompilasinya (`build/` dan binary-nya) yang dihapus. Ini sengaja, karena
`dist/data.csv`, `dist/usage.csv`, dan `dist/logs/` sekarang menyimpan data
yang tidak boleh hilang gara-gara rebuild.

Config (`config/device_map.conf`, `config/channels.conf`) **tidak lagi
disalin ke `dist/`** saat build -- binary membacanya langsung dari folder
`config/` di root project (lewat path relatif `../config/`, lihat bagian
**"File Data Runtime"**).

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

# 3) Build -> dist/usbrelay-gui
make -f Makefile.macos all
```

Kalau mau bisa ketik `make` polos, symlink dulu:
```bash
ln -sf Makefile.macos Makefile
make all
```

**Catatan macOS:** hidapi di Mac mengakses HID device lewat IOKit, jadi saat
`dist/usbrelay-gui` pertama kali dijalankan, macOS bisa minta izin
**Input Monitoring** (System Settings → Privacy & Security → Input
Monitoring). Aktifkan izinnya lalu jalankan ulang programnya.

---

## Build di Windows

**Tidak butuh `make` sama sekali** -- cukup `g++` dari MSYS2. Ini sengaja
supaya tidak error seperti kalau pakai Makefile langsung di Windows (banyak
instalasi Windows tidak punya `make`, dan path library GTK/hidapi juga beda
formatnya dari Unix).

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
       mingw-w64-x86_64-gtk3
   ```
3. Dari Command Prompt / PowerShell (boleh di luar MSYS2), jalankan
   `build_windows.bat` di folder project ini.
   - Script otomatis mendeteksi `g++` dari `C:\msys64\mingw64\bin`.
   - Kalau MSYS2 diinstall bukan di `C:\msys64`, set environment variable
     `MSYS2_MINGW64_BIN` ke folder `...\mingw64\bin` sebelum menjalankan.
   - Script juga memaksa `PKG_CONFIG_PATH` ke folder MSYS2 secara eksplisit,
     supaya tetap benar walau ada `pkg-config` lain di PATH (misalnya dari
     Anaconda/Conda) yang tidak tahu-menahu soal MSYS2.
4. Hasil build: `dist\usbrelay-gui.exe` langsung muncul di folder `dist\`
   (config **tidak** ikut disalin ke situ lagi -- lihat catatan di bagian
   **"File Data Runtime"**).

**Catatan Windows:**
- Kalau `dist\usbrelay-gui.exe` dijalankan lewat double-click dan gagal start
  karena "DLL tidak ditemukan", jalankan dari dalam terminal MSYS2 MinGW64
  (`./dist/usbrelay-gui.exe`) supaya semua DLL GTK/hidapi ketemu lewat PATH,
  atau copy DLL yang relevan dari `C:\msys64\mingw64\bin` ke folder `dist\`
  sebelum didistribusikan ke komputer lain.
- Tidak perlu udev rule (itu khusus Linux) -- device HID pada Windows sudah
  bisa diakses lewat hidapi tanpa driver tambahan untuk board `16c0:05df`.
- `dist\usbrelay-gui.exe` dibuild dengan `-mwindows` (tanpa jendela console),
  jadi kalau gagal start karena hidapi/config tidak ketemu, program akan
  menampilkan `MessageBox` sebagai pengganti pesan error di terminal, supaya
  tetap terlihat walau dijalankan lewat double-click.

---

## Jalankan (semua platform)

```bash
cd dist

# Jalankan GUI
./usbrelay-gui                 # Windows: usbrelay-gui.exe

# Dengan config device map custom
./usbrelay-gui --config /path/ke/rules.conf

# Dengan config channel aktif custom
./usbrelay-gui --channels-config /path/ke/channels.conf

# Paksa jumlah channel hasil auto-detect hardware (opsional, 1-16)
./usbrelay-gui --channels 4

# Tampilkan bantuan
./usbrelay-gui --help
```

**Penting -- selalu jalankan dari dalam folder `dist/`** (`cd dist &&
./usbrelay-gui`). Default path config sekarang adalah `../config/` (relatif
terhadap folder tempat binary dijalankan), yaitu folder `config/` di root
project ini, persis di sebelah folder `dist/`. Kalau `dist/` dipindah,
folder `config/` harus ikut dipindah bersamanya (tetap sebagai folder
sejajar) supaya path relatif ini tetap benar. `data.csv` tidak kena aturan
ini -- filenya sudah permanen ada langsung di dalam `dist/`.

---

## Konfigurasi Channel Aktif (`config/channels.conf`)

Ini file baru yang menentukan **channel mana saja (1-16) yang aktif dipakai
aplikasi**:

```
# Satu nomor per baris, boleh koma, boleh range
1-4
```

- Jumlah tombol yang tampil di GUI **otomatis menyesuaikan** panjang daftar
  di file ini -- aktifkan 4 channel, muncul 4 tombol; aktifkan 16, muncul
  16 tombol.
- Channel di daftar ini jugalah yang dipakai sistem **round-robin**: setiap
  ada NIM baru discan, channel **kosong bernomor terkecil** di daftar ini
  yang dipakai lebih dulu (mis. kalau CH1-CH3 penuh dan CH4 kosong, CH4
  yang dipakai -- user tidak perlu dan tidak bisa lagi memilih channel
  manual).
- Format per baris: satu nomor (`3`), banyak dipisah koma (`1,2,3,4`), atau
  range (`1-4`, setara dengan `1,2,3,4`). Baris `#...` = komentar.
- Kalau file tidak ada / kosong / semua baris invalid → fallback otomatis
  ke channel 1-4.

Contoh lain (edit `config/channels.conf` langsung):
```
# Relay 8 channel, semua dipakai
1-8

# Cuma channel tertentu, tidak harus berurutan
1,3,5,7
```

Kalau nomor channel yang diaktifkan di sini lebih besar dari jumlah channel
yang berhasil dideteksi otomatis dari hardware (lihat bagian **"Deteksi
Jumlah Channel Relay"** di bawah), aplikasi tetap jalan tapi akan menulis
peringatan di panel Log -- pastikan hardware kamu memang punya channel
sebanyak itu.

---

## File Data Runtime (Penting!)

Program ini membaca/menulis beberapa file **relatif terhadap folder tempat
kamu menjalankan binary-nya** (current directory), bukan relatif ke lokasi
source code:

| File                          | Fungsi                                             | Lokasi default (relatif ke `dist/`) |
|--------------------------------|-----------------------------------------------------|--------------------------------------|
| `../config/device_map.conf`   | Aturan VID:PID → auto ON/OFF relay saat USB lain dipasang/dicabut | Dibaca langsung dari `config/` di root project, **tidak** disalin ke `dist/` |
| `../config/channels.conf`     | Channel (1-16) yang aktif dipakai GUI + round-robin | Dibaca langsung dari `config/` di root project, **tidak** disalin ke `dist/` |
| `data.csv`                    | Database NIM → nama (dipakai fitur scan NIM di GUI)  | Permanen di dalam `dist/data.csv`, sudah tidak ada salinan sumber di root |
| `usage.csv`                   | Snapshot channel yang sedang dipakai (real-time), termasuk `waktu_on` untuk hitung durasi | Dibuat & diperbarui otomatis oleh program saat berjalan, di dalam `dist/` |
| `logs/ddmmyy.csv`             | Riwayat harian IN/OUT (dibuat per hari)              | Folder `dist/logs/` dibuat otomatis oleh program |

**Kenapa dipisah begini:** `config/` cukup satu lokasi (root project) supaya
tidak ada kebingungan "yang mana yang asli" -- dulu ada dua salinan
(`config/` dan `dist/config/`) yang gampang beda kalau salah satunya diedit
langsung. `data.csv` sebaliknya, sengaja dibuat **satu-satunya** salinan di
dalam `dist/` (bukan disalin dari root) supaya folder `dist/` bisa langsung
di-copy/porting ke komputer lain sebagai satu paket utuh (binary + data
NIM) tanpa perlu bawa file config terpisah.

**Implikasi penting:** kalau kamu jalankan binary dari luar folder `dist/`
(misalnya double-click dari lokasi lain, symlink, atau shortcut dengan
"Start in" yang salah), program akan mencari `data.csv`/`../config/`
relatif ke folder itu -- **bukan** ke lokasi yang sebenarnya. Kalau
`data.csv` tidak ketemu, program **tidak akan menampilkan error** (gagal
diam-diam) -- fitur pencarian NIM cuma akan selalu "tidak ditemukan" tanpa
penjelasan.

**Aturan aman:**
- Selalu jalankan binary dari dalam folder `dist/` itu sendiri (`cd dist &&
  ./usbrelay-gui`, atau di Windows pastikan shortcut-nya punya "Start in" =
  folder `dist`).
- Kalau memindahkan/mem-porting aplikasi ke komputer lain, salin folder
  `config/` **dan** `dist/` bersama-sama sebagai dua folder sejajar (persis
  struktur di repo ini) -- jangan cuma `dist/` saja, karena `device_map.conf`
  dan `channels.conf` ada di `config/`, bukan di dalam `dist/`.
- Untuk update `data.csv` (database NIM), edit langsung `dist/data.csv` --
  file itu memang satu-satunya sumber sekarang, tidak perlu rebuild atau
  sinkron dari tempat lain.

---

## Kenapa Kadang "Connect Lalu Putus Lagi" (Sudah Diperbaiki)

Board relay clone `16c0:05df` (terutama di Windows) kadang gagal merespons
satu kali saat statusnya dibaca (`hid_get_feature_report`), walau device
sebenarnya baik-baik saja -- ini lumrah untuk firmware murah, bukan berarti
device benar-benar lepas. Sebelumnya, satu kali gagal baca langsung membuat
aplikasi menutup koneksi dan mengulang scan dari awal -- membuat relay
terlihat "connect lalu putus lagi" terus-menerus tanpa pernah stabil.

Sekarang aplikasi baru menganggap relay benar-benar terputus setelah gagal
membaca status **3 kali berturut-turut** (`kMaxStatusFails` di
`src/relay_controller.h`), bukan langsung pada kegagalan pertama.

## Deteksi Jumlah Channel Relay (Otomatis)

Saat relay terhubung, jumlah channel dideteksi **otomatis** dari serial
number / product string HID. Urutan deteksi, dari yang paling dipercaya:

1. Pola eksplisit di product string, mis. `USBRelay4`, `LCUS-2` → langsung
   diambil angkanya.
2. Pola yang sama dicoba di serial number.
3. Konvensi firmware asli dcttech: digit terakhir pada serial number.
4. Digit terakhir pada product string.
5. Kalau semua gagal (string tidak mengandung petunjuk sama sekali) →
   fallback ke 1 channel.

**Catatan jujur:** protokol HID board relay `16c0:05df` (termasuk board
clone) memang tidak punya field "jumlah channel" di hardware-nya —
tidak ada cara membaca itu langsung dari device. Heuristik di atas hanya
bisa membaca **satu digit (1-9)**, jadi tidak bisa mendeteksi board >9
channel secara otomatis dari nama/serial-nya. Untuk kasus itu (atau kalau
heuristiknya salah tebak), pakai flag `--channels N` (1-16) sekali saat
menjalankan program.

**Hubungannya dengan `config/channels.conf`:** dua hal ini independen.
Angka hasil auto-detect/`--channels` di atas cuma dipakai
`RelayController::setAll()` untuk menghitung mask "semua channel ON/OFF".
Channel mana yang benar-benar **muncul di GUI dan dipakai round-robin**
ditentukan sepenuhnya oleh `config/channels.conf` -- keduanya tidak harus
sama persis, tapi idealnya `channels.conf` tidak mengaktifkan channel yang
lebih besar dari jumlah channel fisik hardware kamu.

## Konfigurasi Device Map

Edit `config/device_map.conf` langsung (dibaca langsung dari sini, tidak
ada lagi salinan di `dist/config/`):

```
# Format: VID:PID  CHANNEL  ACTION  [LABEL]

# Mouse Logitech dipasang -> relay ch1 ON
046d:c52b  1  open_on_connect  Mouse Logitech

# USB LAN dipasang -> relay ch2 ON
0bda:8153  2  open_on_connect  USB LAN Adapter

# Flash drive tertentu -> semua relay ON
0781:5567  all  open_on_connect  SanDisk Flash Drive
```

CHANNEL boleh 1-16 atau `all`.

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
- Write byte 3 = nomor channel (tergantung hardware, umumnya 1-16) atau
  `0x00` untuk semua

Status internal aplikasi (`m_last_status`) memakai bitmask 16-bit supaya
bisa menampung sampai 16 channel sekaligus.

Jika relay kamu pakai VID:PID berbeda, ubah konstanta di:
```cpp
// src/relay_controller.h
#define USB_RELAY_VID 0x16C0
#define USB_RELAY_PID 0x05DF
```

## Struktur Proyek

```
usbrelay-autocontrol/
├── Makefile              ← build Linux      -> dist/usbrelay-gui
├── Makefile.macos        ← build macOS      -> dist/usbrelay-gui
├── build_windows.bat     ← build Windows (tanpa "make") -> dist/usbrelay-gui.exe
├── README.md
├── update.sh             ← git pull + rebuild + jalankan dari dist/
├── .gitignore            ← cuma ignore usage.csv & logs/ (data runtime/sesi)
├── config/
│   ├── device_map.conf      ← aturan VID:PID -> relay channel (dibaca langsung, TIDAK disalin ke dist/)
│   └── channels.conf        ← channel (1-16) yang AKTIF dipakai GUI + round-robin (dibaca langsung, TIDAK disalin ke dist/)
├── dist/                    ← hasil build (binary) + data.csv, DILACAK git
│   ├── usbrelay-gui(.exe)   ← hasil build, tidak ikut dihapus oleh "make clean" versi lama
│   ├── data.csv              ← database NIM -> nama, SATU-SATUNYA salinan (bukan disalin dari root lagi)
│   ├── usage.csv             ← dibuat otomatis saat program jalan (runtime, di-gitignore)
│   └── logs/                 ← dibuat otomatis saat program jalan (runtime, di-gitignore)
└── src/
    ├── main.cpp              ← entry point, parse argumen (--config/--channels-config/--channels/--help)
    ├── relay_controller.h/cpp ← kontrol relay via hidapi (bitmask 16-bit, sama di 3 OS)
    ├── usb_monitor.h/cpp     ← monitor USB (libudev/IOKit/SetupAPI sesuai OS)
    ├── device_mapper.h/cpp   ← parsing config/device_map.conf & rule matching
    ├── app_core.h/cpp        ← business logic utama
    └── gui_ui.cpp            ← GUI GTK3: round-robin, nama+durasi, config/channels.conf, usage.csv, data.csv, logs/
```

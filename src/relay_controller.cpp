#include "relay_controller.h"
#include <cstring>

RelayController::RelayController() = default;
RelayController::~RelayController() { closeDevice(); cleanup(); }

bool RelayController::init() {
    if (hid_init() < 0) return false;
    m_initialized = true;
    return true;
}

void RelayController::cleanup() {
    if (m_initialized) { hid_exit(); m_initialized = false; }
}

std::vector<RelayInfo> RelayController::enumerate() {
    std::vector<RelayInfo> result;
    auto* devs = hid_enumerate(USB_RELAY_VID, USB_RELAY_PID);
    for (auto* d = devs; d; d = d->next) {
        RelayInfo info;
        info.path = d->path ? d->path : "";

        if (d->serial_number) {
            std::wstring ws(d->serial_number);
            info.serial.assign(ws.begin(), ws.end());
        }

        // Deteksi jumlah channel dari char ke-4 serial (contoh "BITF4" = 4ch)
        if (info.serial.size() >= 5) {
            char c = info.serial[4];
            if (c >= '1' && c <= '8')
                info.num_channels = c - '0';
        }

        // Fallback: coba deteksi dari product string
        if (info.num_channels <= 0 && d->product_string) {
            std::wstring prod(d->product_string);
            for (int i = (int)prod.size() - 1; i >= 0; i--) {
                if (prod[i] >= L'1' && prod[i] <= L'8') {
                    info.num_channels = prod[i] - L'0';
                    break;
                }
            }
        }

        // Default fallback
        if (info.num_channels <= 0)
            info.num_channels = 1;

        result.push_back(info);
    }
    hid_free_enumeration(devs);
    return result;
}

bool RelayController::openDevice(const std::string& path) {
    closeDevice();
    m_device = hid_open_path(path.c_str());
    if (!m_device) return false;
    for (auto& d : enumerate()) {
        if (d.path == path) { m_info = d; break; }
    }
    m_last_status = 0;
    return true;
}

void RelayController::closeDevice() {
    if (m_device) { hid_close(m_device); m_device = nullptr; }
}

// Protocol HID USB relay dcttech/ICSTATION (SUDAH DIPERBAIKI & dikonfirmasi
// bekerja di hardware):
//   buf[0] = 0x00  (report ID)
//   buf[1] = 0xFF = ON,  0xFD = OFF   <-- command state ADA DI SINI
//   buf[2] = channel (1-8), 0x00 = semua channel  <-- channel ADA DI SINI
//
// Versi sebelumnya keliru: buf[1] selalu di-hardcode 0xFF (selalu kebaca
// firmware sebagai command ON) dan channel/state malah ditaruh di
// buf[2]/buf[3], menyebabkan command OFF tidak pernah benar-benar terkirim
// ke firmware (hanya cabut power yang bisa mematikan relay).
//
// BUG 1 & 3 FIX (tetap dipertahankan): Setelah write berhasil, update
// m_last_status secara optimistis agar UI langsung menampilkan status yang
// benar tanpa harus menunggu round-trip read dari hardware.
bool RelayController::setChannel(int ch, bool on) {
    if (!m_device) return false;
    uint8_t buf[9] = {};
    buf[0] = 0x00;
    buf[1] = on ? 0xFF : 0xFD;
    buf[2] = static_cast<uint8_t>(ch);
    bool ok = hid_write(m_device, buf, 9) == 9;
    if (ok) {
        // Update bit channel yang bersangkutan (ch 1-based → bit 0-based)
        if (on)
            m_last_status |= static_cast<uint8_t>(1 << (ch - 1));
        else
            m_last_status &= static_cast<uint8_t>(~(1 << (ch - 1)));
    }
    return ok;
}

bool RelayController::setAll(bool on) {
    if (!m_device) return false;
    uint8_t buf[9] = {};
    buf[0] = 0x00;
    buf[1] = on ? 0xFF : 0xFD;
    buf[2] = 0x00; // 0x00 = semua channel
    bool ok = hid_write(m_device, buf, 9) == 9;
    if (ok) {
        // Buat mask sesuai jumlah channel yang diketahui
        int n = (m_info.num_channels > 0 && m_info.num_channels <= 8)
                ? m_info.num_channels : 8;
        uint8_t mask = (n >= 8) ? 0xFF
                                : static_cast<uint8_t>((1 << n) - 1);
        m_last_status = on ? mask : 0x00;
    }
    return ok;
}

// BUG 1 & 2 FIX (tidak diubah — belum ada laporan masalah pada status read):
// - Kirim status-request command sebelum baca agar device mengirim laporan
//   status terkini.
// - Jika write gagal (device sudah dicabut), tutup device secara
//   internal sehingga isOpen() → false; AppCore bisa deteksi
//   disconnect tanpa sentinel value yang ambigu.
// Response: [serial(5 byte), num_ch, 0x00, status_bits]
uint8_t RelayController::getStatus() {
    if (!m_device) return m_last_status;

    // Kirim trigger status-request
    uint8_t req[9] = {};
    req[0] = 0x00;
    req[1] = 0x01;   // perintah "minta status"
    if (hid_write(m_device, req, 9) < 0) {
        // Write gagal → device sudah tidak bisa diakses (dicabut)
        // Tutup device agar isOpen() menjadi false; caller deteksi disconnect
        hid_close(m_device);
        m_device = nullptr;
        return m_last_status;
    }

    uint8_t buf[9] = {};
    int res = hid_read_timeout(m_device, buf, 9, 500);
    if (res >= 8) {
        m_last_status = buf[7];
    }
    // Jika timeout, kembalikan cache terakhir (bukan 0)
    return m_last_status;
}
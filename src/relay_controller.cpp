#include "relay_controller.h"
#include <cstring>
#include <sstream>
#include <iomanip>

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

        if (info.serial.size() >= 5) {
            char c = info.serial[4];
            if (c >= '1' && c <= '8')
                info.num_channels = c - '0';
        }

        if (info.num_channels <= 0 && d->product_string) {
            std::wstring prod(d->product_string);
            for (int i = (int)prod.size() - 1; i >= 0; i--) {
                if (prod[i] >= L'1' && prod[i] <= L'8') {
                    info.num_channels = prod[i] - L'0';
                    break;
                }
            }
        }

        if (info.num_channels <= 0)
            info.num_channels = 1;

        // Auto-detect dari serial/product string tidak bisa diandalkan
        // untuk semua board clone. Kalau user sudah set override manual
        // (config/Relay.conf), pakai itu -> lebih akurat & konsisten.
        if (m_channel_override > 0)
            info.num_channels = m_channel_override;

        result.push_back(info);
    }
    hid_free_enumeration(devs);
    return result;
}

// ---------------------------------------------------------------
// Bantu: format buffer jadi hex string
// ---------------------------------------------------------------
static std::string hexDump(const uint8_t* buf, int len) {
    std::ostringstream ss;
    for (int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0')
           << (int)buf[i];
        if (i < len - 1) ss << " ";
    }
    return ss.str();
}

// ---------------------------------------------------------------
// Coba satu metode baca, return status byte (0 jika gagal/timeout),
// isi label dengan deskripsi + raw hex.
// method 0 : hid_get_feature_report report-ID 0x00
// method 1 : hid_get_feature_report report-ID 0x01
// method 2 : hid_read_timeout (tanpa write)
// method 3 : write [0x00,0x01,...] lalu hid_read_timeout
// method 4 : write [0x00,0xD2,...] lalu hid_read_timeout
// ---------------------------------------------------------------
uint8_t RelayController::tryReadMethod(int method, std::string& label) {
    if (!m_device) { label = "no device"; return 0; }

    uint8_t buf[9] = {};
    int res = -99;

    if (method == 0) {
        label = "hid_get_feature_report(ID=0x00)";
        buf[0] = 0x00;
        res = hid_get_feature_report(m_device, buf, sizeof(buf));
    } else if (method == 1) {
        label = "hid_get_feature_report(ID=0x01)";
        buf[0] = 0x01;
        res = hid_get_feature_report(m_device, buf, sizeof(buf));
    } else if (method == 2) {
        label = "hid_read_timeout(no write)";
        res = hid_read_timeout(m_device, buf, sizeof(buf), 300);
    } else if (method == 3 || method == 4) {
        uint8_t cmd = (method == 3) ? 0x01 : 0xD2;
        label = std::string("write[0x") + (method == 3 ? "01" : "D2") + "]+hid_read_timeout";
        uint8_t req[9] = {};
        req[0] = 0x00;
        req[1] = cmd;
        if (hid_write(m_device, req, 9) < 0) {
            label += " WRITE_FAIL";
            return 0;
        }
        res = hid_read_timeout(m_device, buf, sizeof(buf), 300);
    }

    std::ostringstream ss;
    ss << label << " res=" << res << " [" << hexDump(buf, 9) << "]";
    label = ss.str();

    // Kembalikan semua kandidat byte status (b6, b7, b8) digabung ke nibble
    // Caller akan memilih mana yang masuk akal
    if (res > 0) {
        // Simpan semua byte di label sudah; kembalikan res > 0 sebagai sinyal sukses
        return (uint8_t)res; // >0 berarti berhasil baca
    }
    return 0;
}

// ---------------------------------------------------------------
// scanStatus: coba semua metode, log hasilnya, pilih status terbaik
// ---------------------------------------------------------------
std::vector<std::string> RelayController::scanStatus() {
    std::vector<std::string> logs;
    if (!m_device) {
        logs.push_back("[scan] Device tidak terbuka.");
        return logs;
    }

    logs.push_back("[scan] Mulai scan semua metode baca status...");

    struct Result { int method; int res; uint8_t buf[9]; };
    std::vector<Result> results;

    // Method 0: feature report ID 0x00
    {
        uint8_t buf[9] = {}; buf[0] = 0x00;
        int res = hid_get_feature_report(m_device, buf, sizeof(buf));
        std::string s = "M0 hid_get_feature_report(0x00) res=" + std::to_string(res)
                      + " [" + hexDump(buf, 9) + "]";
        logs.push_back(s);
        results.push_back({0, res, {}});
        memcpy(results.back().buf, buf, 9);
    }

    // Method 1: feature report ID 0x01
    {
        uint8_t buf[9] = {}; buf[0] = 0x01;
        int res = hid_get_feature_report(m_device, buf, sizeof(buf));
        std::string s = "M1 hid_get_feature_report(0x01) res=" + std::to_string(res)
                      + " [" + hexDump(buf, 9) + "]";
        logs.push_back(s);
        results.push_back({1, res, {}});
        memcpy(results.back().buf, buf, 9);
    }

    // Method 2: hid_read_timeout tanpa write
    {
        uint8_t buf[9] = {};
        int res = hid_read_timeout(m_device, buf, sizeof(buf), 300);
        std::string s = "M2 hid_read_timeout(no-write) res=" + std::to_string(res)
                      + " [" + hexDump(buf, 9) + "]";
        logs.push_back(s);
        results.push_back({2, res, {}});
        memcpy(results.back().buf, buf, 9);
    }

    // Method 3: write 0x01 lalu read
    {
        uint8_t req[9] = {}; req[1] = 0x01;
        int wres = hid_write(m_device, req, 9);
        uint8_t buf[9] = {};
        int res = (wres > 0) ? hid_read_timeout(m_device, buf, sizeof(buf), 300) : -1;
        std::string s = "M3 write[0x01]+read wres=" + std::to_string(wres)
                      + " res=" + std::to_string(res)
                      + " [" + hexDump(buf, 9) + "]";
        logs.push_back(s);
        results.push_back({3, res, {}});
        memcpy(results.back().buf, buf, 9);
    }

    // Method 4: write 0xD2 lalu read
    {
        uint8_t req[9] = {}; req[1] = 0xD2;
        int wres = hid_write(m_device, req, 9);
        uint8_t buf[9] = {};
        int res = (wres > 0) ? hid_read_timeout(m_device, buf, sizeof(buf), 300) : -1;
        std::string s = "M4 write[0xD2]+read wres=" + std::to_string(wres)
                      + " res=" + std::to_string(res)
                      + " [" + hexDump(buf, 9) + "]";
        logs.push_back(s);
        results.push_back({4, res, {}});
        memcpy(results.back().buf, buf, 9);
    }

    // Pilih metode terbaik: res > 0, dan jika m_status_method sudah diset pakai itu
    // Kandidat byte status: index 6, 7, 8
    // CATATAN: byte hasil decode di sini HANYA untuk log/diagnostik &
    // menentukan metode baca mana yang direspons device (dipakai getStatus()
    // untuk cek device masih hidup/tidak). TIDAK dipakai untuk menimpa
    // m_last_status, karena decode bitmask ini terbukti tidak reliable
    // untuk sebagian board multi-channel (CH1 kebetulan cocok, CH2+ tidak).
    // m_last_status yang jadi acuan tetap dari software (setChannel/setAll),
    // direkonsiliasi ulang dari usage.csv oleh pemanggil setelah connect.
    uint8_t best_status = m_last_status;
    int     best_method = -1;

    for (auto& r : results) {
        if (r.res <= 0) continue;
        // Jika sudah tahu metode yang bekerja, gunakan itu
        if (m_status_method == r.method) {
            uint8_t s = (r.res >= 9) ? r.buf[8]
                      : (r.res >= 8) ? r.buf[7]
                      : (r.res >= 7) ? r.buf[6] : 0;
            best_status = s;
            best_method = r.method;
            break;
        }
        // Pertama kali: ambil metode pertama yang sukses
        if (best_method == -1) {
            uint8_t s = (r.res >= 9) ? r.buf[8]
                      : (r.res >= 8) ? r.buf[7]
                      : (r.res >= 7) ? r.buf[6] : 0;
            best_status = s;
            best_method = r.method;
        }
    }

    if (best_method >= 0) {
        m_status_method = best_method;
        // m_last_status SENGAJA tidak ditimpa oleh best_status (lihat catatan di atas)
        std::ostringstream ss;
        ss << "[scan] Metode baca yang dipakai: M" << best_method
           << " (raw decode 0x" << std::hex << std::setw(2) << std::setfill('0')
           << (int)best_status << std::dec << ", hanya untuk cek koneksi)"
           << " -- status channel sebenarnya mengikuti catatan software.";
        logs.push_back(ss.str());
    } else {
        logs.push_back("[scan] Semua metode gagal / timeout.");
    }

    return logs;
}

bool RelayController::openDevice(const std::string& path) {
    closeDevice();
    m_device = hid_open_path(path.c_str());
    if (!m_device) return false;
    for (auto& d : enumerate()) {
        if (d.path == path) { m_info = d; break; }
    }
    m_last_status  = 0;
    m_status_method = -1;
    // scan dilakukan dari AppCore setelah openDevice agar hasilnya bisa di-log ke GUI
    return true;
}

void RelayController::closeDevice() {
    if (m_device) { hid_close(m_device); m_device = nullptr; }
}

bool RelayController::setChannel(int ch, bool on) {
    if (!m_device) return false;
    uint8_t buf[9] = {};
    buf[0] = 0x00;
    buf[1] = on ? 0xFF : 0xFD;
    buf[2] = static_cast<uint8_t>(ch);
    bool ok = hid_write(m_device, buf, 9) == 9;
    if (ok) {
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
    buf[2] = 0x00;
    bool ok = hid_write(m_device, buf, 9) == 9;
    if (ok) {
        int n = (m_info.num_channels > 0 && m_info.num_channels <= 8)
                ? m_info.num_channels : 8;
        uint8_t mask = (n >= 8) ? 0xFF : static_cast<uint8_t>((1 << n) - 1);
        m_last_status = on ? mask : 0x00;
    }
    return ok;
}

uint8_t RelayController::getStatus() {
    if (!m_device) return m_last_status;

    // ---------------------------------------------------------------
    // PENTING: kita TIDAK lagi menimpa m_last_status dengan hasil decode
    // byte mentah dari hardware di sini. Banyak board clone 16c0:05df
    // (termasuk board 4-channel) tidak mengembalikan bitmask multi-channel
    // yang konsisten lewat metode baca manapun (hid_get_feature_report /
    // hid_read_timeout) — dulu ini yang bikin CH1 kebetulan kebaca benar
    // tapi CH2-CH4 salah. Status channel yang kita percayai adalah state
    // yang KITA sendiri kirim & catat lewat setChannel()/setAll()
    // (m_last_status), yang direkonsiliasi ulang saat konek (lihat
    // gui_ui.cpp: reasserted dari usage.csv).
    //
    // Pembacaan hardware di bawah ini HANYA dipakai untuk mendeteksi
    // apakah device masih hidup/terhubung (write/read gagal = dianggap
    // lepas), bukan untuk menentukan status channel mana yang ON/OFF.
    // ---------------------------------------------------------------
    if (m_status_method < 0) {
        // Belum scan, return cache
        return m_last_status;
    }

    uint8_t buf[9] = {};
    int res = -1;

    if (m_status_method == 0) {
        buf[0] = 0x00;
        res = hid_get_feature_report(m_device, buf, sizeof(buf));
    } else if (m_status_method == 1) {
        buf[0] = 0x01;
        res = hid_get_feature_report(m_device, buf, sizeof(buf));
    } else if (m_status_method == 2) {
        res = hid_read_timeout(m_device, buf, sizeof(buf), 300);
    } else if (m_status_method == 3) {
        uint8_t req[9] = {}; req[1] = 0x01;
        if (hid_write(m_device, req, 9) > 0)
            res = hid_read_timeout(m_device, buf, sizeof(buf), 300);
    } else if (m_status_method == 4) {
        uint8_t req[9] = {}; req[1] = 0xD2;
        if (hid_write(m_device, req, 9) > 0)
            res = hid_read_timeout(m_device, buf, sizeof(buf), 300);
    }

    if (res < 0) {
        hid_close(m_device);
        m_device = nullptr;
    }

    // res >= 0 berarti device masih merespons -> tetap terhubung.
    // Status channel dikembalikan dari cache software (m_last_status),
    // bukan dari decode buf di atas.
    return m_last_status;
}

void RelayController::setLastStatus(uint8_t s) { m_last_status = s; }
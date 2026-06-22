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
    return true;
}

void RelayController::closeDevice() {
    if (m_device) { hid_close(m_device); m_device = nullptr; }
}

// Protocol HID USB relay dcttech/ICSTATION:
//   buf[0] = 0x00  (report ID)
//   buf[1] = 0xFF
//   buf[2] = 0x01 = ON/OPEN, 0x02 = OFF/CLOSE
//   buf[3] = channel (1-8), 0xFF = semua
bool RelayController::setChannel(int ch, bool on) {
    if (!m_device) return false;
    uint8_t buf[9] = {};
    buf[0] = 0x00;
    buf[1] = 0xFF;
    buf[2] = on ? 0x01 : 0x02;
    buf[3] = static_cast<uint8_t>(ch);
    return hid_write(m_device, buf, 9) == 9;
}

bool RelayController::setAll(bool on) {
    if (!m_device) return false;
    uint8_t buf[9] = {};
    buf[0] = 0x00;
    buf[1] = 0xFF;
    buf[2] = on ? 0x01 : 0x02;
    buf[3] = 0xFF;
    return hid_write(m_device, buf, 9) == 9;
}

// Status: read langsung tanpa trigger write
// Response: [serial(5 byte), num_ch, 0x00, status_bits]
// status_bits: bit0=ch1, bit1=ch2, ...
uint8_t RelayController::getStatus() {
    if (!m_device) return 0;
    uint8_t buf[9] = {};
    int res = hid_read_timeout(m_device, buf, 9, 300);
    if (res >= 8)
        return buf[7];
    return m_last_status;  // kembalikan status terakhir jika read gagal
}

void RelayController::setLastStatus(uint8_t s) {
    m_last_status = s;
}
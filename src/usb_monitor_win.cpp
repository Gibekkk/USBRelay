#include "usb_monitor.h"
#if defined(_WIN32)
#include <hidapi/hidapi.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

// Windows: tanpa dependency SetupAPI/WM_DEVICECHANGE tambahan, dipakai polling atas
// hid_enumerate() setiap 300ms, dibandingkan dengan snapshot terakhir
// untuk mendeteksi ADDED/REMOVED. Cukup untuk kebutuhan relay board ini
// (device jarang berganti selagi aplikasi berjalan).

static std::string toHex4(unsigned short v) {
    std::ostringstream ss;
    ss << std::hex << std::setw(4) << std::setfill('0') << v;
    return ss.str();
}

static std::vector<USBDevice> enumerateHid() {
    std::vector<USBDevice> out;
    hid_device_info* devs = hid_enumerate(0x0, 0x0);
    for (hid_device_info* cur = devs; cur; cur = cur->next) {
        USBDevice d;
        d.vid = toHex4(cur->vendor_id);
        d.pid = toHex4(cur->product_id);
        if (cur->serial_number) {
            char buf[256] = {0};
            wcstombs(buf, cur->serial_number, sizeof(buf) - 1);
            d.serial = buf;
        }
        if (cur->manufacturer_string) {
            char buf[256] = {0};
            wcstombs(buf, cur->manufacturer_string, sizeof(buf) - 1);
            d.manufacturer = buf;
        }
        if (cur->product_string) {
            char buf[256] = {0};
            wcstombs(buf, cur->product_string, sizeof(buf) - 1);
            d.product = buf;
        }
        d.syspath = cur->path ? cur->path : "";
        d.devpath = d.syspath;
        out.push_back(d);
    }
    hid_free_enumeration(devs);
    return out;
}

static bool sameDevice(const USBDevice& a, const USBDevice& b) {
    return a.syspath == b.syspath && a.vid == b.vid && a.pid == b.pid;
}

USBMonitor::USBMonitor() { hid_init(); }

USBMonitor::~USBMonitor() { stop(); hid_exit(); }

void USBMonitor::setCallback(USBCallback cb) {
    std::lock_guard<std::mutex> lk(m_cbMutex);
    m_callback = cb;
}

bool USBMonitor::start() {
    m_lastSnapshot = enumerateHid();
    m_running = true;
    m_thread  = std::thread(&USBMonitor::run, this);
    return true;
}

void USBMonitor::stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

void USBMonitor::run() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        if (!m_running) break;

        auto current = enumerateHid();

        // Cari device baru (ADDED)
        for (auto& d : current) {
            bool found = false;
            for (auto& old : m_lastSnapshot) if (sameDevice(d, old)) { found = true; break; }
            if (!found) {
                std::lock_guard<std::mutex> lk(m_cbMutex);
                if (m_callback) m_callback(d, USBAction::ADDED);
            }
        }
        // Cari device hilang (REMOVED)
        for (auto& old : m_lastSnapshot) {
            bool found = false;
            for (auto& d : current) if (sameDevice(d, old)) { found = true; break; }
            if (!found) {
                std::lock_guard<std::mutex> lk(m_cbMutex);
                if (m_callback) m_callback(old, USBAction::REMOVED);
            }
        }
        m_lastSnapshot = current;
    }
}

std::vector<USBDevice> USBMonitor::getConnectedDevices() {
    return enumerateHid();
}

#endif // _WIN32

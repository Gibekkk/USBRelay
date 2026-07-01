#include "app_core.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cctype>
#include <cstdlib>

// ---------------------------------------------------------------
// Baca override jumlah channel relay dari config/Relay.conf (opsional).
// Auto-detect dari serial/product string HID (relay_controller.cpp) TIDAK
// bisa diandalkan untuk semua board clone 16c0:05df -- kalau file ini ada
// dan berisi NUM_CHANNELS=n, nilai itu yang dipakai, bukan hasil tebakan.
// Dipanggil dari AppCore::init() supaya berlaku untuk mode GUI maupun CLI.
// ---------------------------------------------------------------
static void loadRelayChannelConfig(RelayController& relay, const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        while (!key.empty() && isspace((unsigned char)key.back())) key.pop_back();
        size_t start = val.find_first_not_of(" \t");
        if (start != std::string::npos) val = val.substr(start);
        while (!val.empty() && isspace((unsigned char)val.back())) val.pop_back();
        if (key == "NUM_CHANNELS") {
            int n = std::atoi(val.c_str());
            if (n > 0 && n <= 8) relay.setChannelCountOverride(n);
        }
    }
}

static std::string ts() {
    auto t  = std::time(nullptr);
    auto tm = *std::localtime(&t);
    std::ostringstream ss;
    ss << std::put_time(&tm, "[%H:%M:%S]");
    return ss.str();
}

AppCore::AppCore()  = default;
AppCore::~AppCore() { shutdown(); }

bool AppCore::init(const std::string& configPath) {
    if (!m_relay.init()) return false;
    if (!configPath.empty()) m_mapper.loadConfig(configPath);

    // Path config jumlah channel relay: selalu di config/Relay.conf,
    // terpisah dari device_map.conf (configPath, argumen di atas).
    loadRelayChannelConfig(m_relay, "config/Relay.conf");

    m_monitor.setCallback([this](const USBDevice& d, USBAction a) {
        onUSBEvent(d, a);
    });
    return m_monitor.start();
}

void AppCore::shutdown() {
    m_monitor.stop();
    m_relay.closeDevice();
    m_relay.cleanup();
}

bool AppCore::connectRelay(const std::string& path) {
    std::lock_guard<std::mutex> lk(m_relayMtx);
    if (!m_relay.openDevice(path)) return false;

    AppEvent ev;
    ev.type         = AppEvent::Type::RELAY_CONNECTED;
    ev.message      = "Relay terhubung: " + m_relay.getInfo().serial;
    ev.relay_status = m_relay.getStatus();
    pushEvent(ev);
    return true;
}

// Versi internal tanpa lock (dipanggil dari onUSBEvent yang sudah lock)
void AppCore::disconnectRelayInternal() {
    m_relay.closeDevice();
    AppEvent ev;
    ev.type    = AppEvent::Type::RELAY_DISCONNECTED;
    ev.message = "Relay terputus (USB dicabut)";
    pushEvent(ev);
}

void AppCore::disconnectRelay() {
    std::lock_guard<std::mutex> lk(m_relayMtx);
    disconnectRelayInternal();
}

bool AppCore::setRelay(int ch, bool on) {
    std::lock_guard<std::mutex> lk(m_relayMtx);
    if (!m_relay.setChannel(ch, on)) {
        // Tulis gagal: relay mungkin sudah dicabut
        if (m_relay.isOpen()) {
            disconnectRelayInternal();
        }
        return false;
    }
    AppEvent ev;
    ev.type         = AppEvent::Type::RELAY_CHANGED;
    ev.relay_status = m_relay.getStatus();
    ev.message      = "CH" + std::to_string(ch) + (on ? " ON" : " OFF");
    pushEvent(ev);
    return true;
}

bool AppCore::setAll(bool on) {
    std::lock_guard<std::mutex> lk(m_relayMtx);
    if (!m_relay.setAll(on)) {
        if (m_relay.isOpen()) {
            disconnectRelayInternal();
        }
        return false;
    }
    AppEvent ev;
    ev.type         = AppEvent::Type::RELAY_CHANGED;
    ev.relay_status = m_relay.getStatus();
    ev.message      = std::string("Semua relay ") + (on ? "ON" : "OFF");
    pushEvent(ev);
    return true;
}

std::vector<std::string> AppCore::scanRelayStatus() {
    std::lock_guard<std::mutex> lk(m_relayMtx);
    return m_relay.scanStatus();
}

std::vector<RelayInfo>  AppCore::getRelayDevices()       { return m_relay.enumerate(); }
std::vector<USBDevice>  AppCore::getUSBDevices()          { return m_monitor.getConnectedDevices(); }
bool                    AppCore::isRelayConnected() const  { return m_relay.isOpen(); }
RelayInfo               AppCore::getRelayInfo() const     { return m_relay.getInfo(); }

uint8_t AppCore::getRelayStatus() {
    std::lock_guard<std::mutex> lk(m_relayMtx);
    if (!m_relay.isOpen()) return 0;
    uint8_t s = m_relay.getStatus();
    // Jika read gagal berkali-kali, relay mungkin sudah dicabut
    return s;
}

void AppCore::setEventCallback(EventCb cb) { m_eventCb = cb; }

bool AppCore::pollEvent(AppEvent& ev) {
    std::lock_guard<std::mutex> lk(m_queueMtx);
    if (m_queue.empty()) return false;
    ev = m_queue.front();
    m_queue.pop_front();
    return true;
}

void AppCore::saveConfig(const std::string& path) { m_mapper.saveConfig(path); }

// ---------------------------------------------------------------
// Dipanggil dari thread USB monitor saat ada USB add/remove
// ---------------------------------------------------------------
void AppCore::onUSBEvent(const USBDevice& dev, USBAction action) {
    bool connected = (action == USBAction::ADDED);

    AppEvent usbEv;
    usbEv.type       = connected ? AppEvent::Type::USB_ADDED : AppEvent::Type::USB_REMOVED;
    usbEv.usb_device = dev;
    usbEv.message    = ts() + (connected ? " Terhubung: " : " Terputus: ")
                       + dev.product + " [" + dev.vid + ":" + dev.pid + "]";
    pushEvent(usbEv);

    // Jika relay dicabut: cek apakah VID:PID ini adalah relay yang sedang konek
    if (!connected) {
        std::lock_guard<std::mutex> lk(m_relayMtx);
        if (m_relay.isOpen()) {
            // Coba tulis dummy; kalau gagal berarti relay memang sudah lepas
            // Atau cukup cocokkan VID:PID dengan relay yang dikenal
            if (dev.vid == "16c0" && dev.pid == "05df") {
                disconnectRelayInternal();
                return;
            }
            // Fallback: cek apakah masih bisa diakses
            if (m_relay.getStatus() == 0xFF) {  // nilai error sentinel
                disconnectRelayInternal();
                return;
            }
        }
    }

    // Cari aturan device_mapper (untuk auto-control relay berdasarkan USB lain)
    auto rules = m_mapper.findRules(dev.vid, dev.pid);
    for (auto& rule : rules) {
        bool shouldOn = connected
                        ? (rule.action == RelayAction::OPEN_ON_CONNECT)
                        : (rule.action != RelayAction::OPEN_ON_CONNECT);

        std::lock_guard<std::mutex> lk(m_relayMtx);
        if (!m_relay.isOpen()) continue;

        if (rule.relay_channel == 0)
            m_relay.setAll(shouldOn);
        else
            m_relay.setChannel(rule.relay_channel, shouldOn);

        std::string chStr = (rule.relay_channel == 0)
                            ? "semua ch" : ("ch" + std::to_string(rule.relay_channel));

        AppEvent relayEv;
        relayEv.type         = AppEvent::Type::RELAY_CHANGED;
        relayEv.relay_status = m_relay.getStatus();
        relayEv.message      = "Auto: " + chStr + (shouldOn ? " ON" : " OFF")
                               + " untuk " + rule.label;
        pushEvent(relayEv);
    }
}

void AppCore::pushEvent(const AppEvent& ev) {
    {
        std::lock_guard<std::mutex> lk(m_queueMtx);
        m_queue.push_back(ev);
        if (m_queue.size() > 200) m_queue.pop_front();
    }
    if (m_eventCb) m_eventCb(ev);
}

void AppCore::log(const std::string& msg) {
    AppEvent ev;
    ev.type    = AppEvent::Type::LOG;
    ev.message = msg;
    std::lock_guard<std::mutex> lk(m_queueMtx);
    m_queue.push_back(ev);
    if (m_queue.size() > 200) m_queue.pop_front();
}
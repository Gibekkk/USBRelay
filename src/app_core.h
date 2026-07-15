#pragma once
#include "relay_controller.h"
#include "usb_monitor.h"
#include "device_mapper.h"
#include <functional>
#include <mutex>
#include <deque>
#include <string>
#include <vector>

struct AppEvent {
    enum class Type {
        USB_ADDED,
        USB_REMOVED,
        RELAY_CHANGED,
        RELAY_CONNECTED,
        RELAY_DISCONNECTED,
        LOG
    };
    Type        type;
    std::string message;
    USBDevice   usb_device;
    uint16_t    relay_status = 0;
};

class AppCore {
public:
    using EventCb = std::function<void(const AppEvent&)>;

    AppCore();
    ~AppCore();

    bool init(const std::string& configPath = "");
    void shutdown();

    bool connectRelay(const std::string& path);
    void disconnectRelay();
    bool setRelay(int ch, bool on);
    bool setAll(bool on);
    void setChannelCountOverride(int n) { m_relay.setChannelCountOverride(n); }

    std::vector<RelayInfo>  getRelayDevices();
    std::vector<USBDevice>  getUSBDevices();
    uint16_t                 getRelayStatus();
    bool                    isRelayConnected() const;
    RelayInfo               getRelayInfo() const;
    DeviceMapper&           getMapper() { return m_mapper; }
    std::vector<std::string> scanRelayStatus();

    void setEventCallback(EventCb cb);
    bool pollEvent(AppEvent& ev);
    void saveConfig(const std::string& path);

private:
    void onUSBEvent(const USBDevice& dev, USBAction action);
    void disconnectRelayInternal();   // tanpa lock, panggil saat sudah pegang m_relayMtx
    void pushEvent(const AppEvent& ev);
    void log(const std::string& msg);

    RelayController m_relay;
    USBMonitor      m_monitor;
    DeviceMapper    m_mapper;
    EventCb         m_eventCb;

    std::deque<AppEvent> m_queue;
    std::mutex           m_queueMtx;
    std::mutex           m_relayMtx;
};
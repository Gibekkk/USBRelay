#pragma once
#include <libudev.h>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

struct USBDevice {
    std::string vid;
    std::string pid;
    std::string serial;
    std::string manufacturer;
    std::string product;
    std::string syspath;
    std::string devpath;
};

enum class USBAction { ADDED, REMOVED };

using USBCallback = std::function<void(const USBDevice&, USBAction)>;

class USBMonitor {
public:
    USBMonitor();
    ~USBMonitor();

    void setCallback(USBCallback cb);
    bool start();
    void stop();

    // Ambil semua USB device yang saat ini terhubung
    std::vector<USBDevice> getConnectedDevices();

private:
    void run();
    USBDevice fromUdev(struct udev_device* dev);

    struct udev*         m_udev    = nullptr;
    struct udev_monitor* m_monitor = nullptr;
    USBCallback          m_callback;
    std::thread          m_thread;
    std::atomic<bool>    m_running{false};
    std::mutex           m_cbMutex;
};

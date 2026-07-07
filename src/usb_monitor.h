#pragma once
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

#if defined(__linux__)
    #include <libudev.h>
#endif

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

    USBCallback          m_callback;
    std::thread          m_thread;
    std::atomic<bool>    m_running{false};
    std::mutex           m_cbMutex;

#if defined(__linux__)
    USBDevice fromUdev(struct udev_device* dev);
    struct udev*         m_udev    = nullptr;
    struct udev_monitor* m_monitor = nullptr;
#else
    // Mac & Windows: tidak ada push-notification bawaan yang portable
    // tanpa dependency tambahan (IOKit / SetupAPI), jadi dipakai polling
    // ringan atas hasil hid_enumerate() setiap beberapa ratus ms.
    std::vector<USBDevice> m_lastSnapshot;
#endif
};

#include "usb_monitor.h"
#if defined(__linux__)
#include <poll.h>
#include <cstring>

USBMonitor::USBMonitor() { m_udev = udev_new(); }

USBMonitor::~USBMonitor() {
    stop();
    if (m_udev) udev_unref(m_udev);
}

void USBMonitor::setCallback(USBCallback cb) {
    std::lock_guard<std::mutex> lk(m_cbMutex);
    m_callback = cb;
}

bool USBMonitor::start() {
    if (!m_udev) return false;
    m_monitor = udev_monitor_new_from_netlink(m_udev, "udev");
    if (!m_monitor) return false;
    // Filter: hanya subsystem usb, devtype usb_device
    udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "usb", "usb_device");
    udev_monitor_enable_receiving(m_monitor);
    m_running = true;
    m_thread  = std::thread(&USBMonitor::run, this);
    return true;
}

void USBMonitor::stop() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
    if (m_monitor) { udev_monitor_unref(m_monitor); m_monitor = nullptr; }
}

USBDevice USBMonitor::fromUdev(struct udev_device* dev) {
    USBDevice d;
    auto get = [&](const char* attr) -> std::string {
        const char* v = udev_device_get_sysattr_value(dev, attr);
        return v ? v : "";
    };
    d.vid          = get("idVendor");
    d.pid          = get("idProduct");
    d.serial       = get("serial");
    d.manufacturer = get("manufacturer");
    d.product      = get("product");
    d.syspath      = udev_device_get_syspath(dev)  ? udev_device_get_syspath(dev)  : "";
    d.devpath      = udev_device_get_devpath(dev)  ? udev_device_get_devpath(dev)  : "";
    return d;
}

// Thread loop: poll fd udev, dispatch event ke callback
void USBMonitor::run() {
    int fd = udev_monitor_get_fd(m_monitor);
    while (m_running) {
        struct pollfd pfd = { fd, POLLIN, 0 };
        if (poll(&pfd, 1, 200) <= 0) continue;  // timeout 200ms -> cek m_running

        auto* dev = udev_monitor_receive_device(m_monitor);
        if (!dev) continue;

        const char* action_str = udev_device_get_action(dev);
        if (action_str) {
            USBDevice   usbdev = fromUdev(dev);
            USBAction   act    = (strcmp(action_str, "add") == 0)
                                 ? USBAction::ADDED : USBAction::REMOVED;
            std::lock_guard<std::mutex> lk(m_cbMutex);
            if (m_callback) m_callback(usbdev, act);
        }
        udev_device_unref(dev);
    }
}

// Enumerate device USB yang sudah terhubung saat ini
std::vector<USBDevice> USBMonitor::getConnectedDevices() {
    std::vector<USBDevice> result;
    if (!m_udev) return result;

    auto* en = udev_enumerate_new(m_udev);
    udev_enumerate_add_match_subsystem(en, "usb");
    udev_enumerate_add_match_property(en, "DEVTYPE", "usb_device");
    udev_enumerate_scan_devices(en);

    struct udev_list_entry* list = udev_enumerate_get_list_entry(en);
    struct udev_list_entry* entry;
    udev_list_entry_foreach(entry, list) {
        const char* path = udev_list_entry_get_name(entry);
        auto* dev = udev_device_new_from_syspath(m_udev, path);
        if (dev) { result.push_back(fromUdev(dev)); udev_device_unref(dev); }
    }
    udev_enumerate_unref(en);
    return result;
}

#endif // __linux__

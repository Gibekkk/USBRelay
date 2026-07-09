#include "usb_monitor.h"
#include <cstring>
#include <sstream>
#include <iomanip>
#include <cctype>

// =================================================================
// Bagian ini SATU file untuk 3 platform. Implementasi yang benar-benar
// dipakai dipilih otomatis oleh preprocessor sesuai OS target saat
// kompilasi -- tidak perlu ganti daftar source di build system per OS.
//   - Linux   : libudev          (hotplug lewat netlink)
//   - macOS   : IOKit            (hotplug lewat IOServiceAddMatchingNotification)
//   - Windows : SetupAPI + WM_DEVICECHANGE (hotplug lewat hidden window)
// =================================================================

// ---------------------------------------------------------------
// Bagian bersama (sama di semua platform)
// ---------------------------------------------------------------
void USBMonitor::setCallback(USBCallback cb) {
    std::lock_guard<std::mutex> lk(m_cbMutex);
    m_callback = cb;
}

void USBMonitor::dispatch(const USBDevice& dev, USBAction action) {
    std::lock_guard<std::mutex> lk(m_cbMutex);
    if (m_callback) m_callback(dev, action);
}

// =================================================================
// LINUX -- libudev
// =================================================================
#if defined(__linux__)

#include <libudev.h>
#include <poll.h>

struct USBMonitor::Impl {
    struct udev*         udev_ctx = nullptr;
    struct udev_monitor* monitor  = nullptr;
    std::thread          thread;
    std::atomic<bool>    running{false};
    USBMonitor*          owner = nullptr;
};

static USBDevice fromUdev(struct udev_device* dev) {
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
    d.syspath      = udev_device_get_syspath(dev) ? udev_device_get_syspath(dev) : "";
    d.devpath      = udev_device_get_devpath(dev) ? udev_device_get_devpath(dev) : "";
    return d;
}

static void udevRunLoop(USBMonitor::Impl* impl) {
    int fd = udev_monitor_get_fd(impl->monitor);
    while (impl->running) {
        struct pollfd pfd = { fd, POLLIN, 0 };
        if (poll(&pfd, 1, 200) <= 0) continue;  // timeout 200ms -> cek running

        auto* dev = udev_monitor_receive_device(impl->monitor);
        if (!dev) continue;

        const char* action_str = udev_device_get_action(dev);
        if (action_str) {
            USBDevice usbdev = fromUdev(dev);
            USBAction act    = (strcmp(action_str, "add") == 0)
                               ? USBAction::ADDED : USBAction::REMOVED;
            impl->owner->dispatch(usbdev, act);
        }
        udev_device_unref(dev);
    }
}

USBMonitor::USBMonitor() {
    m_impl = new Impl();
    m_impl->owner    = this;
    m_impl->udev_ctx = udev_new();
}

USBMonitor::~USBMonitor() {
    stop();
    if (m_impl) {
        if (m_impl->udev_ctx) udev_unref(m_impl->udev_ctx);
        delete m_impl;
    }
}

bool USBMonitor::start() {
    if (!m_impl->udev_ctx) return false;
    m_impl->monitor = udev_monitor_new_from_netlink(m_impl->udev_ctx, "udev");
    if (!m_impl->monitor) return false;
    udev_monitor_filter_add_match_subsystem_devtype(m_impl->monitor, "usb", "usb_device");
    udev_monitor_enable_receiving(m_impl->monitor);
    m_impl->running = true;
    m_impl->thread  = std::thread(udevRunLoop, m_impl);
    return true;
}

void USBMonitor::stop() {
    if (!m_impl) return;
    m_impl->running = false;
    if (m_impl->thread.joinable()) m_impl->thread.join();
    if (m_impl->monitor) { udev_monitor_unref(m_impl->monitor); m_impl->monitor = nullptr; }
}

std::vector<USBDevice> USBMonitor::getConnectedDevices() {
    std::vector<USBDevice> result;
    if (!m_impl->udev_ctx) return result;

    auto* en = udev_enumerate_new(m_impl->udev_ctx);
    udev_enumerate_add_match_subsystem(en, "usb");
    udev_enumerate_add_match_property(en, "DEVTYPE", "usb_device");
    udev_enumerate_scan_devices(en);

    struct udev_list_entry* list = udev_enumerate_get_list_entry(en);
    struct udev_list_entry* entry;
    udev_list_entry_foreach(entry, list) {
        const char* path = udev_list_entry_get_name(entry);
        auto* dev = udev_device_new_from_syspath(m_impl->udev_ctx, path);
        if (dev) { result.push_back(fromUdev(dev)); udev_device_unref(dev); }
    }
    udev_enumerate_unref(en);
    return result;
}

// =================================================================
// MACOS -- IOKit
// =================================================================
#elif defined(__APPLE__)

#include <IOKit/IOKitLib.h>
#include <IOKit/usb/IOUSBLib.h>
#include <IOKit/IOMessage.h>
#include <CoreFoundation/CoreFoundation.h>

// kIOMainPortDefault baru ada sejak SDK macOS 12 (Monterey); di SDK lebih
// lama namanya kIOMasterPortDefault. Shim ini supaya kompatibel dua-duanya.
#ifndef kIOMainPortDefault
  #define kIOMainPortDefault kIOMasterPortDefault
#endif

static std::string cfStringToStd(CFTypeRef ref) {
    if (!ref || CFGetTypeID(ref) != CFStringGetTypeID()) return "";
    CFStringRef s = (CFStringRef)ref;
    CFIndex len = CFStringGetLength(s);
    CFIndex maxSize = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
    std::string out(maxSize, '\0');
    if (CFStringGetCString(s, &out[0], maxSize, kCFStringEncodingUTF8))
        out.resize(strlen(out.c_str()));
    else
        out.clear();
    return out;
}

static std::string cfNumberToHexStd(CFTypeRef ref) {
    if (!ref || CFGetTypeID(ref) != CFNumberGetTypeID()) return "";
    int val = 0;
    CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &val);
    std::ostringstream ss;
    ss << std::hex << std::setw(4) << std::setfill('0') << val;
    return ss.str();
}

// Ambil properti IORegistry sebagai USBDevice
static USBDevice fromIOKitService(io_service_t service) {
    USBDevice d;
    CFTypeRef vid  = IORegistryEntryCreateCFProperty(service, CFSTR("idVendor"),  kCFAllocatorDefault, 0);
    CFTypeRef pid  = IORegistryEntryCreateCFProperty(service, CFSTR("idProduct"), kCFAllocatorDefault, 0);
    CFTypeRef ser  = IORegistryEntryCreateCFProperty(service, CFSTR("USB Serial Number"), kCFAllocatorDefault, 0);
    CFTypeRef man  = IORegistryEntryCreateCFProperty(service, CFSTR("USB Vendor Name"),   kCFAllocatorDefault, 0);
    CFTypeRef prod = IORegistryEntryCreateCFProperty(service, CFSTR("USB Product Name"),  kCFAllocatorDefault, 0);

    d.vid          = cfNumberToHexStd(vid);
    d.pid          = cfNumberToHexStd(pid);
    d.serial       = cfStringToStd(ser);
    d.manufacturer = cfStringToStd(man);
    d.product      = cfStringToStd(prod);

    io_string_t path = {};
    if (IORegistryEntryGetPath(service, kIOServicePlane, path) == KERN_SUCCESS)
        d.syspath = path;

    if (vid)  CFRelease(vid);
    if (pid)  CFRelease(pid);
    if (ser)  CFRelease(ser);
    if (man)  CFRelease(man);
    if (prod) CFRelease(prod);
    return d;
}

struct USBMonitor::Impl {
    USBMonitor*             owner = nullptr;
    std::thread             thread;
    std::atomic<bool>       running{false};
    CFRunLoopRef            runLoop = nullptr;
    IONotificationPortRef   notifyPort = nullptr;
    io_iterator_t           addedIter = 0;
    io_iterator_t           removedIter = 0;
};

static void onAdded(void* refcon, io_iterator_t iterator) {
    auto* impl = static_cast<USBMonitor::Impl*>(refcon);
    io_service_t svc;
    while ((svc = IOIteratorNext(iterator)) != 0) {
        USBDevice d = fromIOKitService(svc);
        impl->owner->dispatch(d, USBAction::ADDED);
        IOObjectRelease(svc);
    }
}

static void onRemoved(void* refcon, io_iterator_t iterator) {
    auto* impl = static_cast<USBMonitor::Impl*>(refcon);
    io_service_t svc;
    while ((svc = IOIteratorNext(iterator)) != 0) {
        // Device sudah lepas: sebagian besar properti tidak lagi terbaca,
        // tapi biasanya masih sempat terbaca sesaat sebelum object dilepas.
        USBDevice d = fromIOKitService(svc);
        impl->owner->dispatch(d, USBAction::REMOVED);
        IOObjectRelease(svc);
    }
}

static void iokitRunLoop(USBMonitor::Impl* impl) {
    impl->runLoop = CFRunLoopGetCurrent();
    CFRunLoopAddSource(impl->runLoop,
                        IONotificationPortGetRunLoopSource(impl->notifyPort),
                        kCFRunLoopDefaultMode);
    while (impl->running) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.2, true);
    }
}

USBMonitor::USBMonitor() {
    m_impl = new Impl();
    m_impl->owner = this;
}

USBMonitor::~USBMonitor() {
    stop();
    delete m_impl;
}

bool USBMonitor::start() {
    m_impl->notifyPort = IONotificationPortCreate(kIOMainPortDefault);
    if (!m_impl->notifyPort) return false;

    CFMutableDictionaryRef matchAdded = IOServiceMatching(kIOUSBDeviceClassName);
    CFMutableDictionaryRef matchRemoved = IOServiceMatching(kIOUSBDeviceClassName);
    if (!matchAdded || !matchRemoved) return false;

    IOServiceAddMatchingNotification(m_impl->notifyPort, kIOFirstMatchNotification,
                                      matchAdded, onAdded, m_impl, &m_impl->addedIter);
    onAdded(m_impl, m_impl->addedIter); // drain existing (arm notification)

    IOServiceAddMatchingNotification(m_impl->notifyPort, kIOTerminatedNotification,
                                      matchRemoved, onRemoved, m_impl, &m_impl->removedIter);
    onRemoved(m_impl, m_impl->removedIter); // drain existing (arm notification)

    m_impl->running = true;
    m_impl->thread  = std::thread(iokitRunLoop, m_impl);
    return true;
}

void USBMonitor::stop() {
    if (!m_impl) return;
    m_impl->running = false;
    if (m_impl->runLoop) CFRunLoopStop(m_impl->runLoop);
    if (m_impl->thread.joinable()) m_impl->thread.join();

    if (m_impl->addedIter)   { IOObjectRelease(m_impl->addedIter);   m_impl->addedIter = 0; }
    if (m_impl->removedIter) { IOObjectRelease(m_impl->removedIter); m_impl->removedIter = 0; }
    if (m_impl->notifyPort)  { IONotificationPortDestroy(m_impl->notifyPort); m_impl->notifyPort = nullptr; }
}

std::vector<USBDevice> USBMonitor::getConnectedDevices() {
    std::vector<USBDevice> result;
    CFMutableDictionaryRef match = IOServiceMatching(kIOUSBDeviceClassName);
    if (!match) return result;

    io_iterator_t iter;
    if (IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter) != KERN_SUCCESS)
        return result;

    io_service_t svc;
    while ((svc = IOIteratorNext(iter)) != 0) {
        result.push_back(fromIOKitService(svc));
        IOObjectRelease(svc);
    }
    IOObjectRelease(iter);
    return result;
}

// =================================================================
// WINDOWS -- SetupAPI + WM_DEVICECHANGE
// =================================================================
#elif defined(_WIN32)

#include <windows.h>
#include <setupapi.h>
#include <dbt.h>
#include <initguid.h>
#include <cstdio>

#pragma comment(lib, "setupapi.lib")

// GUID_DEVINTERFACE_USB_DEVICE (dari usbiodef.h, didefinisikan manual di
// sini supaya tidak wajib punya WDK terpasang untuk build)
DEFINE_GUID(GUID_DEVINTERFACE_USB_DEVICE_LOCAL,
    0xA5DCBF10L, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);

static const char* kWndClassName = "USBRelayMonitorWndClass";

struct USBMonitor::Impl {
    USBMonitor*        owner = nullptr;
    std::thread        thread;
    std::atomic<bool>  running{false};
    HWND               hwnd = nullptr;
    HDEVNOTIFY         devNotify = nullptr;
    HANDLE             readyEvent = nullptr;
};

// Parse "USB\VID_16C0&PID_05DF\5&abc..." -> vid/pid hex string lower-case
static bool parseVidPid(const std::string& instanceId, std::string& vid, std::string& pid) {
    auto vpos = instanceId.find("VID_");
    auto ppos = instanceId.find("PID_");
    if (vpos == std::string::npos || ppos == std::string::npos) return false;
    vid = instanceId.substr(vpos + 4, 4);
    pid = instanceId.substr(ppos + 4, 4);
    for (auto& c : vid) c = (char)tolower((unsigned char)c);
    for (auto& c : pid) c = (char)tolower((unsigned char)c);
    return true;
}

static USBDevice deviceFromInstanceId(const std::string& instanceId, HDEVINFO devInfo, SP_DEVINFO_DATA* devData) {
    USBDevice d;
    parseVidPid(instanceId, d.vid, d.pid);
    d.devpath = instanceId;
    d.syspath = instanceId;

    char buf[512] = {};
    if (devInfo && devData) {
        if (SetupDiGetDeviceRegistryPropertyA(devInfo, devData, SPDRP_DEVICEDESC,
                                               nullptr, (PBYTE)buf, sizeof(buf), nullptr)) {
            d.product = buf;
        }
        buf[0] = '\0';
        if (SetupDiGetDeviceRegistryPropertyA(devInfo, devData, SPDRP_MFG,
                                               nullptr, (PBYTE)buf, sizeof(buf), nullptr)) {
            d.manufacturer = buf;
        }
    }

    // Serial number: biasanya segmen terakhir path instance id setelah '\'
    auto lastSlash = instanceId.find_last_of('\\');
    if (lastSlash != std::string::npos)
        d.serial = instanceId.substr(lastSlash + 1);

    return d;
}

static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DEVICECHANGE) {
        auto* impl = reinterpret_cast<USBMonitor::Impl*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        if (impl && (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE)) {
            auto* hdr = reinterpret_cast<DEV_BROADCAST_HDR*>(lParam);
            if (hdr && hdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
                auto* devIf = reinterpret_cast<DEV_BROADCAST_DEVICEINTERFACE_A*>(hdr);
                std::string instanceId = devIf->dbcc_name;
                USBDevice d = deviceFromInstanceId(instanceId, nullptr, nullptr);
                USBAction act = (wParam == DBT_DEVICEARRIVAL) ? USBAction::ADDED : USBAction::REMOVED;
                impl->owner->dispatch(d, act);
            }
        }
        return TRUE;
    }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static void winRunLoop(USBMonitor::Impl* impl) {
    WNDCLASSA wc = {};
    wc.lpfnWndProc   = wndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = kWndClassName;
    RegisterClassA(&wc);

    impl->hwnd = CreateWindowExA(0, kWndClassName, "USBRelayMonitor", 0,
                                  0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                  GetModuleHandleA(nullptr), nullptr);
    SetWindowLongPtrA(impl->hwnd, GWLP_USERDATA, (LONG_PTR)impl);

    DEV_BROADCAST_DEVICEINTERFACE_A filter = {};
    filter.dbcc_size       = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid  = GUID_DEVINTERFACE_USB_DEVICE_LOCAL;
    impl->devNotify = RegisterDeviceNotificationA(impl->hwnd, &filter,
                                                   DEVICE_NOTIFY_WINDOW_HANDLE);

    if (impl->readyEvent) SetEvent(impl->readyEvent);

    MSG msg;
    while (impl->running) {
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        Sleep(50);
    }

    if (impl->devNotify) UnregisterDeviceNotification(impl->devNotify);
    if (impl->hwnd) DestroyWindow(impl->hwnd);
    UnregisterClassA(kWndClassName, GetModuleHandleA(nullptr));
}

USBMonitor::USBMonitor() {
    m_impl = new Impl();
    m_impl->owner = this;
    m_impl->readyEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
}

USBMonitor::~USBMonitor() {
    stop();
    if (m_impl->readyEvent) CloseHandle(m_impl->readyEvent);
    delete m_impl;
}

bool USBMonitor::start() {
    m_impl->running = true;
    m_impl->thread  = std::thread(winRunLoop, m_impl);
    if (m_impl->readyEvent) WaitForSingleObject(m_impl->readyEvent, 2000);
    return true;
}

void USBMonitor::stop() {
    if (!m_impl) return;
    m_impl->running = false;
    if (m_impl->hwnd) PostMessageA(m_impl->hwnd, WM_DESTROY, 0, 0);
    if (m_impl->thread.joinable()) m_impl->thread.join();
}

std::vector<USBDevice> USBMonitor::getConnectedDevices() {
    std::vector<USBDevice> result;
    HDEVINFO devInfo = SetupDiGetClassDevsA(&GUID_DEVINTERFACE_USB_DEVICE_LOCAL, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return result;

    SP_DEVICE_INTERFACE_DATA ifData = {};
    ifData.cbSize = sizeof(ifData);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr,
                                                   &GUID_DEVINTERFACE_USB_DEVICE_LOCAL, i, &ifData); i++) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData, nullptr, 0, &needed, nullptr);
        if (needed == 0) continue;

        std::vector<char> buf(needed);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        SP_DEVINFO_DATA devData = {};
        devData.cbSize = sizeof(devData);

        if (SetupDiGetDeviceInterfaceDetailA(devInfo, &ifData, detail, needed, nullptr, &devData)) {
            std::string instanceId = detail->DevicePath;
            result.push_back(deviceFromInstanceId(instanceId, devInfo, &devData));
        }
    }
    SetupDiDestroyDeviceInfoList(devInfo);
    return result;
}

#else
#error "Platform tidak didukung: hanya Linux, macOS, dan Windows."
#endif

#pragma once
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

// Header ini SENGAJA tidak menyertakan header khusus platform (libudev,
// IOKit, Windows SDK, dsb). Implementasi per-platform ada di
// usb_monitor.cpp, dipilih otomatis lewat #if defined(...) sesuai OS
// saat kompilasi. Dengan begini header ini aman di-include dari mana
// saja (app_core.h, gui_ui.cpp) tanpa membocorkan detail
// platform ke kode yang platform-independent.

struct USBDevice {
    std::string vid;
    std::string pid;
    std::string serial;
    std::string manufacturer;
    std::string product;
    std::string syspath;   // Linux: syspath udev. Mac/Win: identifier device (boleh kosong)
    std::string devpath;   // Linux: devpath udev.  Mac/Win: identifier device (boleh kosong)
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

    // --- Detail internal (pimpl), dipakai hanya oleh usb_monitor.cpp ---
    // Publik semata-mata supaya fungsi-fungsi bebas di usb_monitor.cpp
    // (thread runloop tiap platform) bisa memanggilnya lewat pointer
    // USBMonitor::Impl*. Bukan bagian dari API yang dimaksudkan untuk
    // dipakai kode lain (app_core, gui_ui).
    struct Impl;                // detail platform, didefinisikan di usb_monitor.cpp
    void dispatch(const USBDevice& dev, USBAction action);

private:
    Impl*                  m_impl = nullptr;
    USBCallback            m_callback;
    std::mutex             m_cbMutex;
};

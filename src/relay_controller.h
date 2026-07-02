#pragma once
#include <hidapi/hidapi.h>
#include <string>
#include <vector>
#include <cstdint>

#define USB_RELAY_VID 0x16C0
#define USB_RELAY_PID 0x05DF

struct RelayInfo {
    std::string path;
    std::string serial;
    int num_channels = 1;
};

class RelayController {
public:
    RelayController();
    ~RelayController();

    bool init();
    void cleanup();

    std::vector<RelayInfo> enumerate();
    bool openDevice(const std::string& path);
    void closeDevice();

    bool isOpen() const { return m_device != nullptr; }
    RelayInfo getInfo() const { return m_info; }

    bool setChannel(int ch, bool on);
    bool setAll(bool on);
    uint8_t getStatus();
    void setLastStatus(uint8_t s);

    // Scan semua metode baca, return log string, update m_last_status
    std::vector<std::string> scanStatus();

private:
    uint8_t tryReadMethod(int method, std::string& label);

    hid_device* m_device      = nullptr;
    RelayInfo   m_info;
    bool        m_initialized = false;
    uint8_t     m_last_status = 0;
    int         m_status_method = -1; // metode yang berhasil
};
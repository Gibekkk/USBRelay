#pragma once
#include <string>
#include <vector>

enum class RelayAction {
    OPEN_ON_CONNECT,   // nyalakan relay saat device terhubung
    CLOSE_ON_CONNECT   // matikan relay saat device terhubung
};

struct DeviceRule {
    std::string  vid;
    std::string  pid;
    int          relay_channel = 1;  // 0 = semua channel
    RelayAction  action = RelayAction::OPEN_ON_CONNECT;
    std::string  label;
};

class DeviceMapper {
public:
    bool loadConfig(const std::string& path);
    bool saveConfig(const std::string& path) const;

    void addRule(const DeviceRule& rule);
    void removeRule(int index);
    const std::vector<DeviceRule>& getRules() const { return m_rules; }

    // Cari semua aturan yang cocok dengan VID:PID
    std::vector<DeviceRule> findRules(const std::string& vid,
                                       const std::string& pid) const;

private:
    std::vector<DeviceRule> m_rules;
};

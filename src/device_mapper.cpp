#include "device_mapper.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

// Format config:
// # komentar
// VID:PID  CHANNEL  ACTION  [LABEL]
// Contoh:
// 046d:c52b  1  open_on_connect  Logitech Mouse
// 0bda:8153  2  open_on_connect  USB LAN Adapter
// 1234:5678  all  close_on_connect

bool DeviceMapper::loadConfig(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    m_rules.clear();
    std::string line;
    while (std::getline(f, line)) {
        // Hapus komentar
        auto pos = line.find('#');
        if (pos != std::string::npos) line = line.substr(0, pos);
        // Trim kanan
        while (!line.empty() && isspace((unsigned char)line.back())) line.pop_back();
        if (line.empty()) continue;

        std::istringstream ss(line);
        std::string vidpid, ch_str, act_str, label;
        ss >> vidpid >> ch_str >> act_str;
        std::getline(ss, label);
        while (!label.empty() && isspace((unsigned char)label.front()))
            label.erase(label.begin());

        auto sep = vidpid.find(':');
        if (sep == std::string::npos) continue;

        DeviceRule rule;
        rule.vid = toLower(vidpid.substr(0, sep));
        rule.pid = toLower(vidpid.substr(sep + 1));
        rule.relay_channel = (ch_str == "all") ? 0 : std::stoi(ch_str);
        rule.action = (act_str == "close_on_connect")
                      ? RelayAction::CLOSE_ON_CONNECT
                      : RelayAction::OPEN_ON_CONNECT;
        rule.label = label.empty() ? (rule.vid + ":" + rule.pid) : label;
        m_rules.push_back(rule);
    }
    return true;
}

bool DeviceMapper::saveConfig(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "# USB Relay Auto-Control - Konfigurasi Device\n"
      << "# Format: VID:PID  CHANNEL  ACTION  [LABEL]\n"
      << "# CHANNEL : 1-16 atau 'all'\n"
      << "# ACTION  : open_on_connect | close_on_connect\n\n";
    for (auto& r : m_rules) {
        std::string ch  = (r.relay_channel == 0) ? "all" : std::to_string(r.relay_channel);
        std::string act = (r.action == RelayAction::CLOSE_ON_CONNECT)
                          ? "close_on_connect" : "open_on_connect";
        f << r.vid << ":" << r.pid << "\t" << ch << "\t" << act
          << "\t" << r.label << "\n";
    }
    return true;
}

void DeviceMapper::addRule(const DeviceRule& rule) { m_rules.push_back(rule); }

void DeviceMapper::removeRule(int index) {
    if (index >= 0 && index < (int)m_rules.size())
        m_rules.erase(m_rules.begin() + index);
}

std::vector<DeviceRule> DeviceMapper::findRules(const std::string& vid,
                                                  const std::string& pid) const {
    std::vector<DeviceRule> result;
    auto v = toLower(vid), p = toLower(pid);
    for (auto& r : m_rules)
        if (r.vid == v && r.pid == p) result.push_back(r);
    return result;
}

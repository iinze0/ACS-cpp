#include "parse.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace {

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : line) {
        if (ch == ',') {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    parts.push_back(cur);
    return parts;
}

const std::regex kMac{R"(^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$)"};

}  // namespace

bool valid_mac(const std::string& mac) { return std::regex_match(mac, kMac); }

int power_key(const std::string& power) {
    try {
        return std::stoi(trim(power));
    } catch (...) {
        return -999;
    }
}

bool valid_proxy(const std::string& line) {
    static const std::regex re{R"(^(\d{1,3}(?:\.\d{1,3}){3}):(\d{2,5})$)"};
    std::smatch match;
    const auto value = trim(line);
    if (!std::regex_match(value, match, re)) return false;
    int port = 0;
    try {
        port = std::stoi(match[2].str());
    } catch (...) {
        return false;
    }
    if (port < 1 || port > 65535) return false;
    std::stringstream ip(match[1].str());
    std::string octet;
    int count = 0;
    while (std::getline(ip, octet, '.')) {
        int n = 0;
        try {
            n = std::stoi(octet);
        } catch (...) {
            return false;
        }
        if (n < 0 || n > 255) return false;
        ++count;
    }
    return count == 4;
}

std::string pick_monitor_name(const std::string& text, const std::string& iface, const std::vector<std::string>& names) {
    static const std::regex patterns[] = {
        std::regex{R"(on\s+\[[^\]]+\]([A-Za-z0-9._-]+))"},
        std::regex{R"(monitor mode enabled on ([A-Za-z0-9._-]+))"},
    };
    for (const auto& pattern : patterns) {
        auto begin = std::sregex_iterator(text.begin(), text.end(), pattern);
        auto end = std::sregex_iterator();
        if (begin != end) return (*begin)[1].str();
    }
    const std::string candidate = iface + "mon";
    if (std::find(names.begin(), names.end(), candidate) != names.end()) return candidate;
    for (const auto& name : names) {
        if (name.size() > 3 && name.ends_with("mon")) return name;
    }
    return {};
}

std::vector<Ap> parse_airodump(const std::string& text) {
    std::vector<Ap> rows;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("Station MAC", 0) == 0) break;
        if (line.rfind("BSSID,", 0) == 0) continue;
        auto bits = split_csv(line);
        if (bits.size() < 14) continue;
        std::vector<std::string> head;
        head.reserve(13);
        for (int i = 0; i < 13; ++i) head.push_back(trim(bits[static_cast<size_t>(i)]));
        if (!valid_mac(head[0])) continue;
        std::string essid;
        for (size_t i = 13; i < bits.size(); ++i) {
            if (i > 13) essid.push_back(',');
            essid += bits[i];
        }
        essid = trim(essid);
        if (!essid.empty() && essid.back() == ',') essid.pop_back();
        essid = trim(essid);
        if ((essid.empty() || essid == "ESSID") && head[3].empty()) continue;
        std::string enc;
        for (int i : {5, 6, 7}) {
            if (head[static_cast<size_t>(i)].empty()) continue;
            if (!enc.empty()) enc.push_back(' ');
            enc += head[static_cast<size_t>(i)];
        }
        rows.push_back(Ap{head[0], head[3], head[8], enc, essid});
    }
    std::sort(rows.begin(), rows.end(), [](const Ap& a, const Ap& b) { return power_key(a.power) > power_key(b.power); });
    return rows;
}

std::vector<Client> parse_clients(const std::string& text) {
    std::vector<Client> rows;
    std::istringstream input(text);
    std::string line;
    bool started = false;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("Station MAC", 0) == 0) {
            started = true;
            continue;
        }
        if (!started) continue;
        auto bits = split_csv(line);
        if (bits.size() < 6) continue;
        const auto mac = trim(bits[0]);
        if (!valid_mac(mac)) continue;
        const auto bssid = trim(bits[5]);
        std::string probes = bits.size() > 6 ? trim(bits[6]) : "";
        rows.push_back(Client{mac, trim(bits[3]), bssid, probes});
    }
    return rows;
}

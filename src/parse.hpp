#pragma once

#include <string>
#include <vector>

struct Ap {
    std::string bssid;
    std::string channel;
    std::string power;
    std::string enc;
    std::string essid;
};

struct Client {
    std::string mac;
    std::string power;
    std::string bssid;
    std::string probes;
};

std::vector<Ap> parse_airodump(const std::string& text);
std::vector<Client> parse_clients(const std::string& text);
std::string pick_monitor_name(const std::string& text, const std::string& iface, const std::vector<std::string>& names);
bool valid_mac(const std::string& mac);
bool valid_proxy(const std::string& line);
int power_key(const std::string& power);

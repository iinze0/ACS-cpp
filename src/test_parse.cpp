#include "parse.hpp"

#include <iostream>
#include <string>

int main() {
    const std::string airmon = "(mac80211 monitor mode vif enabled for [phy0]wlan0 on [phy0]wlan0mon)";
    if (pick_monitor_name(airmon, "wlan0", {}) != "wlan0mon") return 1;
    if (pick_monitor_name("monitor mode enabled on wlan1mon", "wlan1", {}) != "wlan1mon") return 1;
    if (!valid_proxy("1.2.3.4:1080") || valid_proxy("999.1.1.1:80") || valid_proxy("1.2.3.4:99999")) return 1;
    const std::string csv =
        "BSSID, First time seen, Last time seen, channel, Speed, Privacy, Cipher, Authentication, Power, # beacons, # IV, LAN IP, ID-length, ESSID, Key\n"
        "AA:BB:CC:DD:EE:01, 2020-01-01 00:00:00, 2020-01-01 00:00:01, 6, 54, WPA2, CCMP, PSK, -40, 10, 0, 0.0.0.0, 4, LabNet, \n"
        "AA:BB:CC:DD:EE:02, 2020-01-01 00:00:00, 2020-01-01 00:00:01, 11, 54, WPA2, CCMP, PSK, -70, 10, 0, 0.0.0.0, 9, Cafe, Name, \n"
        "\n"
        "Station MAC, First time seen, Last time seen, Power, # packets, BSSID, Probed ESSIDs\n"
        "11:22:33:44:55:66, 2020-01-01 00:00:00, 2020-01-01 00:00:01, -50, 3, AA:BB:CC:DD:EE:01, LabNet\n";
    auto aps = parse_airodump(csv);
    auto clients = parse_clients(csv);
    if (aps.size() != 2 || aps[0].essid != "LabNet" || aps[1].essid != "Cafe, Name") {
        std::cerr << "ap parse failed\n";
        for (const auto& ap : aps) std::cerr << ap.essid << "\n";
        return 1;
    }
    if (aps[0].power != "-40") return 1;
    if (clients.size() != 1 || clients[0].mac != "11:22:33:44:55:66" || clients[0].bssid != "AA:BB:CC:DD:EE:01") return 1;
    std::cout << "TESTS_OK\n";
    return 0;
}

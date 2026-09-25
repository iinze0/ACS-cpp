#pragma once

#include "parse.hpp"

#include <functional>
#include <string>
#include <vector>

using LogFn = std::function<void(const std::string&)>;

bool have(const std::string& command);
std::string home_dir();
std::string session_path();
std::string proxy_conf_path();

struct Session {
    std::string iface = "wlan0";
    std::string mon;
    std::string channel = "6";
    std::string bssid;
    std::string essid;
    std::string client;
    std::string cap;
    std::string wordlist = "/usr/share/wordlists/rockyou.txt";
};

Session load_session();
void save_session(const Session& session);

class Job {
public:
    void track(int pid);
    int pid();
    void stop();
    bool running() const;

private:
    int pid_ = -1;
};

int run_command(const std::vector<std::string>& args, int timeout_sec, const LogFn& log, Job* job = nullptr);
int spawn_quiet(const std::vector<std::string>& args, Job* job);
int wait_job(Job& job, int seconds);

std::vector<std::string> iw_interfaces();
std::string interface_type(const std::string& name);
bool is_monitor(const std::string& name);

struct Radio {
    bool ok = false;
    std::string iface;
    std::string mon;
};

Radio enable_monitor(std::string iface, std::string mon, const LogFn& log, Job* job);
std::string http_get(const std::string& url, const LogFn& log);
std::vector<std::string> pull_proxies(const std::string& kind, int count, const LogFn& log);
bool write_proxy_chain(const std::string& kind, const std::vector<std::string>& proxies, const LogFn& log);
std::string read_file(const std::string& path);
void ensure_dir(const std::string& path);
bool version_newer(const std::string& remote, const std::string& local);
std::string remote_version(const LogFn& log);
bool install_deb(const std::string& version, const LogFn& log);

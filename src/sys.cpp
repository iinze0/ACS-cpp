#include "sys.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>
#include <thread>

extern char** environ;

namespace fs = std::filesystem;

namespace {

std::mutex g_job_mu;

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::string shell_join(const std::vector<std::string>& args) {
    std::string out;
    for (const auto& arg : args) {
        if (!out.empty()) out.push_back(' ');
        out += arg;
    }
    return out;
}

std::vector<char*> arg_ptrs(const std::vector<std::string>& args) {
    std::vector<char*> ptrs;
    ptrs.reserve(args.size() + 1);
    for (const auto& arg : args) ptrs.push_back(const_cast<char*>(arg.c_str()));
    ptrs.push_back(nullptr);
    return ptrs;
}

int spawn_process(const std::vector<std::string>& args, int* stdout_fd, bool quiet) {
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    posix_spawn_file_actions_init(&actions);
    posix_spawnattr_init(&attr);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attr, 0);
    int devnull = open("/dev/null", O_RDWR);
    if (quiet) {
        if (devnull >= 0) {
            posix_spawn_file_actions_adddup2(&actions, devnull, STDIN_FILENO);
            posix_spawn_file_actions_adddup2(&actions, devnull, STDOUT_FILENO);
            posix_spawn_file_actions_adddup2(&actions, devnull, STDERR_FILENO);
        }
    } else if (stdout_fd) {
        posix_spawn_file_actions_adddup2(&actions, stdout_fd[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, stdout_fd[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, stdout_fd[0]);
    }
    auto argv = arg_ptrs(args);
    pid_t pid = -1;
    const int rc = posix_spawnp(&pid, argv[0], &actions, &attr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    if (devnull >= 0) close(devnull);
    if (rc != 0) return -1;
    return pid;
}

std::string json_escape(const std::string& value) {
    std::string out;
    for (char ch : value) {
        if (ch == '\\' || ch == '"') out.push_back('\\');
        if (ch == '\n') {
            out += "\\n";
            continue;
        }
        out.push_back(ch);
    }
    return out;
}

}  // namespace

bool have(const std::string& command) {
    if (command.empty()) return false;
    const char* path = ::getenv("PATH");
    std::stringstream input(path ? path : "");
    std::string dir;
    while (std::getline(input, dir, ':')) {
        if (dir.empty()) continue;
        if (::access((dir + "/" + command).c_str(), X_OK) == 0) return true;
    }
    return ::access(command.c_str(), X_OK) == 0;
}

std::string home_dir() {
    const char* home = ::getenv("HOME");
    std::string base = home && *home ? home : "/root";
    return base + "/.acs";
}

std::string session_path() { return home_dir() + "/app-session.json"; }
std::string proxy_conf_path() { return home_dir() + "/proxychains.conf"; }

void ensure_dir(const std::string& path) { fs::create_directories(path); }

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

Session load_session() {
    Session session;
    session.cap = home_dir() + "/handshake";
    const auto text = read_file(session_path());
    auto take = [&](const char* key, std::string& dest) {
        const std::regex re{std::string("\"") + key + "\"\\s*:\\s*\"([^\"]*)\""};
        std::smatch match;
        if (std::regex_search(text, match, re)) dest = match[1].str();
    };
    take("iface", session.iface);
    take("mon", session.mon);
    take("channel", session.channel);
    take("bssid", session.bssid);
    take("essid", session.essid);
    take("client", session.client);
    take("cap", session.cap);
    take("wordlist", session.wordlist);
    if (session.iface.empty()) session.iface = "wlan0";
    if (session.channel.empty()) session.channel = "6";
    if (session.cap.empty()) session.cap = home_dir() + "/handshake";
    if (session.wordlist.empty()) session.wordlist = "/usr/share/wordlists/rockyou.txt";
    return session;
}

void save_session(const Session& session) {
    ensure_dir(home_dir());
    std::ofstream out(session_path());
    out << "{\n"
        << "  \"iface\": \"" << json_escape(session.iface) << "\",\n"
        << "  \"mon\": \"" << json_escape(session.mon) << "\",\n"
        << "  \"channel\": \"" << json_escape(session.channel) << "\",\n"
        << "  \"bssid\": \"" << json_escape(session.bssid) << "\",\n"
        << "  \"essid\": \"" << json_escape(session.essid) << "\",\n"
        << "  \"client\": \"" << json_escape(session.client) << "\",\n"
        << "  \"cap\": \"" << json_escape(session.cap) << "\",\n"
        << "  \"wordlist\": \"" << json_escape(session.wordlist) << "\"\n"
        << "}\n";
}

void Job::track(int pid) {
    std::lock_guard<std::mutex> lock(g_job_mu);
    pid_ = pid;
}

int Job::pid() {
    std::lock_guard<std::mutex> lock(g_job_mu);
    return pid_;
}

void Job::stop() {
    int pid = -1;
    {
        std::lock_guard<std::mutex> lock(g_job_mu);
        pid = pid_;
    }
    if (pid > 1) ::kill(-pid, SIGTERM);
}

bool Job::running() const { return pid_ > 1; }

int run_command(const std::vector<std::string>& args, int timeout_sec, const LogFn& log, Job* job) {
    if (args.empty()) return 127;
    if (log) log("$ " + shell_join(args));
    int pipes[2] = {-1, -1};
    if (pipe(pipes) != 0) return 127;
    const int pid = spawn_process(args, pipes, false);
    close(pipes[1]);
    if (pid < 0) {
        close(pipes[0]);
        if (log) log("not found: " + args[0]);
        return 127;
    }
    if (job) job->track(pid);
    std::string pending;
    const auto started = std::chrono::steady_clock::now();
    bool timed_out = false;
    while (true) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(pipes[0], &set);
        timeval tv{0, 200000};
        const int ready = select(pipes[0] + 1, &set, nullptr, nullptr, &tv);
        if (ready > 0) {
            char buf[512];
            const ssize_t n = ::read(pipes[0], buf, sizeof(buf));
            if (n > 0) {
                pending.append(buf, buf + n);
                size_t pos = 0;
                while ((pos = pending.find('\n')) != std::string::npos) {
                    if (log) log(pending.substr(0, pos));
                    pending.erase(0, pos + 1);
                }
            } else if (n == 0) {
                break;
            }
        }
        if (timeout_sec > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
            if (elapsed >= timeout_sec) {
                timed_out = true;
                ::kill(-pid, SIGTERM);
                break;
            }
        }
        int status = 0;
        if (waitpid(pid, &status, WNOHANG) == pid) break;
    }
    if (!pending.empty() && log) log(pending);
    close(pipes[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (job) job->track(-1);
    if (timed_out && log) log("(stopped after " + std::to_string(timeout_sec) + "s)");
    if (WIFEXITED(status)) {
        const int code = WEXITSTATUS(status);
        if (log) log("(exit " + std::to_string(code) + ")");
        return code;
    }
    if (log) log("(stopped)");
    return 128;
}

int spawn_quiet(const std::vector<std::string>& args, Job* job) {
    const int pid = spawn_process(args, nullptr, true);
    if (pid > 0 && job) job->track(pid);
    return pid;
}

int wait_job(Job& job, int seconds) {
    const auto started = std::chrono::steady_clock::now();
    while (true) {
        const int pid = job.pid();
        if (pid <= 1) return 0;
        int status = 0;
        const pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            job.track(-1);
            return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
        if (seconds > 0 && elapsed >= seconds) {
            job.stop();
            waitpid(pid, &status, 0);
            job.track(-1);
            return 128;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

std::vector<std::string> iw_interfaces() {
    if (!have("iw")) return {};
    int pipes[2];
    if (pipe(pipes) != 0) return {};
    const int pid = spawn_process({"iw", "dev"}, pipes, false);
    close(pipes[1]);
    if (pid < 0) {
        close(pipes[0]);
        return {};
    }
    std::string text;
    char buf[256];
    ssize_t n = 0;
    while ((n = ::read(pipes[0], buf, sizeof(buf))) > 0) text.append(buf, buf + n);
    close(pipes[0]);
    waitpid(pid, nullptr, 0);
    std::vector<std::string> names;
    std::regex re{R"(Interface\s+(\S+))"};
    for (auto it = std::sregex_iterator(text.begin(), text.end(), re); it != std::sregex_iterator(); ++it) names.push_back((*it)[1].str());
    return names;
}

std::string interface_type(const std::string& name) {
    if (name.empty() || !have("iw")) return {};
    int pipes[2];
    if (pipe(pipes) != 0) return {};
    const int pid = spawn_process({"iw", "dev", name, "info"}, pipes, false);
    close(pipes[1]);
    if (pid < 0) {
        close(pipes[0]);
        return {};
    }
    std::string text;
    char buf[256];
    ssize_t n = 0;
    while ((n = ::read(pipes[0], buf, sizeof(buf))) > 0) text.append(buf, buf + n);
    close(pipes[0]);
    waitpid(pid, nullptr, 0);
    std::smatch match;
    std::regex re{R"(type\s+(\S+))"};
    if (std::regex_search(text, match, re)) return match[1].str();
    return {};
}

bool is_monitor(const std::string& name) {
    const auto kind = interface_type(name);
    if (!kind.empty()) return kind == "monitor";
    return name.size() > 3 && name.ends_with("mon") && fs::exists("/sys/class/net/" + name);
}

Radio enable_monitor(std::string iface, std::string mon, const LogFn& log, Job* job) {
    auto names = iw_interfaces();
    if (!mon.empty() && is_monitor(mon)) return {true, iface, mon};
    for (const auto& name : names) {
        if (!is_monitor(name)) continue;
        std::string managed = iface;
        for (const auto& other : names) {
            if (!is_monitor(other)) {
                managed = other;
                break;
            }
        }
        if (log) log("already in monitor mode: " + name);
        return {true, managed, name};
    }
    std::vector<std::string> managed;
    for (const auto& name : names) {
        if (!is_monitor(name)) managed.push_back(name);
    }
    if (std::find(managed.begin(), managed.end(), iface) == managed.end()) iface = managed.empty() ? "" : managed.front();
    if (iface.empty()) {
        if (log) log("no wireless interface");
        return {};
    }
    if (log) log("auto monitor on " + iface);
    std::string started;
    run_command({"airmon-ng", "check", "kill"}, 20, log, job);
    run_command({"airmon-ng", "start", iface}, 30, [&](const std::string& line) {
        if (log) log(line);
        if (line.rfind("$ ", 0) != 0 && line.rfind("(exit", 0) != 0 && line.rfind("(stopped", 0) != 0) started += line + "\n";
    }, job);
    names = iw_interfaces();
    auto found = pick_monitor_name(started, iface, names);
    if (found.empty() && is_monitor(iface)) found = iface;
    if (found.empty()) {
        for (const auto& name : names) {
            if (is_monitor(name)) {
                found = name;
                break;
            }
        }
    }
    if (found.empty()) {
        if (log) log("monitor interface did not come up");
        return {};
    }
    if (log) log("monitor up: " + found);
    return {true, iface, found};
}

std::string http_get(const std::string& url, const LogFn& log) {
    if (!have("curl")) {
        if (log) log("missing curl");
        return {};
    }
    int pipes[2];
    if (pipe(pipes) != 0) return {};
    const int pid = spawn_process({"curl", "-fsSL", "--max-time", "25", "-A", "acs-cpp", url}, pipes, false);
    close(pipes[1]);
    if (pid < 0) {
        close(pipes[0]);
        return {};
    }
    std::string text;
    char buf[1024];
    ssize_t n = 0;
    while ((n = ::read(pipes[0], buf, sizeof(buf))) > 0) text.append(buf, buf + n);
    close(pipes[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return {};
    return text;
}

std::vector<std::string> pull_proxies(const std::string& kind, int count, const LogFn& log) {
    const std::vector<std::string> socks5 = {
        "https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/socks5.txt",
        "https://raw.githubusercontent.com/monosans/proxy-list/main/proxies/socks5.txt",
        "https://raw.githubusercontent.com/hookzof/socks5_list/master/proxy.txt",
    };
    const std::vector<std::string> socks4 = {
        "https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/socks4.txt",
        "https://raw.githubusercontent.com/monosans/proxy-list/main/proxies/socks4.txt",
    };
    const std::vector<std::string> http = {
        "https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/http.txt",
        "https://raw.githubusercontent.com/monosans/proxy-list/main/proxies/http.txt",
    };
    const auto& urls = kind == "socks4" ? socks4 : kind == "http" ? http : socks5;
    std::vector<std::string> found;
    for (const auto& url : urls) {
        if (log) log("GET " + url);
        const auto text = http_get(url, log);
        if (text.empty()) {
            if (log) log("failed " + url);
            continue;
        }
        std::istringstream input(text);
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (valid_proxy(line)) found.push_back(trim(line));
        }
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    if (count < 1) count = 1;
    if (count > 40) count = 40;
    std::vector<std::string> chosen;
    if (!found.empty()) {
        const size_t step = std::max<size_t>(1, found.size() / static_cast<size_t>(count));
        for (size_t i = 0; i < found.size() && chosen.size() < static_cast<size_t>(count); i += step) chosen.push_back(found[i]);
    }
    if (log) log(std::to_string(found.size()) + " unique from GitHub, chain uses " + std::to_string(chosen.size()));
    return chosen;
}

bool write_proxy_chain(const std::string& kind, const std::vector<std::string>& proxies, const LogFn& log) {
    if (proxies.empty()) {
        if (log) log("pull proxies first");
        return false;
    }
    ensure_dir(home_dir());
    std::ofstream out(proxy_conf_path());
    out << "# ACS proxy chain — IPs pulled from GitHub\n"
        << "dynamic_chain\nproxy_dns\ntcp_read_time_out 15000\ntcp_connect_time_out 8000\n[ProxyList]\n";
    for (const auto& item : proxies) {
        const auto colon = item.find(':');
        if (colon == std::string::npos) continue;
        out << kind << " " << item.substr(0, colon) << " " << item.substr(colon + 1) << "\n";
    }
    if (log) log("wrote " + proxy_conf_path());
    return true;
}

bool version_newer(const std::string& remote, const std::string& local) {
    auto parts = [](const std::string& text) {
        std::vector<int> out;
        std::stringstream input(text);
        std::string piece;
        while (std::getline(input, piece, '.')) {
            if (piece.empty() || !std::all_of(piece.begin(), piece.end(), ::isdigit)) break;
            out.push_back(std::stoi(piece));
        }
        return out;
    };
    const auto a = parts(remote);
    const auto b = parts(local);
    const size_t n = std::max(a.size(), b.size());
    for (size_t i = 0; i < n; ++i) {
        const int av = i < a.size() ? a[i] : 0;
        const int bv = i < b.size() ? b[i] : 0;
        if (av != bv) return av > bv;
    }
    return false;
}

std::string remote_version(const LogFn& log) {
    auto text = http_get("https://raw.githubusercontent.com/iinze0/ACS-cpp/main/VERSION", log);
    text = trim(text);
    if (!text.empty() && text.size() < 16) return text;
    return {};
}

bool install_deb(const std::string& version, const LogFn& log) {
    if (::geteuid() != 0) return false;
    const std::string url = "https://github.com/iinze0/ACS-cpp/releases/download/v" + version + "/acs-cpp_" + version + "_amd64.deb";
    const std::string dest = "/tmp/acs-cpp_" + version + "_amd64.deb";
    if (log) log("downloading " + url);
    const int code = run_command({"curl", "-fL", "--retry", "3", "--max-time", "60", "-A", "acs-cpp", "-o", dest, url}, 70, log, nullptr);
    if (code != 0) return false;
    if (run_command({"dpkg-deb", "-I", dest}, 15, LogFn{}, nullptr) != 0) return false;
    return run_command({"dpkg", "-i", dest}, 60, log, nullptr) == 0;
}

#include "sys.hpp"
#include "version.hpp"

#include <gtk/gtk.h>

#include <strings.h>

#include <atomic>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace fs = std::filesystem;

namespace {

constexpr const char* kCss = R"(
window, box, grid, scrolledwindow, viewport, stack { background: #0b0e0c; color: #e7f6ee; }
.sidebar { background: #121815; }
.topbar { background: #121815; }
label { color: #e7f6ee; }
label.muted { color: #8aa394; }
label.accent { color: #3dff8a; }
label.title { font-size: 22px; font-weight: 700; }
label.section { font-size: 13px; font-weight: 700; }
label.kicker { color: #8aa394; font-size: 10px; letter-spacing: 1px; }
.chip { background: #1c2620; color: #e7f6ee; padding: 4px 10px; border-radius: 999px; }
.chip.on { color: #3dff8a; }
.card { background: #171e1a; border-radius: 10px; border: 1px solid #24332b; padding: 8px; }
entry { background: #0b0e0c; color: #e7f6ee; caret-color: #3dff8a; border-radius: 6px; padding: 6px; }
button { background: #1c2620; color: #e7f6ee; border-radius: 8px; padding: 8px 14px; border: none; }
button.suggested-action { background: #3dff8a; color: #0b0e0c; font-weight: 700; }
button.danger { background: #3a1818; color: #ff8d8d; font-weight: 700; }
button.nav { background: transparent; color: #e7f6ee; border-radius: 8px; padding: 10px 14px; }
button.nav-on { background: #3dff8a; color: #0b0e0c; font-weight: 700; }
textview, textview text { background: #090c0a; color: #e7f6ee; font-family: "DejaVu Sans Mono"; }
treeview { background: #171e1a; color: #e7f6ee; }
treeview:selected, treeview:selected:focus { background: #163328; color: #3dff8a; }
treeview header button { background: #1c2620; color: #3dff8a; }
radiobutton label { color: #e7f6ee; }
)";

struct App {
    GtkWidget* window = nullptr;
    GtkLabel* chip_iface = nullptr;
    GtkLabel* chip_mon = nullptr;
    GtkLabel* chip_target = nullptr;
    GtkLabel* status = nullptr;
    GtkLabel* target_line = nullptr;
    GtkLabel* ap_count = nullptr;
    GtkEntry* iface = nullptr;
    GtkEntry* mon = nullptr;
    GtkEntry* channel = nullptr;
    GtkEntry* bssid = nullptr;
    GtkEntry* essid = nullptr;
    GtkEntry* client = nullptr;
    GtkEntry* cap = nullptr;
    GtkEntry* wordlist = nullptr;
    GtkEntry* hops = nullptr;
    GtkRadioButton* sec8 = nullptr;
    GtkRadioButton* sec15 = nullptr;
    GtkRadioButton* sec30 = nullptr;
    GtkRadioButton* socks5 = nullptr;
    GtkRadioButton* socks4 = nullptr;
    GtkRadioButton* http = nullptr;
    GtkListStore* aps = nullptr;
    GtkListStore* clients_store = nullptr;
    GtkWidget* nav[5] = {};
    GtkStack* stack = nullptr;
    GtkTextBuffer* log_buf = nullptr;
    std::vector<Client> clients;
    std::vector<std::string> proxies;
    std::mutex log_mu;
    std::vector<std::string> log_q;
    std::atomic<bool> busy{false};
    Job job;

    static const char* pages[5];

    std::string entry(GtkEntry* widget) const {
        const char* text = gtk_entry_get_text(widget);
        return text ? text : "";
    }

    Session read() const {
        Session session;
        session.iface = entry(iface);
        session.mon = entry(mon);
        session.channel = entry(channel);
        session.bssid = entry(bssid);
        session.essid = entry(essid);
        session.client = entry(client);
        session.cap = entry(cap);
        session.wordlist = entry(wordlist);
        return session;
    }

    void write_log(const std::string& line) {
        std::lock_guard<std::mutex> lock(log_mu);
        log_q.push_back(line);
    }

    void flush_log() {
        std::vector<std::string> lines;
        {
            std::lock_guard<std::mutex> lock(log_mu);
            lines.swap(log_q);
        }
        for (const auto& line : lines) {
            const char* tag = "ok";
            if (line.rfind("$", 0) == 0 || line.rfind("GET ", 0) == 0) tag = "cmd";
            else if (line.find("missing") != std::string::npos || line.find("fail") != std::string::npos ||
                     line.find("not found") != std::string::npos || line.find("error") != std::string::npos) {
                tag = "bad";
            }
            GtkTextIter end;
            gtk_text_buffer_get_end_iter(log_buf, &end);
            gtk_text_buffer_insert_with_tags_by_name(log_buf, &end, (line + "\n").c_str(), -1, tag, nullptr);
            gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(g_object_get_data(G_OBJECT(log_buf), "view")), &end, 0, FALSE, 0, 1);
        }
    }

    void refresh() {
        const auto session = read();
        gtk_label_set_text(chip_iface, session.iface.empty() ? "no iface" : session.iface.c_str());
        if (session.mon.empty()) {
            gtk_label_set_text(chip_mon, "managed");
            gtk_style_context_remove_class(gtk_widget_get_style_context(GTK_WIDGET(chip_mon)), "on");
        } else {
            gtk_label_set_text(chip_mon, session.mon.c_str());
            gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(chip_mon)), "on");
        }
        const auto name = !session.essid.empty() ? session.essid : !session.bssid.empty() ? session.bssid : "no target";
        gtk_label_set_text(chip_target, name.c_str());
        const auto line = name + "    channel " + (session.channel.empty() ? "—" : session.channel) + "    " +
                          (session.bssid.empty() ? "no BSSID" : session.bssid);
        gtk_label_set_text(target_line, line.c_str());
    }

    void set_status(const char* text, bool warn = false) {
        gtk_label_set_text(status, text);
        auto* ctx = gtk_widget_get_style_context(GTK_WIDGET(status));
        if (warn) gtk_style_context_add_class(ctx, "accent");
        else gtk_style_context_remove_class(ctx, "accent");
    }

    bool begin_job() {
        if (busy.load()) {
            write_log("something is already running — press Stop");
            flush_log();
            return false;
        }
        busy = true;
        set_status("working…", true);
        return true;
    }

    void end_job() {
        busy = false;
        job.track(-1);
        set_status((std::string("v") + kVersion).c_str());
    }

    void show_page(int index) {
        gtk_stack_set_visible_child_name(stack, pages[index]);
        for (int i = 0; i < 5; ++i) {
            auto* ctx = gtk_widget_get_style_context(nav[i]);
            if (i == index) {
                gtk_style_context_add_class(ctx, "nav-on");
                gtk_style_context_remove_class(ctx, "nav");
            } else {
                gtk_style_context_add_class(ctx, "nav");
                gtk_style_context_remove_class(ctx, "nav-on");
            }
        }
    }

    void save_clicked() {
        save_session(read());
        refresh();
        write_log("saved session");
        flush_log();
    }

    void apply_radio(const Radio& radio) {
        if (!radio.iface.empty()) gtk_entry_set_text(iface, radio.iface.c_str());
        gtk_entry_set_text(mon, radio.mon.c_str());
        refresh();
        save_session(read());
    }

    int scan_seconds() const {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sec8))) return 8;
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sec30))) return 30;
        return 15;
    }

    std::string proxy_kind() const {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(socks4))) return "socks4";
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(http))) return "http";
        return "socks5";
    }

    bool need(const char* command) {
        if (have(command)) return true;
        write_log(std::string("missing ") + command + " — use Install tools");
        flush_log();
        return false;
    }

    void detect() {
        if (!need("iw")) return;
        const auto names = iw_interfaces();
        if (names.empty()) {
            write_log("no wireless interface");
            flush_log();
            return;
        }
        std::string mon_name;
        std::string managed;
        for (const auto& name : names) {
            if (is_monitor(name) && mon_name.empty()) mon_name = name;
            else if (!is_monitor(name) && managed.empty()) managed = name;
        }
        gtk_entry_set_text(iface, (managed.empty() ? names.front() : managed).c_str());
        gtk_entry_set_text(mon, mon_name.c_str());
        refresh();
        save_session(read());
        std::string joined;
        for (const auto& name : names) {
            if (!joined.empty()) joined += ", ";
            joined += name;
        }
        write_log("found " + joined);
        flush_log();
    }

    void monitor() {
        if (!begin_job()) return;
        if (!need("airmon-ng") || !need("iw")) {
            end_job();
            return;
        }
        const auto session = read();
        if (!gtk_dialog_run_question(window, "Enable monitor mode? This drops your Wi-Fi.")) {
            end_job();
            return;
        }
        std::thread([this, session] {
            auto radio = enable_monitor(session.iface, session.mon, [this](const std::string& line) { write_log(line); }, &job);
            g_idle_add(+[](gpointer data) -> gboolean {
                auto* packed = static_cast<std::pair<App*, Radio>*>(data);
                if (packed->second.ok) packed->first->apply_radio(packed->second);
                packed->first->end_job();
                packed->first->flush_log();
                delete packed;
                return G_SOURCE_REMOVE;
            }, new std::pair<App*, Radio>(this, radio));
        }).detach();
    }

    void restore() {
        auto session = read();
        const auto mon_name = session.mon.empty() ? session.iface + "mon" : session.mon;
        if (!begin_job()) return;
        std::thread([this, mon_name] {
            run_command({"airmon-ng", "stop", mon_name}, 20, [this](const std::string& line) { write_log(line); }, &job);
            spawn_quiet({"systemctl", "start", "NetworkManager"}, nullptr);
            g_idle_add(+[](gpointer data) -> gboolean {
                auto* self = static_cast<App*>(data);
                gtk_entry_set_text(self->mon, "");
                self->refresh();
                save_session(self->read());
                self->end_job();
                self->flush_log();
                return G_SOURCE_REMOVE;
            }, this);
        }).detach();
    }

    void install_tools() {
        if (!gtk_dialog_run_question(window, "Install aircrack-ng, hashcat, hcx tools, and proxychains4?")) return;
        if (!begin_job()) return;
        std::thread([this] {
            run_command({"apt-get", "install", "-y", "aircrack-ng", "hashcat", "hcxdumptool", "hcxtools", "proxychains4", "curl", "iw", "wireless-tools"},
                        0, [this](const std::string& line) { write_log(line); }, &job);
            g_idle_add(+[](gpointer data) -> gboolean {
                auto* self = static_cast<App*>(data);
                self->end_job();
                self->flush_log();
                return G_SOURCE_REMOVE;
            }, this);
        }).detach();
    }

    void scan() {
        if (!begin_job()) return;
        if (!need("airodump-ng") || !need("airmon-ng") || !need("iw")) {
            end_job();
            return;
        }
        const auto session = read();
        const int seconds = scan_seconds();
        std::thread([this, session, seconds] {
            auto radio = enable_monitor(session.iface, session.mon, [this](const std::string& line) { write_log(line); }, &job);
            if (!radio.ok) {
                g_idle_add(+[](gpointer data) -> gboolean {
                    auto* self = static_cast<App*>(data);
                    self->end_job();
                    self->flush_log();
                    return G_SOURCE_REMOVE;
                }, this);
                return;
            }
            ensure_dir(home_dir());
            std::error_code ec;
            for (const auto& entry : fs::directory_iterator(home_dir(), ec)) {
                const auto name = entry.path().filename().string();
                if (name.rfind("scan-", 0) == 0) fs::remove(entry.path(), ec);
            }
            const auto prefix = home_dir() + "/scan";
            write_log("scanning " + radio.mon + " for " + std::to_string(seconds) + "s");
            const int pid = spawn_quiet({"airodump-ng", "--band", "abg", "--output-format", "csv", "-w", prefix, radio.mon}, &job);
            if (pid > 0) wait_job(job, seconds);
            std::string newest;
            fs::file_time_type stamp{};
            bool found = false;
            for (const auto& entry : fs::directory_iterator(home_dir(), ec)) {
                if (entry.path().filename().string().rfind("scan-", 0) != 0 || entry.path().extension() != ".csv") continue;
                if (entry.path().string().find("kismet") != std::string::npos) continue;
                auto when = entry.last_write_time();
                if (!found || when > stamp) {
                    newest = entry.path().string();
                    stamp = when;
                    found = true;
                }
            }
            auto* payload = new ScanPayload{this, radio, found ? parse_airodump(read_file(newest)) : std::vector<Ap>{},
                                             found ? parse_clients(read_file(newest)) : std::vector<Client>{}};
            g_idle_add(&App::deliver_scan, payload);
        }).detach();
    }

    struct ScanPayload {
        App* app;
        Radio radio;
        std::vector<Ap> aps;
        std::vector<Client> clients;
    };

    static gboolean deliver_scan(gpointer data) {
        auto* payload = static_cast<ScanPayload*>(data);
        auto* self = payload->app;
        self->apply_radio(payload->radio);
        gtk_list_store_clear(self->aps);
        gtk_list_store_clear(self->clients_store);
        self->clients = payload->clients;
        for (const auto& ap : payload->aps) {
            GtkTreeIter iter;
            gtk_list_store_append(self->aps, &iter);
            gtk_list_store_set(self->aps, &iter, 0, ap.bssid.c_str(), 1, ap.channel.c_str(), 2, ap.power.c_str(), 3, ap.enc.c_str(), 4,
                               ap.essid.c_str(), -1);
        }
        gtk_label_set_text(self->ap_count, (std::to_string(payload->aps.size()) + " networks").c_str());
        self->write_log(std::to_string(payload->aps.size()) + " access points, " + std::to_string(payload->clients.size()) + " clients");
        self->end_job();
        self->flush_log();
        delete payload;
        return G_SOURCE_REMOVE;
    }

    void pick_ap() {
        auto* sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(g_object_get_data(G_OBJECT(aps), "view")));
        GtkTreeIter iter;
        if (!gtk_tree_selection_get_selected(sel, nullptr, &iter)) return;
        gchar* bssid = nullptr;
        gchar* channel = nullptr;
        gchar* essid = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(aps), &iter, 0, &bssid, 1, &channel, 4, &essid, -1);
        gtk_entry_set_text(this->bssid, bssid ? bssid : "");
        gtk_entry_set_text(this->channel, channel ? channel : "");
        gtk_entry_set_text(this->essid, essid ? essid : "");
        gtk_list_store_clear(clients_store);
        int matched = 0;
        std::string only;
        for (const auto& client_row : clients) {
            if (bssid && strcasecmp(client_row.bssid.c_str(), bssid) != 0) continue;
            GtkTreeIter row;
            gtk_list_store_append(clients_store, &row);
            gtk_list_store_set(clients_store, &row, 0, client_row.mac.c_str(), 1, client_row.power.c_str(), 2, client_row.probes.c_str(), -1);
            ++matched;
            only = client_row.mac;
        }
        if (matched == 1) gtk_entry_set_text(client, only.c_str());
        refresh();
        save_session(read());
        write_log(std::string("locked ") + (essid && *essid ? essid : bssid ? bssid : "") + " ch " + (channel ? channel : ""));
        flush_log();
        g_free(bssid);
        g_free(channel);
        g_free(essid);
    }

    void pick_client() {
        auto* view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(clients_store), "view"));
        GtkTreeIter iter;
        if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view), nullptr, &iter)) return;
        gchar* mac = nullptr;
        gtk_tree_model_get(GTK_TREE_MODEL(clients_store), &iter, 0, &mac, -1);
        if (mac) gtk_entry_set_text(client, mac);
        refresh();
        save_session(read());
        write_log(std::string("client ") + (mac ? mac : ""));
        flush_log();
        g_free(mac);
    }

    std::optional<std::string> need_mon() {
        auto session = read();
        if (!session.mon.empty() && is_monitor(session.mon)) return session.mon;
        for (const auto& name : iw_interfaces()) {
            if (!is_monitor(name)) continue;
            gtk_entry_set_text(mon, name.c_str());
            refresh();
            return name;
        }
        write_log("no monitor interface — scan once to turn it on");
        flush_log();
        return std::nullopt;
    }

    void deauth() {
        auto mon_name = need_mon();
        auto session = read();
        if (!mon_name || !valid_mac(session.bssid) || !need("aireplay-ng") || !begin_job()) {
            if (!valid_mac(session.bssid)) write_log("set a real BSSID (scan and click an AP)");
            flush_log();
            return;
        }
        std::vector<std::string> cmd = {"aireplay-ng", "--deauth", "8", "-a", session.bssid};
        if (valid_mac(session.client)) {
            cmd.push_back("-c");
            cmd.push_back(session.client);
        }
        cmd.push_back(*mon_name);
        std::thread([this, cmd] {
            run_command(cmd, 25, [this](const std::string& line) { write_log(line); }, &job);
            g_idle_add(&App::finish_job, this);
        }).detach();
    }

    void fakeauth() {
        auto mon_name = need_mon();
        auto session = read();
        if (!mon_name || !valid_mac(session.bssid) || !need("aireplay-ng") || !begin_job()) {
            flush_log();
            return;
        }
        const auto essid_name = session.essid.empty() ? "lab" : session.essid;
        std::thread([this, mon_name, session, essid_name] {
            run_command({"aireplay-ng", "--fakeauth", "0", "-a", session.bssid, "-e", essid_name, *mon_name}, 20,
                        [this](const std::string& line) { write_log(line); }, &job);
            g_idle_add(&App::finish_job, this);
        }).detach();
    }

    void handshake() {
        auto mon_name = need_mon();
        auto session = read();
        if (!mon_name || !valid_mac(session.bssid) || !need("airodump-ng") || !need("aireplay-ng") || !begin_job()) {
            flush_log();
            return;
        }
        std::thread([this, mon_name, session] {
            write_log("capturing handshake for 20s");
            const int pid = spawn_quiet({"airodump-ng", "-c", session.channel.empty() ? "6" : session.channel, "--bssid", session.bssid, "-w",
                                          session.cap, *mon_name},
                                         &job);
            std::vector<std::string> deauth = {"aireplay-ng", "--deauth", "6", "-a", session.bssid};
            if (valid_mac(session.client)) {
                deauth.push_back("-c");
                deauth.push_back(session.client);
            }
            deauth.push_back(*mon_name);
            run_command(deauth, 20, [this](const std::string& line) { write_log(line); }, nullptr);
            if (pid > 0) {
                job.stop();
                wait_job(job, 3);
            }
            auto cap = latest_capture(session.cap);
            if (!cap) {
                write_log("no capture written");
            } else if (fs::is_regular_file(session.wordlist) && have("aircrack-ng")) {
                write_log("cracking " + cap->filename().string());
                run_command({"aircrack-ng", "-w", session.wordlist, "-b", session.bssid, cap->string()}, 0,
                            [this](const std::string& line) { write_log(line); }, &job);
            } else {
                write_log("saved " + cap->string());
            }
            g_idle_add(&App::finish_job, this);
        }).detach();
    }

    void crack() {
        auto session = read();
        if (!need("aircrack-ng") || !begin_job()) return;
        auto cap = latest_capture(session.cap);
        if (!cap) {
            write_log("no capture yet");
            end_job();
            flush_log();
            return;
        }
        if (!fs::is_regular_file(session.wordlist)) {
            write_log("wordlist missing: " + session.wordlist);
            end_job();
            flush_log();
            return;
        }
        std::vector<std::string> cmd = {"aircrack-ng", "-w", session.wordlist};
        if (valid_mac(session.bssid)) {
            cmd.push_back("-b");
            cmd.push_back(session.bssid);
        }
        cmd.push_back(cap->string());
        std::thread([this, cmd] {
            run_command(cmd, 0, [this](const std::string& line) { write_log(line); }, &job);
            g_idle_add(&App::finish_job, this);
        }).detach();
    }

    void hcx() {
        auto mon_name = need_mon();
        if (!mon_name || !need("hcxdumptool") || !begin_job()) return;
        const auto out = home_dir() + "/hcx.pcapng";
        std::thread([this, mon_name, out] {
            run_command({"hcxdumptool", "-i", *mon_name, "-w", out, "--rds=1"}, 25, [this](const std::string& line) { write_log(line); }, &job);
            g_idle_add(&App::finish_job, this);
        }).detach();
    }

    void hashcat() {
        if (!need("hcxpcapngtool") || !need("hashcat") || !begin_job()) return;
        const auto pcap = home_dir() + "/hcx.pcapng";
        const auto session = read();
        if (!fs::is_regular_file(pcap)) {
            write_log("no hcx.pcapng — run hcx capture first");
            end_job();
            flush_log();
            return;
        }
        if (!fs::is_regular_file(session.wordlist)) {
            write_log("wordlist missing: " + session.wordlist);
            end_job();
            flush_log();
            return;
        }
        std::thread([this, pcap, session] {
            const auto digest = home_dir() + "/hash.hc22000";
            run_command({"hcxpcapngtool", "-o", digest, pcap}, 40, [this](const std::string& line) { write_log(line); }, &job);
            std::error_code ec;
            if (fs::is_regular_file(digest, ec) && fs::file_size(digest, ec) > 0) {
                run_command({"hashcat", "-m", "22000", digest, session.wordlist, "--force"}, 0,
                            [this](const std::string& line) { write_log(line); }, &job);
            } else {
                write_log("no hashes in that capture");
            }
            g_idle_add(&App::finish_job, this);
        }).detach();
    }

    void pull() {
        if (!begin_job()) return;
        const auto kind = proxy_kind();
        int count = 8;
        try {
            count = std::stoi(entry(hops));
        } catch (...) {
        }
        std::thread([this, kind, count] {
            auto chosen = pull_proxies(kind, count, [this](const std::string& line) { write_log(line); });
            auto* payload = new ProxyPayload{this, std::move(chosen)};
            g_idle_add(&App::deliver_proxies, payload);
        }).detach();
    }

    struct ProxyPayload {
        App* app;
        std::vector<std::string> proxies;
    };

    static gboolean deliver_proxies(gpointer data) {
        auto* payload = static_cast<ProxyPayload*>(data);
        payload->app->proxies = payload->proxies;
        auto* store = GTK_LIST_STORE(g_object_get_data(G_OBJECT(payload->app->window), "proxies"));
        gtk_list_store_clear(store);
        for (const auto& item : payload->proxies) {
            GtkTreeIter iter;
            gtk_list_store_append(store, &iter);
            gtk_list_store_set(store, &iter, 0, item.c_str(), -1);
        }
        payload->app->end_job();
        payload->app->flush_log();
        delete payload;
        return G_SOURCE_REMOVE;
    }

    void save_chain() {
        if (!write_proxy_chain(proxy_kind(), proxies, [this](const std::string& line) { write_log(line); })) flush_log();
        else flush_log();
    }

    void test_chain() {
        if (!fs::is_regular_file(proxy_conf_path())) {
            write_log("save a chain first");
            flush_log();
            return;
        }
        const char* binary = have("proxychains4") ? "proxychains4" : "proxychains";
        if (!have(binary) || !begin_job()) {
            if (!have(binary)) write_log("proxychains4 is not installed");
            flush_log();
            return;
        }
        std::thread([this, binary] {
            run_command({binary, "-f", proxy_conf_path(), "curl", "-fsSL", "--max-time", "25", "https://ifconfig.me"}, 40,
                        [this](const std::string& line) { write_log(line); }, &job);
            g_idle_add(&App::finish_job, this);
        }).detach();
    }

    void check_update(bool interactive) {
        if (interactive && !begin_job()) return;
        std::thread([this, interactive] {
            const auto remote = remote_version([this](const std::string& line) { write_log(line); });
            std::string message = "Could not reach GitHub.";
            bool restart = false;
            if (remote.empty()) {
                message = "Could not reach GitHub.";
            } else if (!version_newer(remote, kVersion)) {
                message = std::string("Already on ") + kVersion + ".";
            } else if (install_deb(remote, [this](const std::string& line) { write_log(line); })) {
                message = "Updated to " + remote + ". Restarting.";
                restart = true;
            } else {
                message = "Update failed and was not installed.";
            }
            if (!interactive) {
                g_idle_add(+[](gpointer data) -> gboolean {
                    auto* payload = static_cast<UpdatePayload*>(data);
                    payload->app->flush_log();
                    if (payload->restart) ::execl("/usr/bin/ACS-cpp", "ACS-cpp", nullptr);
                    delete payload;
                    return G_SOURCE_REMOVE;
                }, new UpdatePayload{this, message, restart});
                return;
            }
            g_idle_add(&App::deliver_update, new UpdatePayload{this, message, restart});
        }).detach();
    }

    struct UpdatePayload {
        App* app;
        std::string message;
        bool restart;
    };

    static gboolean deliver_update(gpointer data) {
        auto* payload = static_cast<UpdatePayload*>(data);
        payload->app->end_job();
        payload->app->flush_log();
        gtk_dialog_info(payload->app->window, payload->message.c_str());
        if (payload->restart) {
            ::execl("/usr/bin/ACS-cpp", "ACS-cpp", nullptr);
        }
        delete payload;
        return G_SOURCE_REMOVE;
    }

    static gboolean finish_job(gpointer data) {
        auto* self = static_cast<App*>(data);
        self->end_job();
        self->flush_log();
        return G_SOURCE_REMOVE;
    }

    static std::optional<fs::path> latest_capture(const std::string& cap) {
        fs::path base(cap.empty() ? home_dir() + "/handshake" : cap);
        std::error_code ec;
        if (!fs::exists(base.parent_path(), ec)) return std::nullopt;
        fs::path best;
        fs::file_time_type stamp{};
        bool found = false;
        const auto prefix = base.filename().string() + "-";
        for (const auto& entry : fs::directory_iterator(base.parent_path(), ec)) {
            const auto name = entry.path().filename().string();
            if (name.rfind(prefix, 0) != 0 || entry.path().extension() != ".cap") continue;
            auto when = entry.last_write_time();
            if (!found || when > stamp) {
                best = entry.path();
                stamp = when;
                found = true;
            }
        }
        return found ? std::optional<fs::path>(best) : std::nullopt;
    }

    static gboolean gtk_dialog_run_question(GtkWidget* parent, const char* text) {
        auto* dialog = gtk_message_dialog_new(GTK_WINDOW(parent), GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "%s", text);
        const auto response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return response == GTK_RESPONSE_YES;
    }

    static void gtk_dialog_info(GtkWidget* parent, const char* text) {
        auto* dialog = gtk_message_dialog_new(GTK_WINDOW(parent), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", text);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    }
};

const char* App::pages[5] = {"Station", "Scan", "Attack", "Crack", "Proxy"};

GtkWidget* labeled_entry(GtkWidget* box, const char* caption, GtkEntry** out, const std::string& value) {
    auto* label = gtk_label_new(caption);
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "kicker");
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 2);
    auto* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), value.c_str());
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 2);
    *out = GTK_ENTRY(entry);
    return entry;
}

GtkWidget* button(const char* label, const char* cls) {
    auto* widget = gtk_button_new_with_label(label);
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), cls);
    return widget;
}

GtkWidget* card() {
    auto* frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(frame), "card");
    gtk_widget_set_margin_bottom(frame, 8);
    return frame;
}

void add_action(GtkWidget* page, const char* title, const char* body, GtkWidget* action) {
    auto* box = card();
    auto* heading = gtk_label_new(title);
    gtk_style_context_add_class(gtk_widget_get_style_context(heading), "section");
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    auto* copy = gtk_label_new(body);
    gtk_label_set_line_wrap(GTK_LABEL(copy), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(copy), "muted");
    gtk_widget_set_halign(copy, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), copy, FALSE, FALSE, 0);
    gtk_widget_set_halign(action, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), action, FALSE, FALSE, 2);
    gtk_box_pack_start(GTK_BOX(page), box, FALSE, FALSE, 0);
}

GtkWidget* make_tree(GtkListStore* store, std::initializer_list<std::pair<const char*, int>> columns) {
    auto* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), TRUE);
    int col = 0;
    for (const auto& [title, width] : columns) {
        auto* renderer = gtk_cell_renderer_text_new();
        auto* column = gtk_tree_view_column_new_with_attributes(title, renderer, "text", col, nullptr);
        gtk_tree_view_column_set_min_width(column, width);
        gtk_tree_view_column_set_expand(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
        ++col;
    }
    g_object_set_data(G_OBJECT(store), "view", view);
    return view;
}

void build(App* app) {
    auto session = load_session();
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), (std::string("ACS ") + kVersion).c_str());
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1180, 800);
    g_signal_connect(app->window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
                         auto* self = static_cast<App*>(data);
                         self->job.stop();
                         save_session(self->read());
                         gtk_main_quit();
                     }),
                     app);

    auto* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(app->window), root);

    auto* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(top), "topbar");
    gtk_widget_set_margin_start(top, 8);
    gtk_widget_set_margin_end(top, 18);
    gtk_widget_set_margin_top(top, 8);
    gtk_widget_set_margin_bottom(top, 8);
    auto* brand = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    auto* title = gtk_label_new("ACS");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "title");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "accent");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    auto* by = gtk_label_new("Air Crack Station   ·   Pakun & iinze0");
    gtk_style_context_add_class(gtk_widget_get_style_context(by), "muted");
    gtk_widget_set_halign(by, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(brand), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(brand), by, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(top), brand, FALSE, FALSE, 12);
    app->status = GTK_LABEL(gtk_label_new((std::string("v") + kVersion).c_str()));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(app->status)), "muted");
    gtk_box_pack_end(GTK_BOX(top), GTK_WIDGET(app->status), FALSE, FALSE, 8);
    app->chip_target = GTK_LABEL(gtk_label_new("no target"));
    app->chip_mon = GTK_LABEL(gtk_label_new("managed"));
    app->chip_iface = GTK_LABEL(gtk_label_new(session.iface.c_str()));
    for (auto* chip : {app->chip_target, app->chip_mon, app->chip_iface}) {
        gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(chip)), "chip");
        gtk_box_pack_end(GTK_BOX(top), GTK_WIDGET(chip), FALSE, FALSE, 4);
    }
    gtk_box_pack_start(GTK_BOX(root), top, FALSE, FALSE, 0);

    auto* body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(root), body, TRUE, TRUE, 0);
    auto* sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "sidebar");
    gtk_widget_set_size_request(sidebar, 196, -1);
    auto* kicker = gtk_label_new("WORKSPACE");
    gtk_style_context_add_class(gtk_widget_get_style_context(kicker), "kicker");
    gtk_widget_set_halign(kicker, GTK_ALIGN_START);
    gtk_widget_set_margin_start(kicker, 16);
    gtk_widget_set_margin_top(kicker, 16);
    gtk_box_pack_start(GTK_BOX(sidebar), kicker, FALSE, FALSE, 4);
    app->stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(app->stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    for (int i = 0; i < 5; ++i) {
        app->nav[i] = button(App::pages[i], "nav");
        gtk_widget_set_halign(app->nav[i], GTK_ALIGN_FILL);
        gtk_box_pack_start(GTK_BOX(sidebar), app->nav[i], FALSE, FALSE, 0);
        g_signal_connect(app->nav[i], "clicked", G_CALLBACK(+[](GtkButton* button, gpointer data) {
                             auto* self = static_cast<App*>(data);
                             const char* name = gtk_button_get_label(button);
                             for (int n = 0; n < 5; ++n) {
                                 if (std::string(App::pages[n]) == name) self->show_page(n);
                             }
                         }),
                         app);
    }
    gtk_box_pack_start(GTK_BOX(body), sidebar, FALSE, FALSE, 0);

    auto* stage = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(stage, 18);
    gtk_widget_set_margin_end(stage, 18);
    gtk_widget_set_margin_top(stage, 12);
    gtk_box_pack_start(GTK_BOX(body), stage, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(stage), GTK_WIDGET(app->stack), TRUE, TRUE, 0);

    auto* station = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    auto* scan = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    auto* attack = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    auto* crack = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    auto* proxy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_stack_add_named(app->stack, station, "Station");
    gtk_stack_add_named(app->stack, scan, "Scan");
    gtk_stack_add_named(app->stack, attack, "Attack");
    gtk_stack_add_named(app->stack, crack, "Crack");
    gtk_stack_add_named(app->stack, proxy, "Proxy");

    auto heading = [](GtkWidget* page, const char* title, const char* subtitle) {
        auto* h = gtk_label_new(title);
        gtk_style_context_add_class(gtk_widget_get_style_context(h), "title");
        gtk_widget_set_halign(h, GTK_ALIGN_START);
        auto* s = gtk_label_new(subtitle);
        gtk_style_context_add_class(gtk_widget_get_style_context(s), "muted");
        gtk_label_set_line_wrap(GTK_LABEL(s), TRUE);
        gtk_widget_set_halign(s, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(page), h, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(page), s, FALSE, FALSE, 0);
    };
    heading(station, "Station", "Set the wireless card and the network you are working on.");
    auto* columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    auto* left = card();
    auto* right = card();
    gtk_box_pack_start(GTK_BOX(columns), left, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(columns), right, TRUE, TRUE, 0);
    auto* left_title = gtk_label_new("Radio");
    auto* right_title = gtk_label_new("Target");
    gtk_style_context_add_class(gtk_widget_get_style_context(left_title), "section");
    gtk_style_context_add_class(gtk_widget_get_style_context(right_title), "section");
    gtk_widget_set_halign(left_title, GTK_ALIGN_START);
    gtk_widget_set_halign(right_title, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(left), left_title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), right_title, FALSE, FALSE, 0);
    labeled_entry(left, "INTERFACE", &app->iface, session.iface);
    labeled_entry(left, "MONITOR", &app->mon, session.mon);
    labeled_entry(left, "CHANNEL", &app->channel, session.channel);
    labeled_entry(right, "BSSID", &app->bssid, session.bssid);
    labeled_entry(right, "NETWORK NAME", &app->essid, session.essid);
    labeled_entry(right, "CLIENT", &app->client, session.client);
    labeled_entry(right, "WORDLIST", &app->wordlist, session.wordlist);
    app->cap = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_text(app->cap, session.cap.c_str());
    gtk_box_pack_start(GTK_BOX(station), columns, FALSE, FALSE, 0);
    auto* station_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    auto* save = button("Save", "suggested-action");
    auto* detect = button("Detect wireless", "nav");
    auto* monitor = button("Monitor mode", "nav");
    auto* restore = button("Restore Wi-Fi", "nav");
    auto* install = button("Install tools", "nav");
    auto* update = button("Check for updates", "nav");
    for (auto* widget : {save, detect, monitor, restore, install, update}) gtk_box_pack_start(GTK_BOX(station_buttons), widget, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(station), station_buttons, FALSE, FALSE, 4);
    g_signal_connect(save, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->save_clicked(); }), app);
    g_signal_connect(detect, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->detect(); }), app);
    g_signal_connect(monitor, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->monitor(); }), app);
    g_signal_connect(restore, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->restore(); }), app);
    g_signal_connect(install, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->install_tools(); }), app);
    g_signal_connect(update, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->check_update(true); }), app);

    heading(scan, "Networks", "Scan turns monitor mode on by itself, then lists access points. Click one to lock it.");
    auto* scan_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app->sec8 = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(nullptr, "8s"));
    app->sec15 = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(app->sec8, "15s"));
    app->sec30 = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(app->sec8, "30s"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->sec15), TRUE);
    auto* scan_btn = button("Scan", "suggested-action");
    auto* stop_btn = button("Stop", "nav");
    app->ap_count = GTK_LABEL(gtk_label_new("0 networks"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(app->ap_count)), "muted");
    for (auto* widget : {GTK_WIDGET(app->sec8), GTK_WIDGET(app->sec15), GTK_WIDGET(app->sec30), scan_btn, stop_btn})
        gtk_box_pack_start(GTK_BOX(scan_bar), widget, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(scan_bar), GTK_WIDGET(app->ap_count), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(scan), scan_bar, FALSE, FALSE, 0);
    app->aps = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    auto* ap_view = make_tree(app->aps, {{"BSSID", 180}, {"CH", 50}, {"SIGNAL", 70}, {"ENCRYPTION", 140}, {"NETWORK", 240}});
    auto* ap_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_container_add(GTK_CONTAINER(ap_scroll), ap_view);
    gtk_widget_set_vexpand(ap_scroll, TRUE);
    gtk_box_pack_start(GTK_BOX(scan), ap_scroll, TRUE, TRUE, 0);
    auto* client_kicker = gtk_label_new("CLIENTS ON THE SELECTED NETWORK");
    gtk_style_context_add_class(gtk_widget_get_style_context(client_kicker), "kicker");
    gtk_widget_set_halign(client_kicker, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(scan), client_kicker, FALSE, FALSE, 0);
    app->clients_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    auto* client_view = make_tree(app->clients_store, {{"CLIENT", 220}, {"SIGNAL", 80}, {"PROBES", 320}});
    gtk_widget_set_size_request(client_view, -1, 120);
    gtk_box_pack_start(GTK_BOX(scan), client_view, FALSE, FALSE, 0);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(ap_view)), "changed", G_CALLBACK(+[](GtkTreeSelection*, gpointer p) { static_cast<App*>(p)->pick_ap(); }), app);
    g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(client_view)), "changed",
                     G_CALLBACK(+[](GtkTreeSelection*, gpointer p) { static_cast<App*>(p)->pick_client(); }), app);
    g_signal_connect(scan_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->scan(); }), app);
    g_signal_connect(stop_btn, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) {
                         auto* self = static_cast<App*>(p);
                         self->job.stop();
                         self->write_log("stopped");
                         self->flush_log();
                     }),
                     app);

    heading(attack, "Attack", "Runs against the locked network. Monitor mode has to be on.");
    app->target_line = GTK_LABEL(gtk_label_new("No target locked"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(app->target_line)), "accent");
    gtk_widget_set_halign(GTK_WIDGET(app->target_line), GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(attack), GTK_WIDGET(app->target_line), FALSE, FALSE, 4);
    auto* deauth = button("Send deauth", "danger");
    auto* fake = button("Fake auth", "nav");
    auto* hand = button("Capture handshake", "suggested-action");
    add_action(attack, "Deauth", "Send a short deauth burst. Uses the client field when it is a real MAC.", deauth);
    add_action(attack, "Fake auth", "Associate to the locked access point with the network name you set.", fake);
    add_action(attack, "Handshake", "Capture on the locked channel, deauth, then crack with the wordlist if it exists.", hand);
    g_signal_connect(deauth, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->deauth(); }), app);
    g_signal_connect(fake, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->fakeauth(); }), app);
    g_signal_connect(hand, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->handshake(); }), app);

    heading(crack, "Crack", "WPA uses the newest capture. Hashcat uses a pcapng from hcxdumptool.");
    auto* wpa = button("Crack WPA", "suggested-action");
    auto* hcx = button("Start hcx capture", "nav");
    auto* hash = button("Run hashcat", "nav");
    add_action(crack, "WPA wordlist", "aircrack-ng against the latest capture and your wordlist.", wpa);
    add_action(crack, "Modern capture", "hcxdumptool writes a pcapng in ~/.acs for about 25 seconds.", hcx);
    add_action(crack, "Hashcat 22000", "Convert that pcapng and run hashcat mode 22000.", hash);
    g_signal_connect(wpa, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->crack(); }), app);
    g_signal_connect(hcx, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->hcx(); }), app);
    g_signal_connect(hash, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->hashcat(); }), app);

    heading(proxy, "Proxy chain", "Pull live addresses from GitHub and write a proxychains file.");
    auto* proxy_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app->socks5 = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label(nullptr, "socks5"));
    app->socks4 = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(app->socks5, "socks4"));
    app->http = GTK_RADIO_BUTTON(gtk_radio_button_new_with_label_from_widget(app->socks5, "http"));
    app->hops = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_text(app->hops, "8");
    gtk_entry_set_width_chars(app->hops, 4);
    for (auto* widget : {GTK_WIDGET(app->socks5), GTK_WIDGET(app->socks4), GTK_WIDGET(app->http), GTK_WIDGET(app->hops)})
        gtk_box_pack_start(GTK_BOX(proxy_bar), widget, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(proxy), proxy_bar, FALSE, FALSE, 0);
    auto* proxy_store = gtk_list_store_new(1, G_TYPE_STRING);
    g_object_set_data(G_OBJECT(app->window), "proxies", proxy_store);
    auto* proxy_view = make_tree(proxy_store, {{"PROXY", 240}});
    gtk_widget_set_vexpand(proxy_view, TRUE);
    gtk_box_pack_start(GTK_BOX(proxy), proxy_view, TRUE, TRUE, 0);
    auto* proxy_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    auto* pull = button("Pull from GitHub", "suggested-action");
    auto* save_chain = button("Save chain", "nav");
    auto* test = button("Test chain", "nav");
    for (auto* widget : {pull, save_chain, test}) gtk_box_pack_start(GTK_BOX(proxy_buttons), widget, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(proxy), proxy_buttons, FALSE, FALSE, 0);
    g_signal_connect(pull, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->pull(); }), app);
    g_signal_connect(save_chain, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->save_chain(); }), app);
    g_signal_connect(test, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) { static_cast<App*>(p)->test_chain(); }), app);

    auto* log_frame = card();
    auto* log_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    auto* output = gtk_label_new("OUTPUT");
    gtk_style_context_add_class(gtk_widget_get_style_context(output), "kicker");
    auto* clear = button("Clear", "nav");
    gtk_box_pack_start(GTK_BOX(log_bar), output, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(log_bar), clear, FALSE, FALSE, 0);
    auto* view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    app->log_buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
    gtk_text_buffer_create_tag(app->log_buf, "ok", "foreground", "#3dff8a", nullptr);
    gtk_text_buffer_create_tag(app->log_buf, "cmd", "foreground", "#8aa394", nullptr);
    gtk_text_buffer_create_tag(app->log_buf, "bad", "foreground", "#ff8d8d", nullptr);
    g_object_set_data(G_OBJECT(app->log_buf), "view", view);
    auto* log_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_widget_set_size_request(log_scroll, -1, 140);
    gtk_container_add(GTK_CONTAINER(log_scroll), view);
    gtk_box_pack_start(GTK_BOX(log_frame), log_bar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(log_frame), log_scroll, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(root), log_frame, FALSE, FALSE, 8);
    g_signal_connect(clear, "clicked", G_CALLBACK(+[](GtkButton*, gpointer p) {
                         auto* self = static_cast<App*>(p);
                         gtk_text_buffer_set_text(self->log_buf, "", -1);
                     }),
                     app);

    for (auto* entry : {app->iface, app->mon, app->channel, app->bssid, app->essid, app->client, app->wordlist}) {
        g_signal_connect(entry, "changed", G_CALLBACK(+[](GtkEditable*, gpointer p) { static_cast<App*>(p)->refresh(); }), app);
    }
    app->show_page(0);
    app->refresh();
    g_timeout_add(150, +[](gpointer data) -> gboolean {
        static_cast<App*>(data)->flush_log();
        return G_SOURCE_CONTINUE;
    }, app);

    const char* icon = "/usr/share/pixmaps/acs-cpp.png";
    if (g_file_test(icon, G_FILE_TEST_EXISTS)) gtk_window_set_icon_from_file(GTK_WINDOW(app->window), icon, nullptr);
}

}  // namespace

int ui_main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    auto* css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, kCss, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    auto* app = new App();
    build(app);
    if (::getenv("ACS_NO_UPDATE") == nullptr) {
        g_timeout_add(800, +[](gpointer data) -> gboolean {
            static_cast<App*>(data)->check_update(false);
            return G_SOURCE_REMOVE;
        }, app);
    }
    gtk_widget_show_all(app->window);
    gtk_main();
    return 0;
}

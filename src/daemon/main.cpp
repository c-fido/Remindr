#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <thread>
#include <unistd.h>
#include <vector>
#ifdef __APPLE__
#include <climits>
#include <mach-o/dyld.h>
#endif

#include "../common/credentials.hpp"
#include "../common/reminder.hpp"
#include "../common/socket.hpp"
#include "../common/store.hpp"
#include "../common/sync_state.hpp"
#include "../common/uuid.hpp"
#include "sync_worker.hpp"

static const char* SOCK_PATH  = "/tmp/reminderd.sock";
static const int   MAX_CLIENTS = 32;

static std::vector<Reminder> g_reminders;
static std::mutex              g_reminders_mutex;
static std::atomic<bool>       g_sync_requested{false};

static void save_reminders() {
    if (!remindr::save_reminders(g_reminders)) {
        std::cerr << "reminderd: failed to save reminders\n";
    }
}

static void load_reminders() {
    g_reminders = remindr::load_reminders();
    if (g_reminders.empty()) {
        save_reminders();
    }
}

static void sync_thread_fn() {
    bool first = true;
    while (true) {
        if (!first) {
            for (int i = 0; i < 60; ++i) {
                if (g_sync_requested.load()) break;
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        }
        first = false;
        g_sync_requested = false;

        if (!remindr::load_credentials()) continue;

        remindr::SyncResult result;
        {
            std::lock_guard<std::mutex> lock(g_reminders_mutex);
            if (!remindr::sync_once(g_reminders, &result)) continue;
            save_reminders();
        }
    }
}

#ifdef __APPLE__
static std::string find_notifier() {
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        auto helper = std::filesystem::path(buf).parent_path()
                      / "Remindr.app" / "Contents" / "MacOS" / "remind-notify";
        if (std::filesystem::exists(helper))
            return helper.string();
    }
    return "";
}
#endif

static void fire_notification(const std::string& message) {
#ifdef __APPLE__
    static const std::string notifier = find_notifier();
    if (!notifier.empty()) {
        pid_t pid = fork();
        if (pid == 0) {
            execl(notifier.c_str(), notifier.c_str(), "Remindr", message.c_str(), (char*)nullptr);
            _exit(127);
        } else if (pid < 0) {
            std::cerr << "reminderd: fork(): " << std::strerror(errno) << "\n";
        }
        return;
    }

    std::string safe;
    safe.reserve(message.size() * 2);
    for (char c : message) {
        if      (c == '\'') safe += "'\\''";
        else if (c == '"')  safe += "\\\"";
        else if (c == '\\') safe += "\\\\";
        else                safe += c;
    }
    std::string cmd = "osascript -e 'display notification \""
                    + safe + "\" with title \"Remindr\"' 2>&1";
    std::FILE* p = popen(cmd.c_str(), "r");
    if (!p) { std::cerr << "reminderd: failed to launch osascript\n"; return; }
    char buf[256]; std::string out;
    while (std::fgets(buf, sizeof(buf), p)) out += buf;
    int st = pclose(p);
    if (st != 0) {
        std::cerr << "reminderd: osascript failed (status " << st << ")";
        if (!out.empty()) std::cerr << ": " << out; else std::cerr << "\n";
    }

#elif defined(__linux__)
    pid_t pid = fork();
    if (pid == 0) {
        const char* argv[] = {"notify-send", "Remindr", message.c_str(), nullptr};
        execvp("notify-send", const_cast<char* const*>(argv));
        _exit(127);
    } else if (pid < 0) {
        std::cerr << "reminderd: fork(): " << std::strerror(errno) << "\n";
    }
#else
    std::cerr << "Remindr: " << message << "\n";
#endif
}

static void check_timers() {
    std::lock_guard<std::mutex> lock(g_reminders_mutex);

    auto now = static_cast<int64_t>(std::time(nullptr));
    bool dirty = false;

    for (auto& r : g_reminders) {
        if (r.deleted) continue;

        if (r.fire_at <= now) {
            fire_notification(r.message);
            dirty = true;

            if (r.recurrence == Recurrence::DAILY) {
                r.fire_at += 86400;
                r.updated_at = now;
                r.sync_status = "pending";
            } else if (r.recurrence == Recurrence::WEEKLY) {
                r.fire_at += 604800;
                r.updated_at = now;
                r.sync_status = "pending";
            } else {
                r.fired = true;
                r.deleted = true;
                r.updated_at = now;
                r.sync_status = "pending";
            }
        } else {
            ++it;
        }
    }

    if (dirty) save_reminders();
}

static std::string handle_command(const std::string& raw) {
    std::lock_guard<std::mutex> lock(g_reminders_mutex);

    try {
        auto cmd = nlohmann::json::parse(raw);
        std::string type = cmd.at("type").get<std::string>();

        if (type == "ADD") {
            const auto now = static_cast<int64_t>(std::time(nullptr));
            Reminder r{};
            r.id          = uuid_v4();
            r.message     = cmd.at("message").get<std::string>();
            r.fire_at     = cmd.at("fire_at").get<int64_t>();
            r.recurrence  = recurrence_from_string(
                cmd.value("recurrence", std::string("none")));
            r.fired       = false;
            r.deleted     = false;
            r.updated_at  = now;
            r.sync_status = "pending";
            g_reminders.push_back(r);
            save_reminders();
            nlohmann::json resp{{"ok", true}, {"id", r.id}};
            return resp.dump();
        }

        if (type == "LIST") {
            nlohmann::json arr = nlohmann::json::array();
            auto now = static_cast<int64_t>(std::time(nullptr));
            for (const auto& r : g_reminders) {
                if (r.deleted) continue;
                if (r.fire_at >= now || r.recurrence != Recurrence::NONE) {
                    arr.push_back(r);
                }
            }
            nlohmann::json resp{{"ok", true}, {"reminders", arr}};
            return resp.dump();
        }

        if (type == "DELETE") {
            const std::string id = cmd.at("id").is_string()
                ? cmd.at("id").get<std::string>()
                : std::to_string(cmd.at("id").get<uint64_t>());
            const auto now = static_cast<int64_t>(std::time(nullptr));
            bool removed = false;

            for (auto& r : g_reminders) {
                if (r.id == id && !r.deleted) {
                    r.deleted = true;
                    r.updated_at = now;
                    r.sync_status = "pending";
                    removed = true;
                    break;
                }
            }

            if (removed) save_reminders();
            nlohmann::json resp{{"ok", removed}};
            return resp.dump();
        }

        if (type == "SYNC") {
            g_sync_requested = true;
            remindr::SyncResult result;
            if (!remindr::load_credentials()) {
                nlohmann::json resp{
                    {"ok", false},
                    {"error", "not logged in"},
                };
                return resp.dump();
            }
            if (!remindr::sync_once(g_reminders, &result)) {
                nlohmann::json resp{
                    {"ok", false},
                    {"error", result.error.empty() ? "sync failed" : result.error},
                };
                return resp.dump();
            }
            save_reminders();
            nlohmann::json resp{
                {"ok", true},
                {"applied", result.applied},
                {"pending", result.pending},
            };
            return resp.dump();
        }

        if (type == "STATUS") {
            const auto creds = remindr::load_credentials();
            const bool sync_enabled = creds.has_value();
            int64_t last_sync_at = 0;
            if (auto state = remindr::load_sync_state()) {
                last_sync_at = state->last_sync_at;
            }

            nlohmann::json resp{
                {"ok", true},
                {"sync_enabled", sync_enabled},
                {"last_sync_at", last_sync_at},
                {"pending", remindr::count_pending(g_reminders)},
            };
            if (creds && !creds->email.empty()) {
                resp["email"] = creds->email;
            }
            return resp.dump();
        }

        nlohmann::json resp{{"ok", false}, {"error", "unknown command type"}};
        return resp.dump();

    } catch (const std::exception& e) {
        nlohmann::json resp{{"ok", false}, {"error", e.what()}};
        return resp.dump();
    }
}

int main() {
    signal(SIGCHLD, SIG_IGN);

    load_reminders();

    std::thread(sync_thread_fn).detach();

    ::unlink(SOCK_PATH);

    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "reminderd: socket(): " << std::strerror(errno) << "\n";
        return 1;
    }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path) - 1);

    if (bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "reminderd: bind(): " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 8) < 0) {
        std::cerr << "reminderd: listen(): " << std::strerror(errno) << "\n";
        close(server_fd);
        return 1;
    }

    std::cerr << "reminderd: listening on " << SOCK_PATH << "\n";

    std::vector<int> client_fds;
    std::vector<std::string> client_bufs;

    while (true) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_fd = server_fd;

        for (int fd : client_fds) {
            FD_SET(fd, &read_fds);
            if (fd > max_fd) max_fd = fd;
        }

        timeval tv{};
        tv.tv_sec  = 1;
        tv.tv_usec = 0;

        int ready = select(max_fd + 1, &read_fds, nullptr, nullptr, &tv);

        check_timers();

        if (ready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "reminderd: select(): " << std::strerror(errno) << "\n";
            break;
        }

        if (ready == 0) continue;

        if (FD_ISSET(server_fd, &read_fds)) {
            int client_fd = accept(server_fd, nullptr, nullptr);
            if (client_fd >= 0 && static_cast<int>(client_fds.size()) < MAX_CLIENTS) {
                client_fds.push_back(client_fd);
                client_bufs.emplace_back();
            } else if (client_fd >= 0) {
                close(client_fd);
            }
        }

        for (size_t i = 0; i < client_fds.size(); ) {
            int fd = client_fds[i];
            if (!FD_ISSET(fd, &read_fds)) { ++i; continue; }

            char buf[4096];
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                close(fd);
                client_fds.erase(client_fds.begin() + static_cast<ptrdiff_t>(i));
                client_bufs.erase(client_bufs.begin() + static_cast<ptrdiff_t>(i));
                continue;
            }

            client_bufs[i].append(buf, static_cast<size_t>(n));

            size_t pos;
            while ((pos = client_bufs[i].find('\n')) != std::string::npos) {
                std::string line = client_bufs[i].substr(0, pos);
                client_bufs[i].erase(0, pos + 1);
                if (!line.empty()) {
                    std::string resp = handle_command(line);
                    write_line(fd, resp);
                }
            }
            ++i;
        }
    }

    close(server_fd);
    ::unlink(SOCK_PATH);
    return 0;
}

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <unistd.h>
#include <vector>
#ifdef __APPLE__
#include <climits>
#include <mach-o/dyld.h>
#endif

#include "../common/json.hpp"
#include "../common/reminder.hpp"
#include "../common/socket.hpp"

static const char* SOCK_PATH  = "/tmp/reminderd.sock";
static const int   MAX_CLIENTS = 32;

static const std::string& reminders_path() {
    static const std::string path = []() {
        const char* home = std::getenv("HOME");
        return std::string(home ? home : "/tmp") + "/.config/reminderd/reminders.json";
    }();
    return path;
}

static std::vector<Reminder> g_reminders;

static void save_reminders() {
    static bool dir_ensured = false;
    try {
        const std::string& path = reminders_path();
        if (!dir_ensured) {
            std::filesystem::create_directories(std::filesystem::path(path).parent_path());
            dir_ensured = true;
        }
        nlohmann::json j = g_reminders;
        std::ofstream f(path);
        if (!f) throw std::runtime_error("cannot open for writing: " + path);
        f << j.dump(2) << "\n";
    } catch (const std::exception& e) {
        std::cerr << "reminderd: save error: " << e.what() << "\n";
    }
}

static void load_reminders() {
    std::string path = reminders_path();
    try {
        std::ifstream f(path);
        if (!f) return; // first run — empty list is fine
        nlohmann::json j;
        f >> j;
        g_reminders = j.get<std::vector<Reminder>>();
    } catch (const std::exception& e) {
        std::cerr << "reminderd: reminders.json is malformed (" << e.what()
                  << "); backing up and starting fresh\n";
        try {
            std::string backup = path + ".bak";
            std::filesystem::copy_file(path, backup,
                std::filesystem::copy_options::overwrite_existing);
            std::cerr << "reminderd: backup saved to " << backup << "\n";
        } catch (...) {}
        g_reminders.clear();
        save_reminders();
    }
}

#ifdef __APPLE__
static std::string find_notifier() {
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        auto helper = std::filesystem::path(buf).parent_path()
                      / "Redmindr.app" / "Contents" / "MacOS" / "remind-notify";
        if (std::filesystem::exists(helper))
            return helper.string();
    }
    return "";
}
#endif

static void fire_notification(const std::string& message) {
#ifdef __APPLE__
    // Prefer the native Swift helper (proper UNUserNotificationCenter, appears in
    // System Settings > Notifications as "Redmindr"). Fall back to osascript if
    // the helper bundle hasn't been installed.
    static const std::string notifier = find_notifier();
    if (!notifier.empty()) {
        pid_t pid = fork();
        if (pid == 0) {
            execl(notifier.c_str(), notifier.c_str(), "Redmindr", message.c_str(), (char*)nullptr);
            _exit(127);
        } else if (pid < 0) {
            std::cerr << "reminderd: fork(): " << std::strerror(errno) << "\n";
        }
        // Child is reaped automatically by SIGCHLD SIG_IGN set in main().
        return;
    }

    // osascript fallback — escape for shell (single-quote) and AppleScript (double-quote).
    std::string safe;
    safe.reserve(message.size() * 2);
    for (char c : message) {
        if      (c == '\'') safe += "'\\''";
        else if (c == '"')  safe += "\\\"";
        else if (c == '\\') safe += "\\\\";
        else                safe += c;
    }
    std::string cmd = "osascript -e 'display notification \""
                    + safe + "\" with title \"Redmindr\"' 2>&1";
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
        const char* argv[] = {"notify-send", "Redmindr", message.c_str(), nullptr};
        execvp("notify-send", const_cast<char* const*>(argv));
        _exit(127);
    } else if (pid < 0) {
        std::cerr << "reminderd: fork(): " << std::strerror(errno) << "\n";
    }
#else
    std::cerr << "Redmindr: " << message << "\n";
#endif
}

static void check_timers() {
    auto now = static_cast<int64_t>(std::time(nullptr));
    bool dirty = false;

    for (auto it = g_reminders.begin(); it != g_reminders.end(); ) {
        if (it->fire_at <= now) {
            fire_notification(it->message);
            dirty = true;

            if (it->recurrence == Recurrence::DAILY) {
                it->fire_at += 86400;
                ++it;
            } else if (it->recurrence == Recurrence::WEEKLY) {
                it->fire_at += 604800;
                ++it;
            } else {
                it = g_reminders.erase(it);
            }
        } else {
            ++it;
        }
    }

    if (dirty) save_reminders();
}

static uint64_t generate_id() {
    auto ts  = static_cast<uint64_t>(std::time(nullptr));
    auto rnd = static_cast<uint64_t>(std::rand() & 0xFFFF); // NOLINT
    return (ts << 16) | rnd;
}

static std::string handle_command(const std::string& raw) {
    try {
        auto cmd = nlohmann::json::parse(raw);
        std::string type = cmd.at("type").get<std::string>();

        if (type == "ADD") {
            Reminder r{};
            r.id         = generate_id();
            r.message    = cmd.at("message").get<std::string>();
            r.fire_at    = cmd.at("fire_at").get<int64_t>();
            r.recurrence = recurrence_from_string(
                cmd.value("recurrence", std::string("none")));
            r.fired = false;
            g_reminders.push_back(r);
            save_reminders();
            nlohmann::json resp{{"ok", true}, {"id", r.id}};
            return resp.dump();
        }

        if (type == "LIST") {
            nlohmann::json arr = nlohmann::json::array();
            auto now = static_cast<int64_t>(std::time(nullptr));
            for (const auto& r : g_reminders) {
                if (r.fire_at >= now || r.recurrence != Recurrence::NONE) {
                    arr.push_back(r);
                }
            }
            nlohmann::json resp{{"ok", true}, {"reminders", arr}};
            return resp.dump();
        }

        if (type == "DELETE") {
            uint64_t id = cmd.at("id").get<uint64_t>();
            auto before = g_reminders.size();
            g_reminders.erase(
                std::remove_if(g_reminders.begin(), g_reminders.end(),
                    [id](const Reminder& r) { return r.id == id; }),
                g_reminders.end());
            bool removed = g_reminders.size() < before;
            if (removed) save_reminders();
            nlohmann::json resp{{"ok", removed}};
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
    // Auto-reap notification helper children so they don't block the event loop.
    signal(SIGCHLD, SIG_IGN);

    std::srand(static_cast<unsigned>(std::time(nullptr)));
    load_reminders();

    ::unlink(SOCK_PATH); // remove stale socket from a previous run

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
    std::vector<std::string> client_bufs; // parallel partial-read buffers

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

        // Always check timers each iteration regardless of select() result.
        check_timers();

        if (ready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "reminderd: select(): " << std::strerror(errno) << "\n";
            break;
        }

        if (ready == 0) continue; // timeout — timers already checked above

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

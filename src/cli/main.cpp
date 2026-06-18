#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>
#ifdef __APPLE__
#include <climits>
#include <mach-o/dyld.h>
#endif

#include "../common/json.hpp"
#include "../common/reminder.hpp"
#include "../common/socket.hpp"

static const char* SOCK_PATH = "/tmp/reminderd.sock";

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

// Parse "HH:MM", "H:MM", "Hpm", "H:MMpm", "H:MMam" etc.
// Returns seconds since midnight, or -1 on failure.
static int parse_clock(const std::string& tok) {
    std::string s = to_lower(tok);
    bool is_pm = false, is_am = false;

    if (s.size() > 2 && s.substr(s.size() - 2) == "pm") { is_pm = true; s = s.substr(0, s.size() - 2); }
    else if (s.size() > 2 && s.substr(s.size() - 2) == "am") { is_am = true; s = s.substr(0, s.size() - 2); }

    int hour = -1, minute = 0;
    auto colon = s.find(':');
    try {
        if (colon != std::string::npos) {
            hour   = std::stoi(s.substr(0, colon));
            minute = std::stoi(s.substr(colon + 1));
        } else {
            hour = std::stoi(s);
        }
    } catch (...) { return -1; }

    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return -1;

    if (is_pm && hour < 12) hour += 12;
    if (is_am && hour == 12) hour = 0;

    return hour * 3600 + minute * 60;
}

// Map day-of-week name to 0-6 (Sun=0). Returns -1 on unknown.
static int day_name_to_wday(const std::string& tok) {
    static const char* names[] = {
        "sunday","monday","tuesday","wednesday","thursday","friday","saturday"
    };
    std::string s = to_lower(tok);
    for (int i = 0; i < 7; ++i)
        if (s == names[i]) return i;
    return -1;
}

static bool is_time_token(const std::string& tok) {
    std::string s = to_lower(tok);
    if (s == "tomorrow" || s == "at") return true;
    if (day_name_to_wday(s) >= 0) return true;
    if (parse_clock(s) >= 0) return true;
    return false;
}

static std::optional<std::time_t> parse_time_expr(const std::string& expr) {
    std::string s = to_lower(expr);

    // 1. Relative: "in N minutes/hours/days"
    if (s.rfind("in ", 0) == 0) {
        std::istringstream iss(s.substr(3));
        int n; std::string unit;
        if (iss >> n >> unit) {
            int64_t secs = 0;
            if (unit.rfind("minute", 0) == 0 || unit == "min" || unit == "mins") secs = n * 60;
            else if (unit.rfind("hour",   0) == 0 || unit == "hr" || unit == "hrs") secs = n * 3600;
            else if (unit.rfind("day",    0) == 0)                                  secs = n * 86400;
            else if (unit.rfind("week",   0) == 0)                                  secs = n * 604800;
            if (secs > 0) return static_cast<std::time_t>(std::time(nullptr) + secs);
        }
    }

    auto split = [](const std::string& str) -> std::vector<std::string> {
        std::vector<std::string> tokens;
        std::istringstream iss(str);
        std::string tok;
        while (iss >> tok) tokens.push_back(tok);
        return tokens;
    };

    auto tokens = split(s);
    if (tokens.empty()) return std::nullopt;

    int clock_secs  = -1;
    int target_wday = -1;  // 0-6, -1 = not set
    bool has_tomorrow = false;

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i] == "tomorrow") { has_tomorrow = true; continue; }
        if (tokens[i] == "at")       continue;
        int wday = day_name_to_wday(tokens[i]);
        if (wday >= 0) { target_wday = wday; continue; }
        int cs = parse_clock(tokens[i]);
        if (cs >= 0) { clock_secs = cs; continue; }
    }

    std::time_t now = std::time(nullptr);
    struct tm tm_now{};
#ifdef __APPLE__
    localtime_r(&now, &tm_now);
#else
    localtime_r(&now, &tm_now);
#endif

    if (has_tomorrow && clock_secs < 0) {
        struct tm t = tm_now;
        t.tm_mday  += 1;
        t.tm_hour   = 0; t.tm_min = 0; t.tm_sec = 0;
        t.tm_isdst  = -1;
        return std::mktime(&t);
    }

    if (has_tomorrow && clock_secs >= 0) {
        struct tm t = tm_now;
        t.tm_mday  += 1;
        t.tm_hour   = clock_secs / 3600;
        t.tm_min    = (clock_secs % 3600) / 60;
        t.tm_sec    = 0;
        t.tm_isdst  = -1;
        return std::mktime(&t);
    }

    if (target_wday >= 0) {
        int today = tm_now.tm_wday;
        int diff  = target_wday - today;
        if (diff <= 0) diff += 7; // always future

        struct tm t = tm_now;
        t.tm_mday += diff;
        if (clock_secs >= 0) {
            t.tm_hour = clock_secs / 3600;
            t.tm_min  = (clock_secs % 3600) / 60;
            t.tm_sec  = 0;
        } else {
            t.tm_hour = 0; t.tm_min = 0; t.tm_sec = 0;
        }
        t.tm_isdst = -1;
        return std::mktime(&t);
    }

    if (clock_secs >= 0) {
        // Today at that time; if already past, push to tomorrow.
        struct tm t = tm_now;
        t.tm_hour   = clock_secs / 3600;
        t.tm_min    = (clock_secs % 3600) / 60;
        t.tm_sec    = 0;
        t.tm_isdst  = -1;
        std::time_t candidate = std::mktime(&t);
        if (candidate <= now) {
            t.tm_mday += 1;
            t.tm_isdst = -1;
            candidate  = std::mktime(&t);
        }
        return candidate;
    }

    return std::nullopt;
}

static std::string format_time(int64_t ts) {
    auto t = static_cast<std::time_t>(ts);
    struct tm tm_buf{};
    localtime_r(&t, &tm_buf);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm_buf);
    return buf;
}

static std::string format_id(uint64_t id) {
    std::ostringstream oss;
    oss << id;
    return oss.str();
}

static std::string find_reminderd() {
#ifdef __APPLE__
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        auto sibling = std::filesystem::path(buf).parent_path() / "reminderd";
        if (std::filesystem::exists(sibling))
            return sibling.string();
    }
#endif
    return "reminderd";
}

#ifdef __APPLE__
static int do_install() {
    const char* home = std::getenv("HOME");
    if (!home) { std::cerr << "remind: $HOME not set\n"; return 1; }

    std::string agents_dir = std::string(home) + "/Library/LaunchAgents";
    std::string plist_path = agents_dir + "/com.remind.reminderd.plist";

    std::string reminderd_path = find_reminderd();
    std::string log_path = std::string(home) + "/.config/reminderd/reminderd.log";

    std::string plist = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
  "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>com.remind.reminderd</string>
    <key>ProgramArguments</key>
    <array>
        <string>)" + reminderd_path + R"(</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>StandardErrorPath</key>
    <string>)" + log_path + R"(</string>
    <key>StandardOutPath</key>
    <string>)" + log_path + R"(</string>
</dict>
</plist>
)";

    try {
        std::filesystem::create_directories(agents_dir);
        std::filesystem::create_directories(std::filesystem::path(log_path).parent_path());
        std::ofstream f(plist_path);
        if (!f) throw std::runtime_error("cannot write " + plist_path);
        f << plist;
        std::cout << "Installed: " << plist_path << "\n";
        std::cout << "Daemon binary: " << reminderd_path << "\n";

        // Prefer SUDO_UID when elevated — gui/0 doesn't support LaunchAgent bootstrap.
        uid_t real_uid = ::getuid();
        const char* sudo_uid_env = std::getenv("SUDO_UID");
        if (sudo_uid_env) {
            try { real_uid = static_cast<uid_t>(std::stoul(sudo_uid_env)); } catch (...) {}
        }
        if (real_uid == 0) {
            std::cerr << "remind: --install should not be run as root; "
                         "run without sudo as your login user.\n";
            return 1;
        }
        std::string uid    = std::to_string(static_cast<unsigned>(real_uid));
        std::string domain = "gui/" + uid;
        static const char* SERVICE_LABEL = "com.remind.reminderd";

        // launchd may have auto-loaded the plist already; bootout by label first
        // so we can do a clean bootstrap.
        std::string bootout_cmd = "launchctl bootout " + domain + "/" + SERVICE_LABEL + " 2>/dev/null";
        std::system(bootout_cmd.c_str());

        // bootstrap is the modern way; load -w is deprecated but more lenient on Apple Silicon.
        std::string bootstrap_cmd = "launchctl bootstrap " + domain + " \"" + plist_path + "\" 2>/dev/null";
        std::string load_cmd      = "launchctl load -w \"" + plist_path + "\" 2>/dev/null";
        std::string check_cmd     = "launchctl list " + std::string(SERVICE_LABEL) + " >/dev/null 2>&1";

        bool started = false;
        if (std::system(bootstrap_cmd.c_str()) == 0)
            started = true;
        else if (std::system(load_cmd.c_str()) == 0)
            started = true;
        else
            started = (std::system(check_cmd.c_str()) == 0); // auto-loaded by launchd?

        if (started)
            std::cout << "reminderd started and will launch at every login.\n";
        else
            std::cout << "Plist installed. To start now:\n"
                      << "  launchctl bootstrap " << domain << " \"" << plist_path << "\"\n"
                      << "Or log out and back in.\n";
    } catch (const std::exception& e) {
        std::cerr << "remind: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
#else
static int do_install() {
    const char* home = std::getenv("HOME");
    if (!home) { std::cerr << "remind: $HOME not set\n"; return 1; }

    std::string systemd_dir  = std::string(home) + "/.config/systemd/user";
    std::string service_path = systemd_dir + "/reminderd.service";

    std::string unit = R"([Unit]
Description=Remind daemon

[Service]
ExecStart=reminderd
Restart=always

[Install]
WantedBy=default.target
)";

    try {
        std::filesystem::create_directories(systemd_dir);
        std::ofstream f(service_path);
        if (!f) throw std::runtime_error("cannot write " + service_path);
        f << unit;
        std::cout << "Installed: " << service_path << "\n";
        std::cout << "Enable with:\n";
        std::cout << "  systemctl --user daemon-reload\n";
        std::cout << "  systemctl --user enable --now reminderd\n";
    } catch (const std::exception& e) {
        std::cerr << "remind: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
#endif

static int daemon_fd() {
    int fd = connect_to_daemon(SOCK_PATH);
    if (fd < 0) {
        std::cerr << "reminderd is not running. Start it with: reminderd\n";
        std::exit(1);
    }
    return fd;
}

static void print_usage() {
    std::cout << R"(Usage:
  remind <message> <time_expr> [--daily|--weekly]   Add a reminder
  remind list                                        Show upcoming reminders
  remind delete <id>                                 Delete a reminder by id
  remind --install                                   Install & start on login

The message does not need quotes. Time tokens (day names, clock times,
"tomorrow", "in N unit") are detected automatically from the end of the line.

Examples:
  remind coffee chat friday 3pm
  remind standup tomorrow 9am --daily
  remind call mom in 2 hours
  remind dentist appointment tuesday at 10am
  remind "quoted message works too" friday 4pm

Time expression tokens:
  friday / monday / …    next occurrence of that weekday (at midnight)
  tomorrow               next calendar day at midnight
  3pm / 4:30pm / 16:00   clock time (today, or tomorrow if already past)
  in 30 minutes          relative from now  (also: hours, days, weeks)
)";
}

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(); return 1; }

    Recurrence recur = Recurrence::NONE;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--daily")        recur = Recurrence::DAILY;
        else if (a == "--weekly")  recur = Recurrence::WEEKLY;
        else                       args.push_back(a);
    }
    if (args.empty()) { print_usage(); return 1; }

    std::string subcmd = args[0];

    if (subcmd == "--install") return do_install();

    if (subcmd == "list") {
        int fd = daemon_fd();
        nlohmann::json cmd{{"type", "LIST"}};
        auto resp_str = send_command(fd, cmd.dump());
        close(fd);
        if (!resp_str) { std::cerr << "remind: no response from daemon\n"; return 1; }

        try {
            auto resp = nlohmann::json::parse(*resp_str);
            if (!resp.at("ok").get<bool>()) {
                std::cerr << "remind: " << resp.value("error", "unknown error") << "\n";
                return 1;
            }
            auto reminders = resp.at("reminders").get<std::vector<Reminder>>();
            if (reminders.empty()) {
                std::cout << "No upcoming reminders.\n";
                return 0;
            }
            std::sort(reminders.begin(), reminders.end(),
                [](const Reminder& a, const Reminder& b){ return a.fire_at < b.fire_at; });

            std::cout << std::left
                      << std::setw(20) << "ID"
                      << std::setw(20) << "When"
                      << std::setw(10) << "Recur"
                      << "Message\n";
            std::cout << std::string(72, '-') << "\n";
            for (const auto& r : reminders) {
                std::cout << std::left
                          << std::setw(20) << format_id(r.id)
                          << std::setw(20) << format_time(r.fire_at)
                          << std::setw(10) << recurrence_to_string(r.recurrence)
                          << r.message << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "remind: parse error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    if (subcmd == "delete") {
        if (args.size() < 2) { std::cerr << "remind: delete requires an id\n"; return 1; }
        uint64_t id = 0;
        try { id = std::stoull(args[1]); }
        catch (...) { std::cerr << "remind: invalid id: " << args[1] << "\n"; return 1; }

        int fd = daemon_fd();
        nlohmann::json cmd{{"type", "DELETE"}, {"id", id}};
        auto resp_str = send_command(fd, cmd.dump());
        close(fd);
        if (!resp_str) { std::cerr << "remind: no response from daemon\n"; return 1; }

        try {
            auto resp = nlohmann::json::parse(*resp_str);
            if (resp.at("ok").get<bool>())
                std::cout << "Deleted reminder " << id << "\n";
            else
                std::cerr << "remind: reminder " << id << " not found\n";
        } catch (const std::exception& e) {
            std::cerr << "remind: parse error: " << e.what() << "\n";
            return 1;
        }
        return 0;
    }

    std::string message, time_expr;

    // "in N unit" anchors the time expression; otherwise scan from the right.
    int in_pos = -1;
    for (int i = 1; i < (int)args.size(); ++i) {
        if (to_lower(args[i]) == "in") { in_pos = i; break; }
    }

    if (in_pos >= 1) {
        for (int i = 0; i < in_pos; ++i) {
            if (!message.empty()) message += " ";
            message += args[i];
        }
        for (int i = in_pos; i < (int)args.size(); ++i) {
            if (!time_expr.empty()) time_expr += " ";
            time_expr += args[i];
        }
    } else {
        int time_start = (int)args.size();
        for (int i = (int)args.size() - 1; i >= 0; --i) {
            if (is_time_token(args[i])) time_start = i;
            else break;
        }
        if (time_start == 0) {
            std::cerr << "remind: message is required before the time expression\n";
            print_usage();
            return 1;
        }
        for (int i = 0; i < time_start; ++i) {
            if (!message.empty()) message += " ";
            message += args[i];
        }
        for (int i = time_start; i < (int)args.size(); ++i) {
            if (!time_expr.empty()) time_expr += " ";
            time_expr += args[i];
        }
    }

    if (message.empty()) {
        std::cerr << "remind: message is required\n";
        print_usage();
        return 1;
    }

    auto fire_at = parse_time_expr(time_expr);
    if (!fire_at) {
        std::cerr << "Could not parse time expression: " << time_expr << "\n";
        std::cerr << "Examples: \"in 30 minutes\", \"tomorrow 9am\", \"friday at 4pm\", \"16:00\"\n";
        return 1;
    }

    int fd = daemon_fd();
    nlohmann::json cmd{
        {"type",       "ADD"},
        {"message",    message},
        {"fire_at",    static_cast<int64_t>(*fire_at)},
        {"recurrence", recurrence_to_string(recur)}
    };
    auto resp_str = send_command(fd, cmd.dump());
    close(fd);
    if (!resp_str) { std::cerr << "remind: no response from daemon\n"; return 1; }

    try {
        auto resp = nlohmann::json::parse(*resp_str);
        if (!resp.at("ok").get<bool>()) {
            std::cerr << "remind: " << resp.value("error", "unknown error") << "\n";
            return 1;
        }
        std::cout << "Reminder set for " << format_time(static_cast<int64_t>(*fire_at))
                  << " (id " << resp.at("id").get<uint64_t>() << ")\n";
    } catch (const std::exception& e) {
        std::cerr << "remind: parse error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

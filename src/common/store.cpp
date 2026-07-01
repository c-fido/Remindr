#include "store.hpp"

#include "paths.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace remindr {

std::string reminders_v1_path() {
    return config_dir() + "/reminders.json";
}

std::string reminders_v2_path() {
    return config_dir() + "/reminders_v2.json";
}

static bool load_json_file(const std::string& path, nlohmann::json& out) {
    std::ifstream f(path);
    if (!f) return false;
    f >> out;
    return true;
}

static Reminder migrate_v1_entry(const nlohmann::json& item, int64_t migrated_at) {
    Reminder r{};
    from_json(item, r);
    r.updated_at  = migrated_at;
    r.deleted     = false;
    r.sync_status = "pending";
    return r;
}

static std::vector<Reminder> parse_v2_file(const std::string& path) {
    nlohmann::json j;
    if (!load_json_file(path, j)) return {};

    if (!j.is_array()) {
        throw std::runtime_error("reminder store must be a JSON array");
    }

    return j.get<std::vector<Reminder>>();
}

static void backup_corrupt_file(const std::string& path) {
    try {
        const std::string backup = path + ".bak";
        std::filesystem::copy_file(
            path, backup, std::filesystem::copy_options::overwrite_existing);
        std::cerr << "reminderd: backup saved to " << backup << "\n";
    } catch (...) {}
}

std::vector<Reminder> load_reminders() {
    const std::string v2 = reminders_v2_path();
    const std::string v1 = reminders_v1_path();

    try {
        if (std::filesystem::exists(v2)) {
            return parse_v2_file(v2);
        }

        if (std::filesystem::exists(v1)) {
            nlohmann::json j;
            if (!load_json_file(v1, j)) return {};

            if (!j.is_array()) {
                throw std::runtime_error("reminders.json must be a JSON array");
            }

            const int64_t migrated_at = static_cast<int64_t>(std::time(nullptr));
            std::vector<Reminder> reminders;
            reminders.reserve(j.size());

            for (const auto& item : j) {
                reminders.push_back(migrate_v1_entry(item, migrated_at));
            }

            save_reminders(reminders);
            std::cerr << "reminderd: migrated " << reminders.size()
                      << " reminder(s) from reminders.json to reminders_v2.json\n";
            return reminders;
        }

        return {};
    } catch (const std::exception& e) {
        const std::string bad_path = std::filesystem::exists(v2) ? v2 : v1;
        std::cerr << "reminderd: reminder store is malformed (" << e.what()
                  << "); backing up and starting fresh\n";
        if (!bad_path.empty() && std::filesystem::exists(bad_path)) {
            backup_corrupt_file(bad_path);
        }
        return {};
    }
}

bool save_reminders(const std::vector<Reminder>& reminders) {
    try {
        const std::string path = reminders_v2_path();
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());

        nlohmann::json j = reminders;
        std::ofstream f(path);
        if (!f) return false;
        f << j.dump(2) << "\n";
        return true;
    } catch (const std::exception& e) {
        std::cerr << "reminderd: save error: " << e.what() << "\n";
        return false;
    }
}

}  // namespace remindr

#include "sync_state.hpp"

#include "json.hpp"
#include "paths.hpp"
#include "uuid.hpp"

#include <filesystem>
#include <fstream>

namespace remindr {

static std::string sync_state_path() {
    return config_dir() + "/sync_state.json";
}

std::optional<SyncState> load_sync_state() {
    try {
        std::ifstream f(sync_state_path());
        if (!f) return std::nullopt;

        nlohmann::json j;
        f >> j;

        SyncState s;
        s.last_sync_at = j.value("last_sync_at", static_cast<int64_t>(0));
        s.device_id    = j.at("device_id").get<std::string>();
        return s;
    } catch (...) {
        return std::nullopt;
    }
}

bool save_sync_state(const SyncState& state) {
    try {
        const auto path = sync_state_path();
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());

        nlohmann::json j{
            {"last_sync_at", state.last_sync_at},
            {"device_id",    state.device_id},
        };

        std::ofstream f(path);
        if (!f) return false;
        f << j.dump(2) << "\n";
        return true;
    } catch (...) {
        return false;
    }
}

SyncState load_or_create_sync_state() {
    if (auto s = load_sync_state()) return *s;
    SyncState s;
    s.device_id = uuid_v4();
    save_sync_state(s);
    return s;
}

}  // namespace remindr

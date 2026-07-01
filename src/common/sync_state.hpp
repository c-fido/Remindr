#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace remindr {

struct SyncState {
    int64_t     last_sync_at = 0;
    std::string device_id;
};

std::optional<SyncState> load_sync_state();
bool save_sync_state(const SyncState& state);
SyncState load_or_create_sync_state();

}  // namespace remindr

#pragma once

#include <string>
#include <vector>

#include "../common/reminder.hpp"

namespace remindr {

struct SyncResult {
    bool        ok = false;
    int         applied = 0;
    int         pending = 0;
    int         conflicts = 0;
    std::string error;
};

// Pushes pending local rows to POST /v1/sync and merges inbound changes.
// No-op (returns false, no error) when credentials are missing.
bool sync_once(std::vector<Reminder>& reminders, SyncResult* result = nullptr);

int count_pending(const std::vector<Reminder>& reminders);

}  // namespace remindr

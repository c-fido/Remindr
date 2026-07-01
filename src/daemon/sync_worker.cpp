#include "sync_worker.hpp"

#include "../common/credentials.hpp"
#include "../common/http_client.hpp"
#include "../common/sync_state.hpp"
#include "../common/uuid.hpp"

#include <ctime>
#include <iostream>
#include <unordered_set>

namespace remindr {

namespace {

bool looks_like_uuid(const std::string& id) {
    return id.size() == 36 && id[8] == '-' && id[13] == '-' && id[18] == '-' && id[23] == '-';
}

nlohmann::json reminder_to_sync_json(const Reminder& r) {
    return nlohmann::json{
        {"id",         r.id},
        {"message",    r.message},
        {"fire_at",    r.fire_at},
        {"recurrence", recurrence_to_string(r.recurrence)},
        {"fired",      r.fired},
        {"deleted",    r.deleted},
        {"updated_at", r.updated_at},
    };
}

Reminder* find_by_id(std::vector<Reminder>& reminders, const std::string& id) {
    for (auto& r : reminders) {
        if (r.id == id) return &r;
    }
    return nullptr;
}

void ensure_uuid_for_sync(Reminder& r, int64_t now) {
    if (looks_like_uuid(r.id)) return;
    r.id         = uuid_v4();
    r.updated_at = now;
}

void apply_inbound_change(std::vector<Reminder>& reminders, const Reminder& server) {
    if (Reminder* local = find_by_id(reminders, server.id)) {
        if (server.updated_at > local->updated_at) {
            *local = server;
            local->sync_status = "synced";
        } else if (server.updated_at == local->updated_at) {
            local->sync_status = "synced";
        } else if (local->sync_status != "pending") {
            *local = server;
            local->sync_status = "synced";
        }
        return;
    }

    Reminder inserted = server;
    inserted.sync_status = "synced";
    reminders.push_back(inserted);
}

void log_sync(const std::string& msg) {
    std::cerr << "reminderd: sync: " << msg << "\n";
}

std::string api_error_message(int status, const std::string& body) {
    try {
        auto j = nlohmann::json::parse(body);
        if (j.contains("error")) {
            return "HTTP " + std::to_string(status) + ": " + j.at("error").get<std::string>();
        }
    } catch (...) {}
    if (body.empty()) {
        return "HTTP " + std::to_string(status);
    }
    return "HTTP " + std::to_string(status) + ": " + body;
}

void reuuid_pending_reminders(std::vector<Reminder>& reminders, int64_t now) {
    for (auto& r : reminders) {
        if (r.sync_status == "synced") continue;
        r.id         = uuid_v4();
        r.updated_at = now;
    }
}

bool run_sync_request(
    std::vector<Reminder>& reminders,
    SyncResult* result,
    const SyncState& state,
    const std::vector<Reminder>& outgoing)
{
    nlohmann::json changes = nlohmann::json::array();
    for (const auto& r : outgoing) {
        changes.push_back(reminder_to_sync_json(r));
    }

    nlohmann::json req{
        {"device_id",    state.device_id},
        {"last_sync_at", state.last_sync_at},
        {"changes",      changes},
    };

    const HttpResponse resp = http_post_json("/v1/sync", req.dump(), true);
    if (resp.status == 0) {
        result->error = "network error";
        log_sync(result->error);
        return false;
    }
    if (resp.status != 200) {
        result->error = api_error_message(resp.status, resp.body);
        log_sync(result->error);
        return false;
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(resp.body);
    } catch (const std::exception& e) {
        result->error = std::string("invalid JSON: ") + e.what();
        log_sync(result->error);
        return false;
    }

    result->applied   = body.value("applied", 0);
    result->conflicts = 0;

    std::unordered_set<std::string> conflict_ids;
    if (body.contains("conflicts") && body.at("conflicts").is_array()) {
        for (const auto& c : body.at("conflicts")) {
            Reminder conflict = c.get<Reminder>();
            ++result->conflicts;
            conflict_ids.insert(conflict.id);

            if (Reminder* local = find_by_id(reminders, conflict.id)) {
                if (conflict.updated_at >= local->updated_at) {
                    *local = conflict;
                }
                local->sync_status = "conflict";
                log_sync("conflict on reminder " + conflict.id);
            }
        }
    }

    for (const auto& sent : outgoing) {
        if (conflict_ids.count(sent.id)) continue;
        if (Reminder* local = find_by_id(reminders, sent.id)) {
            local->sync_status = "synced";
        }
    }

    if (body.contains("changes") && body.at("changes").is_array()) {
        for (const auto& item : body.at("changes")) {
            apply_inbound_change(reminders, item.get<Reminder>());
        }
    }

    SyncState updated = state;
    updated.last_sync_at = body.value("last_sync_at", static_cast<int64_t>(std::time(nullptr)));
    save_sync_state(updated);

    result->ok      = true;
    result->pending = count_pending(reminders);
    log_sync("applied=" + std::to_string(result->applied)
             + " conflicts=" + std::to_string(result->conflicts)
             + " pending=" + std::to_string(result->pending));
    return true;
}

}  // namespace

int count_pending(const std::vector<Reminder>& reminders) {
    int n = 0;
    for (const auto& r : reminders) {
        if (r.sync_status != "synced") ++n;
    }
    return n;
}

bool sync_once(std::vector<Reminder>& reminders, SyncResult* result) {
    SyncResult local_result;
    if (!result) result = &local_result;

    result->ok        = false;
    result->applied   = 0;
    result->conflicts = 0;
    result->error.clear();

    if (!load_credentials()) {
        return false;
    }

    const int64_t now = static_cast<int64_t>(std::time(nullptr));
    SyncState state   = load_or_create_sync_state();

    std::vector<Reminder> outgoing;
    outgoing.reserve(reminders.size());
    for (auto& r : reminders) {
        if (r.sync_status == "synced") continue;
        ensure_uuid_for_sync(r, now);
        outgoing.push_back(r);
    }

    if (run_sync_request(reminders, result, state, outgoing)) {
        return true;
    }

    // IDs may belong to another account — assign fresh UUIDs and retry once.
    if (result->error.find("HTTP 409") != std::string::npos ||
        result->error.find("HTTP 500") != std::string::npos) {
        log_sync("retrying with new reminder ids");
        reuuid_pending_reminders(reminders, now);
        outgoing.clear();
        for (auto& r : reminders) {
            if (r.sync_status == "synced") continue;
            outgoing.push_back(r);
        }
        result->error.clear();
        result->ok = false;
        if (run_sync_request(reminders, result, state, outgoing)) {
            return true;
        }
    }

    return false;
}

}  // namespace remindr

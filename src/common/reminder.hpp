#pragma once
#include <cstdint>
#include <string>
#include "json.hpp"

enum class Recurrence { NONE, DAILY, WEEKLY };

struct Reminder {
    uint64_t    id;
    std::string message;
    int64_t     fire_at;
    Recurrence  recurrence;
    bool        fired;
};

inline std::string recurrence_to_string(Recurrence r) {
    switch (r) {
        case Recurrence::DAILY:  return "daily";
        case Recurrence::WEEKLY: return "weekly";
        default:                 return "none";
    }
}

inline Recurrence recurrence_from_string(const std::string& s) {
    if (s == "daily")  return Recurrence::DAILY;
    if (s == "weekly") return Recurrence::WEEKLY;
    return Recurrence::NONE;
}

inline void to_json(nlohmann::json& j, const Reminder& r) {
    j = nlohmann::json{
        {"id",         r.id},
        {"message",    r.message},
        {"fire_at",    r.fire_at},
        {"recurrence", recurrence_to_string(r.recurrence)},
        {"fired",      r.fired}
    };
}

inline void from_json(const nlohmann::json& j, Reminder& r) {
    j.at("id").get_to(r.id);
    j.at("message").get_to(r.message);
    j.at("fire_at").get_to(r.fire_at);
    r.recurrence = recurrence_from_string(j.at("recurrence").get<std::string>());
    j.at("fired").get_to(r.fired);
}

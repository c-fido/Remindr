#pragma once

#include <string>
#include <vector>

#include "reminder.hpp"

namespace remindr {

std::string reminders_v1_path();
std::string reminders_v2_path();

std::vector<Reminder> load_reminders();
bool save_reminders(const std::vector<Reminder>& reminders);

}  // namespace remindr

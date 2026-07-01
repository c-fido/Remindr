#include "paths.hpp"

#include <cstdlib>
#include <filesystem>

namespace remindr {

std::string config_dir() {
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "/tmp") + "/.config/reminderd";
}

}  // namespace remindr

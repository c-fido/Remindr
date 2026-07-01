#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace remindr {

struct Credentials {
    std::string access_token;
    std::string refresh_token;
    int64_t     expires_at = 0;  // unix seconds UTC
    std::string email;           // optional, for status display
};

std::optional<Credentials> load_credentials();
bool save_credentials(const Credentials& creds);
bool delete_credentials();

}  // namespace remindr

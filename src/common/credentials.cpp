#include "credentials.hpp"

#include "json.hpp"
#include "paths.hpp"

#include <filesystem>
#include <fstream>

namespace remindr {

static std::string credentials_path() {
    return config_dir() + "/credentials.json";
}

std::optional<Credentials> load_credentials() {
    try {
        std::ifstream f(credentials_path());
        if (!f) return std::nullopt;

        nlohmann::json j;
        f >> j;

        Credentials c;
        c.access_token  = j.at("access_token").get<std::string>();
        c.refresh_token = j.at("refresh_token").get<std::string>();
        c.expires_at    = j.at("expires_at").get<int64_t>();
        c.email         = j.value("email", std::string());
        c.api_url       = j.value("api_url", std::string());
        return c;
    } catch (...) {
        return std::nullopt;
    }
}

bool save_credentials(const Credentials& creds) {
    try {
        const auto path = credentials_path();
        std::filesystem::create_directories(std::filesystem::path(path).parent_path());

        nlohmann::json j{
            {"access_token",  creds.access_token},
            {"refresh_token", creds.refresh_token},
            {"expires_at",    creds.expires_at},
        };
        if (!creds.email.empty()) {
            j["email"] = creds.email;
        }
        if (!creds.api_url.empty()) {
            j["api_url"] = creds.api_url;
        }

        std::ofstream f(path);
        if (!f) return false;
        f << j.dump(2) << "\n";
        return true;
    } catch (...) {
        return false;
    }
}

bool delete_credentials() {
    std::error_code ec;
    return std::filesystem::remove(credentials_path(), ec);
}

}  // namespace remindr

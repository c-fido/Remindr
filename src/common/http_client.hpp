#pragma once

#include <optional>
#include <string>

namespace remindr {

struct HttpResponse {
    int         status = 0;
    std::string body;
};

// Base URL without trailing slash, e.g. http://localhost:8080
std::string api_base_url();

HttpResponse http_request(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    const std::optional<std::string>& bearer_token);

// POST JSON; refreshes access token once on 401 if credentials exist on disk.
HttpResponse http_post_json(
    const std::string& path,
    const std::string& json_body,
    bool auth = true);

}  // namespace remindr

#include "http_client.hpp"

#include "credentials.hpp"
#include "json.hpp"

#include <curl/curl.h>
#include <cstdlib>
#include <ctime>

namespace remindr {

namespace {

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

bool refresh_access_token() {
    auto creds = load_credentials();
    if (!creds) return false;

    const std::string url = api_base_url() + "/v1/auth/refresh";
    nlohmann::json req{{"refresh_token", creds->refresh_token}};
    const std::string body = req.dump();

    HttpResponse resp = http_request("POST", url, body, std::nullopt);
    if (resp.status != 200) return false;

    try {
        auto j = nlohmann::json::parse(resp.body);
        Credentials updated;
        updated.access_token  = j.at("access_token").get<std::string>();
        updated.refresh_token = j.at("refresh_token").get<std::string>();
        const int expires_in  = j.at("expires_in").get<int>();
        updated.expires_at    = static_cast<int64_t>(std::time(nullptr)) + expires_in;
        return save_credentials(updated);
    } catch (...) {
        return false;
    }
}

}  // namespace

std::string api_base_url() {
    const char* env = std::getenv("REMINDR_API_URL");
    std::string base;
    if (env && env[0] != '\0') {
        base = env;
    } else if (auto creds = load_credentials()) {
        base = creds->api_url;
    }
    if (base.empty()) {
        base = "http://localhost:8080";
    }
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base;
}

HttpResponse http_request(
    const std::string& method,
    const std::string& url,
    const std::string& body,
    const std::optional<std::string>& bearer_token)
{
    HttpResponse result;

    CURL* curl = curl_easy_init();
    if (!curl) return result;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (bearer_token) {
        const std::string auth = "Authorization: Bearer " + *bearer_token;
        headers = curl_slist_append(headers, auth.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    if (!body.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    }

    CURLcode rc = curl_easy_perform(curl);
    if (rc == CURLE_OK) {
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        result.status = static_cast<int>(code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}

HttpResponse http_post_json(
    const std::string& path,
    const std::string& json_body,
    bool auth)
{
    const std::string url = api_base_url() + path;
    std::optional<std::string> token;
    if (auth) {
        if (auto creds = load_credentials()) {
            token = creds->access_token;
        }
    }

    HttpResponse resp = http_request("POST", url, json_body, token);
    if (auth && resp.status == 401 && refresh_access_token()) {
        if (auto creds = load_credentials()) {
            resp = http_request("POST", url, json_body, creds->access_token);
        }
    }
    return resp;
}

}  // namespace remindr

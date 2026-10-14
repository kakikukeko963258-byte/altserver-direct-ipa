#pragma once
#include <WinSock2.h>
#include <cpprest/http_client.h>
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include "AppleRootCA.h"

// Only the private Apple GSA endpoint uses Apple's private root. Trust is
// scoped to this client; Windows trust stores are never changed.
class GSAHttpClient {
    struct State {
        std::mutex mutex;
        std::vector<std::string> cookies;
    };
    struct Result {
        web::http::http_response response;
        std::vector<unsigned char> body;
    };
    std::shared_ptr<State> state = std::make_shared<State>();
    static size_t Body(char* bytes, size_t size, size_t count, void* context) {
        try {
            auto result = static_cast<Result*>(context);
            size_t length = size * count;
            if (length > 4 * 1024 * 1024 || result->body.size() > 4 * 1024 * 1024 - length) return 0;
            result->body.insert(result->body.end(), bytes, bytes + length);
            return length;
        } catch (...) { return 0; }
    }
    static size_t Header(char* bytes, size_t size, size_t count, void* context) {
        size_t length = size * count;
        std::string line(bytes, length);
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            auto value = line.substr(colon + 1);
            auto begin = value.find_first_not_of(" \t\r\n");
            auto end = value.find_last_not_of(" \t\r\n");
            if (begin != std::string::npos) {
                try {
                    static_cast<Result*>(context)->response.headers().add(
                        utility::conversions::to_string_t(line.substr(0, colon)),
                        utility::conversions::to_string_t(value.substr(begin, end - begin + 1)));
                } catch (...) { return 0; }
            }
        }
        return length;
    }
public:
    explicit GSAHttpClient(const utility::string_t& endpoint) {
        if (endpoint != U("https://gsa.apple.com")) throw std::invalid_argument("Unsupported Apple authentication endpoint.");
    }
    static bool IsSafePath(const std::string& path) {
        return !path.empty() && path[0] == '/' && path.compare(0, 2, "//") != 0 &&
            path.find_first_of("\r\n\\") == std::string::npos;
    }
    pplx::task<web::http::http_response> request(web::http::http_request request) const {
        auto shared = state;
        return pplx::create_task([request, shared]() mutable {
            static std::once_flag initialized;
            std::call_once(initialized, [] {
                if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
                    throw std::runtime_error("Could not initialize Apple TLS transport.");
            });
            auto path = utility::conversions::to_utf8string(request.request_uri().to_string());
            if (!IsSafePath(path)) throw std::invalid_argument("Unsupported Apple authentication URL.");
            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle(curl_easy_init(), curl_easy_cleanup);
            if (!handle) throw std::runtime_error("Could not create Apple TLS request.");
            std::lock_guard<std::mutex> lock(shared->mutex);
            std::vector<unsigned char> body;
            if (request.body().is_valid()) body = request.extract_vector().get();
            struct Wipe {
                std::vector<unsigned char>& data;
                ~Wipe() { if (!data.empty()) SecureZeroMemory(data.data(), data.size()); }
            } wipe{body};
            auto method = utility::conversions::to_utf8string(request.method());
            auto url = "https://gsa.apple.com" + path;
            curl_slist* headers = nullptr;
            for (auto& header : request.headers()) {
                auto name = utility::conversions::to_utf8string(header.first);
                auto value = utility::conversions::to_utf8string(header.second);
                if (name.find_first_of("\r\n") != std::string::npos || value.find_first_of("\r\n") != std::string::npos)
                    throw std::invalid_argument("Invalid Apple authentication header.");
                auto next = curl_slist_append(headers, (name + ": " + value).c_str());
                if (!next) { curl_slist_free_all(headers); throw std::bad_alloc(); }
                headers = next;
            }
            std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headerOwner(headers, curl_slist_free_all);
            curl_blob root{const_cast<char*>(AppleRootCAPem), sizeof(AppleRootCAPem) - 1, CURL_BLOB_COPY};
            Result result;
            curl_easy_setopt(handle.get(), CURLOPT_URL, url.c_str());
            curl_easy_setopt(handle.get(), CURLOPT_PROTOCOLS_STR, "https");
            curl_easy_setopt(handle.get(), CURLOPT_FOLLOWLOCATION, 0L);
            curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(handle.get(), CURLOPT_SSL_VERIFYHOST, 2L);
            if (curl_easy_setopt(handle.get(), CURLOPT_CAINFO_BLOB, &root) != CURLE_OK)
                throw std::runtime_error("Apple's trusted root certificate could not be configured.");
            curl_easy_setopt(handle.get(), CURLOPT_CONNECTTIMEOUT, 20L);
            curl_easy_setopt(handle.get(), CURLOPT_TIMEOUT, 60L);
            curl_easy_setopt(handle.get(), CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(handle.get(), CURLOPT_ACCEPT_ENCODING, "");
            curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(handle.get(), CURLOPT_CUSTOMREQUEST, method.c_str());
            if (method != "GET" && method != "HEAD") {
                curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS, body.empty() ? "" : reinterpret_cast<const char*>(body.data()));
                curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
            }
            curl_easy_setopt(handle.get(), CURLOPT_COOKIEFILE, "");
            for (auto& cookie : shared->cookies) curl_easy_setopt(handle.get(), CURLOPT_COOKIELIST, cookie.c_str());
            curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, Body);
            curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, &result);
            curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, Header);
            curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, &result);
            auto code = curl_easy_perform(handle.get());
            if (code != CURLE_OK) throw std::runtime_error(std::string("Apple authentication connection failed: ") + curl_easy_strerror(code));
            curl_slist* cookies = nullptr;
            if (curl_easy_getinfo(handle.get(), CURLINFO_COOKIELIST, &cookies) == CURLE_OK) {
                shared->cookies.clear();
                for (auto item = cookies; item; item = item->next) shared->cookies.emplace_back(item->data);
                curl_slist_free_all(cookies);
            }
            long status = 0;
            curl_easy_getinfo(handle.get(), CURLINFO_RESPONSE_CODE, &status);
            result.response.set_status_code(static_cast<web::http::status_code>(status));
            result.response.set_body(result.body);
            return result.response;
        });
    }
};

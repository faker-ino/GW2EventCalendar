#include "rssfeed.h"

#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <cstring>

#include <pugixml.hpp>

#include "rfc822date.h"

namespace {

// Minimal blocking HTTPS GET over WinHTTP - same pattern used throughout
// this addon and the sibling BigBomb addon's gw2api.cpp/iconcache.cpp.
std::string HttpsGet(const wchar_t* host, const std::wstring& path) {
    std::string result;

    HINTERNET hSession = WinHttpOpen(L"GW2EventCalendar/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        return result;
    }

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return result;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    BOOL sent = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                    WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    BOOL received = sent && WinHttpReceiveResponse(hRequest, nullptr);

    if (received) {
        for (;;) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &available) || available == 0) {
                break;
            }
            std::vector<char> buffer(available);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buffer.data(), available, &read)) {
                break;
            }
            result.append(buffer.data(), read);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

bool SplitUrl(const std::string& url, std::wstring& outHost, std::wstring& outPath) {
    constexpr const char* kPrefix = "https://";
    size_t prefixLen = strlen(kPrefix);
    if (url.compare(0, prefixLen, kPrefix) != 0) {
        return false;
    }
    size_t pathStart = url.find('/', prefixLen);
    if (pathStart == std::string::npos) {
        return false;
    }
    std::string host = url.substr(prefixLen, pathStart - prefixLen);
    std::string path = url.substr(pathStart);
    outHost.assign(host.begin(), host.end());
    outPath.assign(path.begin(), path.end());
    return true;
}

bool ParseRssFeed(const std::string& xmlBody, std::vector<Event>& out) {
    pugi::xml_document doc;
    if (!doc.load_buffer(xmlBody.data(), xmlBody.size())) {
        return false;
    }
    pugi::xml_node channel = doc.child("rss").child("channel");
    if (!channel) {
        return false;
    }

    std::vector<Event> parsed;
    for (pugi::xml_node item : channel.children("item")) {
        Event e;
        e.uid = item.child("guid").text().as_string();
        e.title = item.child("title").text().as_string();
        e.detail_url = item.child("link").text().as_string();
        e.start_utc = ParseRfc822(item.child("pubDate").text().as_string());
        e.end_utc = ParseRfc822(item.child("endDate").text().as_string());
        if (e.end_utc == 0) {
            e.end_utc = e.start_utc; // no endDate - treat as a single-instant event
        }
        if (e.uid.empty() || e.start_utc == 0) {
            continue; // malformed entry - skip rather than show a garbage date
        }
        parsed.push_back(std::move(e));
    }

    out = std::move(parsed);
    return true; // zero items is still a successful fetch, not a failure
}

bool FetchUrl(const std::string& url, std::vector<Event>& out) {
    std::wstring host, path;
    if (!SplitUrl(url, host, path)) {
        return false;
    }
    std::string body = HttpsGet(host.c_str(), path);
    if (body.empty()) {
        return false;
    }
    return ParseRssFeed(body, out);
}

} // namespace

bool FetchAndParseRssFeed(const std::string& feedUrlOverride,
                           const std::string& feedMemberId,
                           const std::string& feedKey,
                           std::vector<Event>& out) {
    if (!feedUrlOverride.empty()) {
        return FetchUrl(feedUrlOverride, out);
    }

    constexpr const char* kBaseUrl = "https://en-forum.guildwars2.com/events/events.xml/";
    if (FetchUrl(kBaseUrl, out)) {
        return true;
    }

    if (!feedMemberId.empty() && !feedKey.empty()) {
        std::string authUrl = std::string(kBaseUrl) + "?member=" + feedMemberId + "&key=" + feedKey;
        return FetchUrl(authUrl, out);
    }

    return false;
}

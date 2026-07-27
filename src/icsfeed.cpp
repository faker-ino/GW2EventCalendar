#include "icsfeed.h"

#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "textsanitize.h"

namespace {

constexpr const char* kBaseUrl = "https://en-forum.guildwars2.com/events/1-game-updates/download/";

// Same request pattern as wikifeed.cpp's HttpsGet - duplicated
// rather than shared across translation units, since each is the only caller
// it has.
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

// Splits the raw .ics body into logical (already-unfolded) lines. RFC5545
// "folds" long property lines by breaking them at arbitrary points and
// continuing on the next line, marked by a single leading space or tab -
// real data from this feed does this to SUMMARY (e.g. a title split
// mid-word). CRLF and bare-LF line endings are both accepted.
std::vector<std::string> UnfoldLines(const std::string& body) {
    std::vector<std::string> rawLines;
    size_t start = 0;
    for (size_t i = 0; i <= body.size(); ++i) {
        if (i == body.size() || body[i] == '\n') {
            std::string line = body.substr(start, i - start);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            rawLines.push_back(std::move(line));
            start = i + 1;
        }
    }

    std::vector<std::string> unfolded;
    for (const std::string& line : rawLines) {
        if (!line.empty() && (line[0] == ' ' || line[0] == '\t') && !unfolded.empty()) {
            unfolded.back() += line.substr(1);
        } else {
            unfolded.push_back(line);
        }
    }
    return unfolded;
}

// Splits "NAME;PARAM1;PARAM2=x:value" into name/params/value. Good enough
// for this feed's actual properties (DTSTART/DTEND/SUMMARY/UID) - none of
// which carry a quoted param value containing a colon, so a plain first-':'
// split is safe, unlike a general RFC5545 parser would need to be.
bool SplitProperty(const std::string& line, std::string& name, std::string& params, std::string& value) {
    size_t colon = line.find(':');
    if (colon == std::string::npos) {
        return false;
    }
    std::string left = line.substr(0, colon);
    value = line.substr(colon + 1);
    size_t semi = left.find(';');
    if (semi == std::string::npos) {
        name = left;
        params.clear();
    } else {
        name = left.substr(0, semi);
        params = left.substr(semi + 1);
    }
    return true;
}

// Unescapes RFC5545 TEXT values (\\ \, \; \n \N). Byte-wise scanning is
// UTF-8-safe here since continuation bytes are always >= 0x80, never equal
// to any of these ASCII escape characters.
std::string UnescapeIcsText(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[i + 1];
            if (next == 'n' || next == 'N') {
                out += '\n';
                i++;
                continue;
            }
            if (next == ',' || next == ';' || next == '\\') {
                out += next;
                i++;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

// Parses a DTSTART/DTEND value: either a bare "YYYYMMDD" date (when the
// property carries a "VALUE=DATE" param - midnight UTC on that day, same
// convention as ParseWikiDate in wikidate.cpp) or a full
// "YYYYMMDDTHHMMSSZ" UTC datetime. Returns 0 on any parse failure, which
// callers treat as "skip this event" - same convention as ParseWikiDate.
time_t ParseIcsDateTime(const std::string& value, bool dateOnly) {
    int year = 0, month = 0, day = 0, hh = 0, mm = 0, ss = 0;
    if (dateOnly) {
        if (value.size() < 8) {
            return 0;
        }
        year = atoi(value.substr(0, 4).c_str());
        month = atoi(value.substr(4, 2).c_str());
        day = atoi(value.substr(6, 2).c_str());
    } else {
        if (value.size() < 15 || value[8] != 'T') {
            return 0;
        }
        year = atoi(value.substr(0, 4).c_str());
        month = atoi(value.substr(4, 2).c_str());
        day = atoi(value.substr(6, 2).c_str());
        hh = atoi(value.substr(9, 2).c_str());
        mm = atoi(value.substr(11, 2).c_str());
        ss = atoi(value.substr(13, 2).c_str());
    }
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return 0;
    }

    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hh;
    t.tm_min = mm;
    t.tm_sec = ss;
    return _mkgmtime(&t); // interprets as UTC directly - correct since these values are always Z-suffixed or bare dates
}

// UIDs look like "289-1-4ce01e018f60fd86897ed78d2f7f177a@en-forum.guildwars2.com" -
// the leading number is the forum event id, the same bare numeric string
// used as e.uid from the old RSS feed's <guid> and from wikifeed.cpp's
// ExtractEventId, so the existing wiki-merge matching in eventfetcher.cpp
// needs no changes to work against this source instead.
std::string ExtractForumId(const std::string& uidValue) {
    size_t i = 0;
    while (i < uidValue.size() && isdigit((unsigned char)uidValue[i])) {
        i++;
    }
    return uidValue.substr(0, i);
}

} // namespace

bool FetchAndParseIcsFeed(const std::string& feedUrlOverride, std::vector<Event>& out) {
    std::string body;
    if (!feedUrlOverride.empty()) {
        std::wstring host, path;
        if (!SplitUrl(feedUrlOverride, host, path)) {
            return false;
        }
        body = HttpsGet(host.c_str(), path);
    } else {
        std::wstring host, path;
        if (!SplitUrl(kBaseUrl, host, path)) {
            return false;
        }
        body = HttpsGet(host.c_str(), path);
    }
    if (body.empty()) {
        return false;
    }

    std::vector<std::string> lines = UnfoldLines(body);

    std::vector<Event> parsed;
    bool inEvent = false;
    bool hasDtend = false;
    Event current;

    for (const std::string& line : lines) {
        if (line == "BEGIN:VEVENT") {
            inEvent = true;
            hasDtend = false;
            current = Event();
            continue;
        }
        if (line == "END:VEVENT") {
            if (inEvent) {
                if (!hasDtend || current.end_utc < current.start_utc) {
                    current.end_utc = current.start_utc;
                }
                if (!current.uid.empty() && current.start_utc != 0) {
                    current.detail_url = "https://en-forum.guildwars2.com/events/event/" + current.uid + "/";
                    parsed.push_back(current);
                }
            }
            inEvent = false;
            continue;
        }
        if (!inEvent) {
            continue; // ignores VCALENDAR/VTIMEZONE properties with no special-casing needed
        }

        std::string name, params, value;
        if (!SplitProperty(line, name, params, value)) {
            continue;
        }

        if (name == "UID") {
            current.uid = ExtractForumId(value);
        } else if (name == "SUMMARY") {
            current.title = SanitizeForDisplay(UnescapeIcsText(value));
        } else if (name == "DTSTART") {
            current.start_utc = ParseIcsDateTime(value, params.find("VALUE=DATE") != std::string::npos);
        } else if (name == "DTEND") {
            current.end_utc = ParseIcsDateTime(value, params.find("VALUE=DATE") != std::string::npos);
            hasDtend = true;
        }
    }

    out = std::move(parsed);
    return true; // zero parsed events is still a successful fetch
}

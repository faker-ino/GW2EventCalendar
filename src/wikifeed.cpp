#include "wikifeed.h"

#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include <cctype>
#include <cstdlib>
#include <cstring>

#include "textsanitize.h"
#include "wikidate.h"

namespace {

constexpr wchar_t kWikiHost[] = L"wiki.guildwars2.com";
constexpr wchar_t kWikiPath[] = L"/index.php?title=Special_event&action=render";

// Same request pattern as icsfeed.cpp's HttpsGet - duplicated rather than
// shared across translation units, since each is the only caller it has.
std::string HttpsGet(const wchar_t* host, const wchar_t* path) {
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

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr,
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

std::string Trim(const std::string& s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return "";
    }
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

// Decodes the small set of HTML entities MediaWiki's renderer actually
// emits in this page's content (numeric refs for smart quotes/apostrophes,
// plus the handful of named ones) into UTF-8. Not a general entity decoder.
std::string DecodeEntities(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] != '&') {
            out += s[i++];
            continue;
        }
        size_t semi = s.find(';', i);
        if (semi == std::string::npos || semi - i > 10) {
            out += s[i++];
            continue;
        }
        std::string ent = s.substr(i + 1, semi - i - 1);
        unsigned long cp = 0;
        bool known = true;
        if (!ent.empty() && ent[0] == '#') {
            if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) {
                cp = strtoul(ent.c_str() + 2, nullptr, 16);
            } else {
                cp = strtoul(ent.c_str() + 1, nullptr, 10);
            }
        } else if (ent == "amp") { cp = '&'; }
        else if (ent == "lt") { cp = '<'; }
        else if (ent == "gt") { cp = '>'; }
        else if (ent == "quot") { cp = '"'; }
        else if (ent == "nbsp") { cp = ' '; }
        else { known = false; }

        if (!known || cp == 0) {
            out += s[i++];
            continue;
        }

        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
        i = semi + 1;
    }
    return out;
}

// Strips "<...>" tags and collapses whitespace runs, then decodes entities.
// Good enough for the small, well-formed table-cell fragments this page
// produces - not a general HTML-to-text converter.
std::string StripTags(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool inTag = false;
    for (char c : html) {
        if (c == '<') { inTag = true; continue; }
        if (c == '>') { inTag = false; continue; }
        if (inTag) { continue; }
        out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
    }
    out = DecodeEntities(out);

    std::string collapsed;
    collapsed.reserve(out.size());
    bool lastSpace = false;
    for (char c : out) {
        if (c == ' ') {
            if (!lastSpace) { collapsed += c; }
            lastSpace = true;
        } else {
            collapsed += c;
            lastSpace = false;
        }
    }
    return Trim(collapsed);
}

// Extracts the inner content of the next <td>...</td> at/after `pos`,
// advancing `pos` past it. No nested <td> handling needed - none of this
// page's cells nest another table.
bool ExtractNextCell(const std::string& html, size_t& pos, std::string& outContent) {
    size_t tdStart = html.find("<td", pos);
    if (tdStart == std::string::npos) {
        return false;
    }
    size_t tagClose = html.find('>', tdStart);
    if (tagClose == std::string::npos) {
        return false;
    }
    size_t cellEnd = html.find("</td>", tagClose);
    if (cellEnd == std::string::npos) {
        return false;
    }
    outContent = html.substr(tagClose + 1, cellEnd - tagClose - 1);
    pos = cellEnd + strlen("</td>");
    return true;
}

// Description cell: an italicized blurb followed by a "<p><b>Official
// page:</b> <a href=...>...</p>" paragraph. Only the blurb (before that
// paragraph) is the description; the link is pulled out separately below.
std::string ExtractDescription(const std::string& cellHtml) {
    size_t pPos = cellHtml.find("<p>");
    return StripTags(pPos == std::string::npos ? cellHtml : cellHtml.substr(0, pPos));
}

std::string ExtractOfficialPageUrl(const std::string& cellHtml) {
    size_t labelPos = cellHtml.find("Official page:");
    if (labelPos == std::string::npos) {
        return "";
    }
    size_t hrefPos = cellHtml.find("href=\"", labelPos);
    if (hrefPos == std::string::npos) {
        return "";
    }
    hrefPos += strlen("href=\"");
    size_t hrefEnd = cellHtml.find('"', hrefPos);
    if (hrefEnd == std::string::npos) {
        return "";
    }
    return cellHtml.substr(hrefPos, hrefEnd - hrefPos);
}

// Features cell: a "<ul><li>...</li>...</ul>" list. Flattened to one
// "- " line per bullet; falls back to the whole cell's text if for some
// reason there's no <li> markup at all.
std::string ExtractFeatures(const std::string& cellHtml) {
    std::string result;
    size_t pos = 0;
    while (true) {
        size_t liStart = cellHtml.find("<li", pos);
        if (liStart == std::string::npos) {
            break;
        }
        size_t tagClose = cellHtml.find('>', liStart);
        if (tagClose == std::string::npos) {
            break;
        }
        size_t liEnd = cellHtml.find("</li>", tagClose);
        if (liEnd == std::string::npos) {
            break;
        }
        std::string item = StripTags(cellHtml.substr(tagClose + 1, liEnd - tagClose - 1));
        if (!item.empty()) {
            if (!result.empty()) { result += "\n"; }
            result += "- " + item;
        }
        pos = liEnd + strlen("</li>");
    }
    if (result.empty()) {
        result = StripTags(cellHtml);
    }
    return result;
}

// Bonus-effects cell: "<span class="inline-icon effect"><a ...><img
// ...></a></span> <a href=... title="Name">Name</a>: description...". May
// be entirely empty (most events have no bonus effect at all) or - rare -
// contain more than one effect; only the first is extracted, matching the
// day-detail popup's single name+description slot. The icon itself is never
// pulled out - the UI always shows the static fallback icon there instead.
void ExtractBonusEffect(const std::string& cellHtml, std::string& outName, std::string& outDescription) {
    size_t spanPos = cellHtml.find("inline-icon effect");
    if (spanPos == std::string::npos) {
        return; // no bonus effect in this cell - leave everything empty
    }

    size_t spanCloseTag = cellHtml.find("</span>", spanPos);
    size_t nameLinkStart = (spanCloseTag != std::string::npos) ? cellHtml.find("<a", spanCloseTag) : std::string::npos;
    if (nameLinkStart == std::string::npos) {
        return;
    }
    size_t nameLinkTagEnd = cellHtml.find('>', nameLinkStart);
    size_t nameLinkClose = (nameLinkTagEnd != std::string::npos) ? cellHtml.find("</a>", nameLinkTagEnd) : std::string::npos;
    if (nameLinkTagEnd == std::string::npos || nameLinkClose == std::string::npos) {
        return;
    }
    outName = StripTags(cellHtml.substr(nameLinkTagEnd + 1, nameLinkClose - nameLinkTagEnd - 1));

    std::string rest = StripTags(cellHtml.substr(nameLinkClose + strlen("</a>")));
    if (!rest.empty() && rest[0] == ':') {
        rest = Trim(rest.substr(1));
    }
    outDescription = rest;
}

struct HeaderInfo {
    std::string title;
    time_t startUtc = 0;
    time_t endUtc = 0;
};

// Header row: "<a href="//wiki.guildwars2.com/wiki/Page" title="...">Title
// </a>: July 21, 2026 — July 28, 2026<small>(duration: ...)</small><span
// style="float: right">Modes</span>". Handles the no-dash single-day case
// (start == end) the same way the RSS/wikitext parsers already do.
bool ParseHeaderCell(const std::string& thHtml, HeaderInfo& out) {
    size_t aStart = thHtml.find("<a");
    if (aStart == std::string::npos) {
        return false;
    }
    size_t aTagEnd = thHtml.find('>', aStart);
    size_t aClose = (aTagEnd != std::string::npos) ? thHtml.find("</a>", aTagEnd) : std::string::npos;
    if (aTagEnd == std::string::npos || aClose == std::string::npos) {
        return false;
    }

    out.title = StripTags(thHtml.substr(aTagEnd + 1, aClose - aTagEnd - 1));
    if (out.title.empty()) {
        return false;
    }

    size_t afterLink = aClose + strlen("</a>");
    size_t smallPos = thHtml.find("<small", afterLink);
    std::string dateRangeText = StripTags(
        thHtml.substr(afterLink, (smallPos == std::string::npos ? thHtml.size() : smallPos) - afterLink));

    size_t colon = dateRangeText.find(':');
    if (colon != std::string::npos) {
        dateRangeText = dateRangeText.substr(colon + 1);
    }

    size_t dashPos = dateRangeText.find("\xE2\x80\x94"); // em dash (U+2014), UTF-8
    std::string startText = dashPos == std::string::npos ? dateRangeText : dateRangeText.substr(0, dashPos);
    std::string endText = dashPos == std::string::npos ? dateRangeText : dateRangeText.substr(dashPos + 3);

    out.startUtc = ParseWikiDate(startText);
    out.endUtc = (dashPos == std::string::npos) ? out.startUtc : ParseWikiDate(endText);
    if (out.endUtc == 0) {
        out.endUtc = out.startUtc;
    }
    return out.startUtc != 0;
}

// "official page" links look like
// ".../events/event/291-wvw-rush-event/" - the leading number is the same
// id the RSS feed exposes as <guid>, so extracting it lets eventfetcher.cpp
// match a wiki row to its RSS event precisely instead of fuzzy-matching on
// title/date. Non-forum official-page links (a handful of pre-forum
// historical events link to old news posts instead) simply don't match and
// fall back to the synthetic "wiki:..." id below.
std::string ExtractEventId(const std::string& url) {
    constexpr const char* kMarker = "/events/event/";
    size_t pos = url.find(kMarker);
    if (pos == std::string::npos) {
        return "";
    }
    size_t numStart = pos + strlen(kMarker);
    size_t numEnd = numStart;
    while (numEnd < url.size() && isdigit((unsigned char)url[numEnd])) {
        numEnd++;
    }
    return url.substr(numStart, numEnd - numStart);
}

} // namespace

bool FetchAndParseWikiEvents(std::vector<Event>& out) {
    std::string html = HttpsGet(kWikiHost, kWikiPath);
    if (html.empty()) {
        return false;
    }

    std::vector<Event> parsed;
    size_t pos = 0;
    while (true) {
        size_t headerPos = html.find("<tr class=\"line\" id=\"", pos);
        if (headerPos == std::string::npos) {
            break;
        }
        size_t headerTagEnd = html.find('>', headerPos);
        size_t headerRowEnd = (headerTagEnd != std::string::npos) ? html.find("</tr>", headerTagEnd) : std::string::npos;
        if (headerTagEnd == std::string::npos || headerRowEnd == std::string::npos) {
            break; // malformed - stop rather than loop forever
        }
        pos = headerRowEnd + strlen("</tr>");

        HeaderInfo hdr;
        if (!ParseHeaderCell(html.substr(headerTagEnd + 1, headerRowEnd - headerTagEnd - 1), hdr)) {
            continue; // not enough to show a meaningful entry
        }

        Event e;
        e.title = SanitizeForDisplay(hdr.title);
        e.start_utc = hdr.startUtc;
        e.end_utc = hdr.endUtc;

        // The data row (Description/Features/Bonus effects cells) is the
        // next "<tr class=\"line\">" (no id attribute, unlike the header) -
        // only consume it if it's actually still part of this event, i.e.
        // it comes before the next header row.
        size_t dataRowPos = html.find("<tr class=\"line\">", headerRowEnd);
        size_t nextHeaderPos = html.find("<tr class=\"line\" id=\"", headerRowEnd);
        if (dataRowPos != std::string::npos && (nextHeaderPos == std::string::npos || dataRowPos < nextHeaderPos)) {
            size_t dataTagEnd = html.find('>', dataRowPos);
            size_t dataRowEnd = (dataTagEnd != std::string::npos) ? html.find("</tr>", dataTagEnd) : std::string::npos;
            if (dataTagEnd != std::string::npos && dataRowEnd != std::string::npos) {
                std::string dataRowHtml = html.substr(dataTagEnd + 1, dataRowEnd - dataTagEnd - 1);

                size_t cellPos = 0;
                std::string cell1, cell2, cell3;
                ExtractNextCell(dataRowHtml, cellPos, cell1);
                ExtractNextCell(dataRowHtml, cellPos, cell2);
                ExtractNextCell(dataRowHtml, cellPos, cell3);

                e.description = SanitizeForDisplay(ExtractDescription(cell1));
                e.detail_url = ExtractOfficialPageUrl(cell1);
                e.features = SanitizeForDisplay(ExtractFeatures(cell2));
                ExtractBonusEffect(cell3, e.bonus_effect_name, e.bonus_effect_description);
                e.bonus_effect_name = SanitizeForDisplay(e.bonus_effect_name);
                e.bonus_effect_description = SanitizeForDisplay(e.bonus_effect_description);

                pos = dataRowEnd + strlen("</tr>");
            }
        }

        std::string id = ExtractEventId(e.detail_url);
        e.uid = !id.empty() ? id : ("wiki:" + e.title + "|" + std::to_string((long long)e.start_utc));
        parsed.push_back(std::move(e));
    }

    out = std::move(parsed);
    return true; // zero parsed rows is still a successful fetch
}

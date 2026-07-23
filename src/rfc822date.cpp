#include "rfc822date.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

int MonthFromName(const char* mon) {
    static const char* const kMonths[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };
    for (int i = 0; i < 12; ++i) {
        if (_stricmp(mon, kMonths[i]) == 0) {
            return i + 1;
        }
    }
    return 0;
}

} // namespace

time_t ParseRfc822(const std::string& s) {
    // "Tue, 14 Jul 2026 16:00:00 +0000"
    char weekday[8] = {};
    char monthName[8] = {};
    int day = 0, year = 0, hh = 0, mm = 0, ss = 0;
    char tz[8] = {};

    int matched = sscanf(s.c_str(), "%7[^,], %d %7s %d %d:%d:%d %7s",
                          weekday, &day, monthName, &year, &hh, &mm, &ss, tz);
    if (matched < 7) {
        return 0; // unexpected format - caller skips this event
    }

    int month = MonthFromName(monthName);
    if (month == 0 || day < 1 || day > 31) {
        return 0;
    }

    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hh;
    t.tm_min = mm;
    t.tm_sec = ss;

    // _mkgmtime interprets the struct as UTC directly (no local-timezone
    // conversion, unlike mktime) - correct here since the feed's trailing
    // "+0000" confirms every timestamp is already UTC.
    return _mkgmtime(&t);
}

time_t ParseWikiDate(const std::string& s) {
    int month = 0, day = 0, year = 0;
    std::string token;
    // Trailing ' ' forces the last token to flush through the same path as
    // every other one instead of needing a duplicated post-loop check.
    for (size_t i = 0; i <= s.size(); ++i) {
        char c = (i < s.size()) ? s[i] : ' ';
        if (isalnum((unsigned char)c)) {
            token += c;
            continue;
        }
        if (!token.empty()) {
            if (isalpha((unsigned char)token[0])) {
                // MonthFromName matches the RFC822 3-letter abbreviations
                // ("Jul"); the wiki spells months out in full ("July"), and
                // every English month name's first 3 letters happen to be
                // its abbreviation, so truncate before comparing.
                int m = MonthFromName(token.substr(0, 3).c_str());
                if (m != 0) {
                    month = m;
                }
            } else {
                int value = atoi(token.c_str());
                if (token.size() == 4) {
                    year = value;
                } else if (value >= 1 && value <= 31) {
                    day = value;
                }
            }
            token.clear();
        }
    }
    if (month == 0 || day == 0 || year == 0) {
        return 0;
    }

    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    return _mkgmtime(&t); // midnight UTC on that day, same convention as ParseRfc822
}

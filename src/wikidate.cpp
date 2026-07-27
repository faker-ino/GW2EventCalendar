#include "wikidate.h"

#include <cctype>
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
    return _mkgmtime(&t); // midnight UTC on that day - _mkgmtime interprets the struct as UTC directly, no local-timezone conversion
}

#pragma once

#include <ctime>
#include <map>
#include <string>
#include <vector>

// One event from the forum's RSS feed. The feed carries no image, location,
// or all-day-flag data at all - see rssfeed.cpp for the exact fields it does
// provide. The description/features/bonus_effect_* fields below never come
// from the feed - they're filled in from the GW2 wiki's "Special event" page
// (see wikifeed.cpp) when that page tracks a matching event, and stay empty
// otherwise. No event image is fetched from anywhere - the UI always shows
// the static fallback calendar icon (see calendarui.cpp's GetFallbackTexture).
struct Event {
    std::string uid;             // from <guid> - stable unique key
    std::string title;
    time_t      start_utc = 0;   // from <pubDate>, RFC822
    time_t      end_utc   = 0;   // from <endDate>, RFC822 (or == start_utc if absent)
    std::string detail_url;      // from <link>

    std::string description;            // short blurb from the wiki's event table - empty if none
    std::string features;               // bullet list from the wiki's "Features" column, one "- " line per bullet - empty if none
    std::string bonus_effect_name;      // e.g. "Greater Call of the Mists (WvW)" - empty if this event has no bonus effect
    std::string bonus_effect_description; // e.g. "Earn increased rewards in WvW during events! +100% WXP ..."
};

// "YYYY-MM-DD" (UTC) -> events spanning that day, sorted by start time.
using EventsByDate = std::map<std::string, std::vector<Event>>;

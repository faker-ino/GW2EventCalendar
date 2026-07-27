#pragma once

#include <ctime>
#include <map>
#include <string>
#include <vector>

// One event from the forum's "Game Updates" calendar ICS feed. That feed
// carries no image, location, or all-day-flag data at all - see icsfeed.cpp
// for the exact fields it does provide. The description/features/
// bonus_effect_* fields below never come from it - they're filled in from
// the GW2 wiki's "Special event" page (see wikifeed.cpp) when that page
// tracks a matching event, and stay empty otherwise. No event image is
// fetched from anywhere - the UI always shows the static fallback calendar
// icon (see calendarui.cpp's GetFallbackTexture).
struct Event {
    std::string uid;             // forum event id - from ICS UID's leading number - stable unique key
    std::string title;
    time_t      start_utc = 0;   // from ICS DTSTART, UTC
    time_t      end_utc   = 0;   // from ICS DTEND, UTC (or == start_utc if absent/invalid)
    std::string detail_url;      // reconstructed forum permalink from uid

    std::string description;            // short blurb from the wiki's event table - empty if none
    std::string features;               // bullet list from the wiki's "Features" column, one "- " line per bullet - empty if none
    std::string bonus_effect_name;      // e.g. "Greater Call of the Mists (WvW)" - empty if this event has no bonus effect
    std::string bonus_effect_description; // e.g. "Earn increased rewards in WvW during events! +100% WXP ..."
};

// "YYYY-MM-DD" (UTC) -> events spanning that day, sorted by start time.
using EventsByDate = std::map<std::string, std::vector<Event>>;

#pragma once

#include <string>

// Persisted addon configuration. feed_member_id/feed_key are the user's own
// forum account credentials for the events RSS feed - never hardcode real
// values anywhere in source, they only ever live in the on-disk
// settings.json under the user's own GW2 install (see .gitignore).
struct Settings {
    bool        window_visible = true;
    std::string feed_member_id;
    std::string feed_key;
    std::string feed_url_override;
    int         months_back = 1;
    int         months_forward = 3;
    int         refresh_interval_minutes = 180;
    bool        week_starts_monday = true; // false = Sunday-first (US-style), true = Monday-first
    bool        show_quickaccess_icon = true;
};

extern Settings g_settings;

// Missing/malformed file keeps defaults rather than failing addon load.
void LoadSettings(const std::string& path);
void SaveSettings();

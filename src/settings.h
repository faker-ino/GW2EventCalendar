#pragma once

#include <string>

// Persisted addon configuration. feed_url_override is an escape hatch for
// the rare case the public ICS calendar feed (icsfeed.h) breaks - it needs
// no auth, so there's no credential equivalent to feed_url_override here.
struct Settings {
    bool        window_visible = true;
    std::string feed_url_override;
    int         months_back = 1;
    int         months_forward = 3;
    int         refresh_interval_minutes = 180;
    bool        week_starts_monday = true; // false = Sunday-first (US-style), true = Monday-first
    bool        show_quickaccess_icon = true;
    float       today_highlight_color[3] = {1.0f, 0.843f, 0.0f}; // gold; RGB 0-1, for ImGui::ColorEdit3
};

extern Settings g_settings;

// Missing/malformed file keeps defaults rather than failing addon load.
void LoadSettings(const std::string& path);
void SaveSettings();

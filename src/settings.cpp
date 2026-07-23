#include "settings.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

Settings g_settings;
static std::string g_settingsPath;

void LoadSettings(const std::string& path) {
    g_settingsPath = path;
    std::ifstream file(path);
    if (!file.is_open()) {
        return; // no settings saved yet - keep the defaults
    }
    try {
        nlohmann::json doc;
        file >> doc;
        if (doc.contains("window_visible")) {
            g_settings.window_visible = doc["window_visible"].get<bool>();
        }
        if (doc.contains("feed_member_id")) {
            g_settings.feed_member_id = doc["feed_member_id"].get<std::string>();
        }
        if (doc.contains("feed_key")) {
            g_settings.feed_key = doc["feed_key"].get<std::string>();
        }
        if (doc.contains("feed_url_override")) {
            g_settings.feed_url_override = doc["feed_url_override"].get<std::string>();
        }
        if (doc.contains("months_back")) {
            g_settings.months_back = doc["months_back"].get<int>();
        }
        if (doc.contains("months_forward")) {
            g_settings.months_forward = doc["months_forward"].get<int>();
        }
        if (doc.contains("refresh_interval_minutes")) {
            g_settings.refresh_interval_minutes = doc["refresh_interval_minutes"].get<int>();
        }
        if (doc.contains("week_starts_monday")) {
            g_settings.week_starts_monday = doc["week_starts_monday"].get<bool>();
        }
        if (doc.contains("show_quickaccess_icon")) {
            g_settings.show_quickaccess_icon = doc["show_quickaccess_icon"].get<bool>();
        }
    } catch (const nlohmann::json::exception&) {
        // malformed file - keep the defaults rather than failing addon load
    }
}

void SaveSettings() {
    if (g_settingsPath.empty()) {
        return;
    }
    nlohmann::json doc;
    doc["window_visible"] = g_settings.window_visible;
    doc["feed_member_id"] = g_settings.feed_member_id;
    doc["feed_key"] = g_settings.feed_key;
    doc["feed_url_override"] = g_settings.feed_url_override;
    doc["months_back"] = g_settings.months_back;
    doc["months_forward"] = g_settings.months_forward;
    doc["refresh_interval_minutes"] = g_settings.refresh_interval_minutes;
    doc["week_starts_monday"] = g_settings.week_starts_monday;
    doc["show_quickaccess_icon"] = g_settings.show_quickaccess_icon;

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(g_settingsPath).parent_path(), ec);

    std::ofstream file(g_settingsPath, std::ios::trunc);
    if (file.is_open()) {
        file << doc.dump(2);
    }
}

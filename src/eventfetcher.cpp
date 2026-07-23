#include "eventfetcher.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <thread>

#include <nlohmann/json.hpp>

#include "rssfeed.h"
#include "settings.h"
#include "wikifeed.h"

EventStore g_eventStore;

namespace {

constexpr time_t kSecondsPerDay = 24 * 60 * 60;

std::string FormatDateKey(time_t t) {
    struct tm utcTm;
    gmtime_s(&utcTm, &t);
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday);
    return std::string(buf);
}

// Flattens a by-date map back into one Event per uid, de-duplicating since a
// multi-day event is stored under every date it spans.
std::map<std::string, Event> FlattenByUid(const EventsByDate& byDate) {
    std::map<std::string, Event> result;
    for (const auto& [date, dayEvents] : byDate) {
        for (const auto& e : dayEvents) {
            result[e.uid] = e;
        }
    }
    return result;
}

time_t StartOfUtcDay(time_t t) {
    struct tm utcTm;
    gmtime_s(&utcTm, &t);
    utcTm.tm_hour = 0;
    utcTm.tm_min = 0;
    utcTm.tm_sec = 0;
    return _mkgmtime(&utcTm);
}

} // namespace

void EventStore::StartFetch(const std::string& cachePath) {
    bool expected = false;
    if (!started_.compare_exchange_strong(expected, true)) {
        return; // already started - don't fetch twice
    }
    cachePath_ = cachePath;
    if (LoadCache()) {
        version_.fetch_add(1);
        ready_ = true; // instant paint from cache, no network wait
    }
    std::thread([this]() { FetchThreadMain(); }).detach();
}

void EventStore::ForceRefresh() {
    bool expected = false;
    if (!refreshing_.compare_exchange_strong(expected, true)) {
        return; // a refresh is already in flight
    }
    std::thread([this]() {
        FetchThreadMain();
        refreshing_ = false;
    }).detach();
}

bool EventStore::IsReady() const { return ready_; }
bool EventStore::HasError() const { return error_; }
bool EventStore::IsRefreshing() const { return refreshing_; }
uint32_t EventStore::Version() const { return version_; }

EventsByDate EventStore::Snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return data_;
}

EventsByDate EventStore::BuildEventsByDate(const std::vector<Event>& events) const {
    time_t now = time(nullptr);
    // 31 days/month is a deliberate approximation - the months-back/forward
    // sliders only need to bound "roughly how far out to show," not land on
    // exact calendar month boundaries.
    time_t rangeStart = StartOfUtcDay(now) - (time_t)g_settings.months_back * 31 * kSecondsPerDay;
    time_t rangeEnd = StartOfUtcDay(now) + (time_t)g_settings.months_forward * 31 * kSecondsPerDay;

    EventsByDate result;
    for (const auto& e : events) {
        if (e.end_utc < rangeStart || e.start_utc > rangeEnd) {
            continue; // entirely outside the configured window
        }
        for (time_t day = StartOfUtcDay(e.start_utc); day <= e.end_utc; day += kSecondsPerDay) {
            result[FormatDateKey(day)].push_back(e);
        }
    }
    return result;
}

bool EventStore::LoadCache() {
    std::ifstream file(cachePath_);
    if (!file.is_open()) {
        return false;
    }

    nlohmann::json doc;
    try {
        file >> doc;
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }
    if (!doc.is_array()) {
        return false;
    }

    std::vector<Event> events;
    for (const auto& j : doc) {
        if (!j.contains("uid") || !j.contains("title") || !j.contains("start_utc") || !j.contains("end_utc")) {
            continue; // skip malformed entries instead of failing the whole load
        }
        Event e;
        e.uid = j["uid"].get<std::string>();
        e.title = j["title"].get<std::string>();
        e.start_utc = (time_t)j["start_utc"].get<int64_t>();
        e.end_utc = (time_t)j["end_utc"].get<int64_t>();
        e.detail_url = j.contains("detail_url") ? j["detail_url"].get<std::string>() : std::string();
        e.description = j.contains("description") ? j["description"].get<std::string>() : std::string();
        e.features = j.contains("features") ? j["features"].get<std::string>() : std::string();
        e.bonus_effect_name = j.contains("bonus_effect_name") ? j["bonus_effect_name"].get<std::string>() : std::string();
        e.bonus_effect_description = j.contains("bonus_effect_description")
                                          ? j["bonus_effect_description"].get<std::string>()
                                          : std::string();
        events.push_back(std::move(e));
    }

    std::lock_guard<std::mutex> lock(mutex_);
    data_ = BuildEventsByDate(events);
    return true;
}

void EventStore::SaveCache() const {
    if (cachePath_.empty()) {
        return;
    }

    nlohmann::json arr = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::map<std::string, Event> byUid = FlattenByUid(data_);
        for (const auto& [uid, e] : byUid) {
            arr.push_back({
                {"uid", e.uid},
                {"title", e.title},
                {"start_utc", (int64_t)e.start_utc},
                {"end_utc", (int64_t)e.end_utc},
                {"detail_url", e.detail_url},
                {"description", e.description},
                {"features", e.features},
                {"bonus_effect_name", e.bonus_effect_name},
                {"bonus_effect_description", e.bonus_effect_description},
            });
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(cachePath_).parent_path(), ec);

    std::ofstream file(cachePath_, std::ios::trunc);
    if (file.is_open()) {
        file << arr.dump(2);
    }
}

void EventStore::FetchThreadMain() {
    std::vector<Event> events;
    bool ok = FetchAndParseRssFeed(g_settings.feed_url_override, g_settings.feed_member_id,
                                    g_settings.feed_key, events);
    if (!ok) {
        error_ = true;
        return; // keep whatever data_ already holds - don't blank the UI on a transient blip
    }

    // The RSS feed only ever carries current+near-future events, and even
    // for those carries no description/features/bonus-effect data at all.
    // The wiki's "Special event" page tracks the same rotation with all of
    // that extra detail plus a running Historical table (and sometimes an
    // Upcoming entry the feed hasn't published yet) going back years. RSS
    // stays authoritative on title/dates/forum-link for any event both
    // sources carry - matched by forum event id, which is both the RSS
    // <guid> and the numeric id embedded in the wiki's "official page" link
    // (see wikifeed.cpp's ExtractEventId) - but gets enriched with that
    // event's wiki fields when a match exists. Wiki events with no RSS
    // match at all (the historical/upcoming gaps) get added outright - EXCEPT
    // when we've already captured that same uid from RSS on some earlier
    // fetch (i.e. it just aged out of the feed's rolling window this time):
    // in that case keep the previously-cached, RSS-confirmed dates/link
    // instead of the wiki's, since the wiki's "Month D, YYYY" text is
    // date-only (no time-of-day) and has been observed a full day off from
    // the real UTC boundary the forum's own event page reports (e.g. wiki
    // editors seem to transcribe whichever local day the event felt like).
    // Only description/features/bonus-effect fields get refreshed from the
    // wiki in that case.
    std::map<std::string, Event> previousByUid;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        previousByUid = FlattenByUid(data_);
    }

    std::vector<Event> wikiEvents;
    if (FetchAndParseWikiEvents(wikiEvents)) {
        std::map<std::string, const Event*> wikiById;
        for (const auto& e : wikiEvents) {
            wikiById[e.uid] = &e;
        }

        for (auto& e : events) {
            auto it = wikiById.find(e.uid);
            if (it == wikiById.end()) {
                continue;
            }
            const Event& w = *it->second;
            e.description = w.description;
            e.features = w.features;
            e.bonus_effect_name = w.bonus_effect_name;
            e.bonus_effect_description = w.bonus_effect_description;
        }

        std::set<std::string> rssIds;
        for (const auto& e : events) {
            rssIds.insert(e.uid);
        }
        for (auto& e : wikiEvents) {
            if (rssIds.count(e.uid)) {
                continue;
            }
            auto prev = previousByUid.find(e.uid);
            if (prev != previousByUid.end()) {
                Event merged = prev->second;
                merged.description = e.description;
                merged.features = e.features;
                merged.bonus_effect_name = e.bonus_effect_name;
                merged.bonus_effect_description = e.bonus_effect_description;
                events.push_back(std::move(merged));
            } else {
                events.push_back(std::move(e));
            }
        }
    }

    error_ = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        data_ = BuildEventsByDate(events);
    }
    SaveCache();
    version_.fetch_add(1);
    ready_ = true;
}

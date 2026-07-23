#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "eventmodel.h"

// Mirrors the sibling BigBomb addon's Gw2SkillDatabase (src/gw2api.h) -
// background-thread fetch, mutex-guarded snapshot, disk cache. Unlike that
// static skill data, event listings are time-sensitive: a cache hit still
// always triggers a background re-fetch afterward instead of skipping it.
class EventStore {
public:
    // Loads any on-disk cache immediately (instant paint - IsReady() is true
    // right away with no network wait if a cache exists), then always kicks
    // a background fetch anyway. Safe to call more than once - only the
    // first call does anything.
    void StartFetch(const std::string& cachePath);

    // Always starts a fresh background fetch. No-op if one is already in
    // flight. Used by the "Refresh" button and the periodic refresh timer.
    void ForceRefresh();

    bool IsReady() const;
    bool HasError() const;
    bool IsRefreshing() const;

    // Bumped on every successful (re)fetch. Callers that cache their own
    // copy of Snapshot() should re-snapshot whenever this changes.
    uint32_t Version() const;

    EventsByDate Snapshot() const;

private:
    void FetchThreadMain();
    bool LoadCache();
    void SaveCache() const;
    EventsByDate BuildEventsByDate(const std::vector<Event>& events) const;

    mutable std::mutex mutex_;
    EventsByDate      data_;
    std::string       cachePath_;
    std::atomic<bool> ready_{false};
    std::atomic<bool> error_{false};
    std::atomic<bool> started_{false};
    std::atomic<bool> refreshing_{false};
    std::atomic<uint32_t> version_{0};
};

extern EventStore g_eventStore;

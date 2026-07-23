#pragma once

#include <vector>

#include "eventmodel.h"

// Fetches the GW2 wiki's "Special event" page
// (wiki.guildwars2.com/wiki/Special_event), rendered to HTML server-side,
// and parses its Active/Upcoming/Historical events tables. That page tracks
// the same bonus-event rotations as the forum's RSS feed (rssfeed.h) but,
// unlike the feed, keeps a running "Historical events" table going back
// years, an "Upcoming events" table that can lead the feed, and - the whole
// reason this parses the rendered HTML instead of raw wikitext - a
// Description/Features/Bonus-effects breakdown per event that the feed has
// no equivalent of at all. See EventStore::FetchThreadMain in
// eventfetcher.cpp for how this gets merged with (and used to enrich) the
// RSS-sourced events.
//
// Returns false if the page couldn't be fetched at all. A fetch that
// succeeds but parses zero rows still returns true - callers should treat
// that the same as "no events available from this source right now", not a
// hard failure.
bool FetchAndParseWikiEvents(std::vector<Event>& out);

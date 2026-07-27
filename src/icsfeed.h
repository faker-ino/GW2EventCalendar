#pragma once

#include <string>
#include <vector>

#include "eventmodel.h"

// Fetches and parses the forum's "Game Updates" calendar as an iCalendar
// (.ics) export. This is the same calendar the addon used to read as a
// truncated "current+near-future only" RSS feed - the .ics export instead
// carries the calendar's full history (years back) plus everything
// upcoming, each with precise UTC DTSTART/DTEND, and is regenerated fresh on
// every request (the feed's own REFRESH-INTERVAL/X-PUBLISHED-TTL hint is for
// subscribing calendar clients' polling cadence, not a sign it's a static
// snapshot). See eventfetcher.cpp's FetchThreadMain for how this is merged
// with wikifeed.h's description/features/bonus-effect enrichment.
//
// feedUrlOverride, if non-empty, is fetched instead of the hardcoded public
// URL - an escape hatch for the rare case the public feed breaks. The feed
// needs no member/key auth (confirmed by direct fetch).
//
// Returns false if the feed couldn't be fetched at all. A fetch that
// succeeds but parses zero events still returns true.
bool FetchAndParseIcsFeed(const std::string& feedUrlOverride, std::vector<Event>& out);

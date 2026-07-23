#pragma once

#include <ctime>
#include <string>

// Parses "Tue, 14 Jul 2026 16:00:00 +0000" (RFC822/RFC1123, as emitted by
// the forum's RSS feed for pubDate/endDate) into a UTC epoch time. The feed
// always carries a "+0000" offset (confirmed by direct inspection), so this
// deliberately only handles that one fixed, server-generated format rather
// than the full space of RFC822 variants - returns 0 on any parse failure,
// which callers treat as "skip this event" rather than showing a garbage
// Jan-1-1970 date.
time_t ParseRfc822(const std::string& s);

// Parses the GW2 wiki's "{{Special event row}}" date fields into a UTC
// epoch time (midnight UTC on that calendar day). Unlike the RSS feed, wiki
// editors have used at least three different orderings over the years -
// "July 21, 2026", "2 June 2026", "February 24 2026" (no comma) all appear
// in the actual page history - so this scans tokens instead of matching one
// fixed layout: a purely-alphabetic token is tried as a month name, a
// 4-digit numeric token is the year, anything else numeric in 1-31 is the
// day. Returns 0 (caller skips the event) if month/day/year didn't all
// resolve.
time_t ParseWikiDate(const std::string& s);

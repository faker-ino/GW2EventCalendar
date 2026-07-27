#pragma once

#include <ctime>
#include <string>

// Parses the GW2 wiki's "{{Special event row}}" date fields into a UTC
// epoch time (midnight UTC on that calendar day). Wiki editors have used at
// least three different orderings over the years -
// "July 21, 2026", "2 June 2026", "February 24 2026" (no comma) all appear
// in the actual page history - so this scans tokens instead of matching one
// fixed layout: a purely-alphabetic token is tried as a month name, a
// 4-digit numeric token is the year, anything else numeric in 1-31 is the
// day. Returns 0 (caller skips the event) if month/day/year didn't all
// resolve.
time_t ParseWikiDate(const std::string& s);

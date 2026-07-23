#pragma once

#include <string>
#include <vector>

#include "eventmodel.h"

// Fetches and parses the forum's events RSS feed. Tries, in order:
// (1) feedUrlOverride if non-empty (full user-controlled escape hatch),
// (2) the bare public feed URL with no auth params - unconfirmed whether
//     this is genuinely public, so this is how that gets settled in
//     practice,
// (3) the bare URL with ?member=...&key=... appended, if both are set and
//     (2) failed.
// Returns false (out left untouched) if every attempt failed; a feed that
// fetches successfully but contains zero items still returns true.
bool FetchAndParseRssFeed(const std::string& feedUrlOverride,
                           const std::string& feedMemberId,
                           const std::string& feedKey,
                           std::vector<Event>& out);

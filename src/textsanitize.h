#pragma once

#include <string>

// Replaces "smart"/typographic Unicode punctuation (curly quotes, en/em
// dashes, bullets, ellipsis, (R)/(TM)) with plain-ASCII equivalents. Used on
// any text scraped from the wiki or the ICS feed before it's stored on an
// Event - this addon draws through Nexus's own shared ImGui font, which
// this addon has no way to add glyph ranges to, and that font's glyph
// coverage doesn't extend past basic Latin. Any codepoint outside the
// loaded font falls back to ImGui's default "?" glyph, which is exactly
// what silently turns a wiki bullet-separated bonus-effect blurb, or a
// title with a typographic apostrophe, into text full of stray "?"s.
std::string SanitizeForDisplay(const std::string& utf8);

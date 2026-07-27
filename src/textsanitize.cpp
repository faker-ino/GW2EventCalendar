#include "textsanitize.h"

namespace {

struct Replacement {
    const char* from;
    size_t      fromLen;
    const char* to;
};

// All the multi-byte UTF-8 sequences here are characters MediaWiki/rich
// editors commonly emit literally (not as HTML entities) in rendered text -
// curly quotes, en/em dashes, bullet separators, ellipsis, (R)/(TM), and the
// General Punctuation block's various fixed-width space characters (e.g.
// U+2002 EN SPACE, confirmed by direct inspection to be what the wiki's
// "Special event" page actually uses to pad between concatenated
// bonus-effect stat lines, in place of a real line break). Every one of
// these falls outside Nexus's shared ImGui font's glyph range (see
// textsanitize.h).
constexpr Replacement kReplacements[] = {
    { "\xE2\x80\x98", 3, "'" },    // U+2018 LEFT SINGLE QUOTATION MARK
    { "\xE2\x80\x99", 3, "'" },    // U+2019 RIGHT SINGLE QUOTATION MARK
    { "\xE2\x80\x9C", 3, "\"" },   // U+201C LEFT DOUBLE QUOTATION MARK
    { "\xE2\x80\x9D", 3, "\"" },   // U+201D RIGHT DOUBLE QUOTATION MARK
    { "\xE2\x80\x93", 3, "-" },    // U+2013 EN DASH
    { "\xE2\x80\x94", 3, " - " },  // U+2014 EM DASH
    { "\xE2\x80\xA2", 3, " - " },  // U+2022 BULLET
    { "\xE2\x80\xA6", 3, "..." },  // U+2026 HORIZONTAL ELLIPSIS
    { "\xC2\xAE", 2, "(R)" },      // U+00AE REGISTERED SIGN
    { "\xE2\x84\xA2", 3, "(TM)" }, // U+2122 TRADE MARK SIGN
    // U+2000-U+200A: EN QUAD through HAIR SPACE - every fixed-width space
    // variant in the General Punctuation block. All collapse to a plain
    // ASCII space.
    { "\xE2\x80\x80", 3, " " },    // U+2000 EN QUAD
    { "\xE2\x80\x81", 3, " " },    // U+2001 EM QUAD
    { "\xE2\x80\x82", 3, " " },    // U+2002 EN SPACE
    { "\xE2\x80\x83", 3, " " },    // U+2003 EM SPACE
    { "\xE2\x80\x84", 3, " " },    // U+2004 THREE-PER-EM SPACE
    { "\xE2\x80\x85", 3, " " },    // U+2005 FOUR-PER-EM SPACE
    { "\xE2\x80\x86", 3, " " },    // U+2006 SIX-PER-EM SPACE
    { "\xE2\x80\x87", 3, " " },    // U+2007 FIGURE SPACE
    { "\xE2\x80\x88", 3, " " },    // U+2008 PUNCTUATION SPACE
    { "\xE2\x80\x89", 3, " " },    // U+2009 THIN SPACE
    { "\xE2\x80\x8A", 3, " " },    // U+200A HAIR SPACE
    { "\xE2\x80\xAF", 3, " " },    // U+202F NARROW NO-BREAK SPACE
    { "\xE3\x80\x80", 3, " " },    // U+3000 IDEOGRAPHIC SPACE
};

} // namespace

std::string SanitizeForDisplay(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        bool replaced = false;
        for (const Replacement& r : kReplacements) {
            if (s.compare(i, r.fromLen, r.from) == 0) {
                out += r.to;
                i += r.fromLen;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            out += s[i++];
        }
    }
    return out;
}

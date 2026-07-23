#include "calendarui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <ctime>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <windows.h>
#include <shellapi.h>

#include "imgui.h"
#include "Nexus.h"

#include "eventfetcher.h"
#include "settings.h"
#include "fallback_icon_data.h"

extern AddonAPI_t* g_api;

namespace {

int  g_viewYear = 0;
int  g_viewMonth = 0; // 1-12
bool g_viewInitialized = false;
std::string g_selectedDate;

uint32_t     g_lastSeenVersion = 0;
EventsByDate g_cachedSnapshot;
time_t       g_lastRefreshCheck = 0;

const char* kMonthNames[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December",
};
const char* kDayNames[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" }; // index = tm_wday (0=Sun)

// Column 0's weekday - 0=Sunday, 1=Monday - per Settings::week_starts_monday.
int WeekStartIndex() { return g_settings.week_starts_monday ? 1 : 0; }

// Maps a raw tm_wday (0=Sun..6=Sat) to which grid column it falls in given
// the configured week start.
int WeekdayToColumn(int wday) { return (wday - WeekStartIndex() + 7) % 7; }

void InitViewIfNeeded() {
    if (g_viewInitialized) {
        return;
    }
    time_t now = time(nullptr);
    struct tm utcTm;
    gmtime_s(&utcTm, &now);
    g_viewYear = utcTm.tm_year + 1900;
    g_viewMonth = utcTm.tm_mon + 1;
    g_viewInitialized = true;
}

std::string FormatDateKey(int year, int month, int day) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    return std::string(buf);
}

std::string TodayDateKey() {
    time_t now = time(nullptr);
    struct tm utcTm;
    gmtime_s(&utcTm, &now);
    return FormatDateKey(utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday);
}

int DaysInMonth(int year, int month) {
    static const int kDays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2) {
        bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return kDays[month - 1];
}

// 0=Sunday .. 6=Saturday.
int WeekdayOf(int year, int month, int day) {
    struct tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = 12; // noon - sidesteps any DST-adjacent edge cases, not that _mkgmtime applies DST anyway
    time_t tt = _mkgmtime(&t);
    struct tm result;
    gmtime_s(&result, &tt);
    return result.tm_wday;
}

std::string FormatEventRange(const Event& e) {
    struct tm startTm, endTm;
    time_t s = e.start_utc, en = e.end_utc;
    gmtime_s(&startTm, &s);
    gmtime_s(&endTm, &en);
    char buf[64];
    if (startTm.tm_year == endTm.tm_year && startTm.tm_mon == endTm.tm_mon && startTm.tm_mday == endTm.tm_mday) {
        snprintf(buf, sizeof(buf), "%s %d, %d", kMonthNames[startTm.tm_mon], startTm.tm_mday, startTm.tm_year + 1900);
    } else {
        snprintf(buf, sizeof(buf), "%s %d - %s %d, %d",
                 kMonthNames[startTm.tm_mon], startTm.tm_mday,
                 kMonthNames[endTm.tm_mon], endTm.tm_mday, endTm.tm_year + 1900);
    }
    return std::string(buf);
}

// No event ever has a fetched image - see eventmodel.h. Every icon-shaped
// slot in this UI (day-cell thumbnails, the day-detail popup's image, the
// bonus-effect icon) always shows this same static asset.
Texture_t* GetFallbackTexture() {
    static Texture_t* tex = nullptr;
    if (!tex) {
        tex = g_api->Textures_GetOrCreateFromMemory("GW2EVENTCAL_FALLBACK_ICON",
                                                      (void*)kFallbackIconPng, kFallbackIconPngSize);
    }
    return tex;
}

void OpenForumLink(const std::string& url) {
    if (url.empty()) {
        return;
    }
    std::wstring wideUrl(url.begin(), url.end()); // url is pure ASCII - safe narrow->wide widen
    ShellExecuteW(nullptr, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// Trims text to fit maxWidth (current font/scale), appending "..." - used
// instead of a fixed character cutoff so titles use whatever room the
// window's current size actually gives the column.
std::string TruncateToWidth(const std::string& text, float maxWidth) {
    if (maxWidth <= 0.0f) {
        return std::string();
    }
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }
    const char* kEllipsis = "...";
    float ellipsisWidth = ImGui::CalcTextSize(kEllipsis).x;
    if (ellipsisWidth > maxWidth) {
        return std::string();
    }
    std::string result = text;
    while (!result.empty() && ImGui::CalcTextSize(result.c_str()).x + ellipsisWidth > maxWidth) {
        result.pop_back();
    }
    return result.empty() ? std::string() : result + kEllipsis;
}

// One event's presence within a single week-row, clipped to the columns
// (0=Sun..6=Sat) it actually touches in that row - a multi-week event gets
// a separate segment per row it spans. `lane` is its vertical stacking slot
// once AssignLanes below has run.
struct RowSegment {
    const Event* event = nullptr;
    int firstCol = 0;
    int lastCol = 0;
    int lane = 0;
};

// Builds one segment per event touching this row, then greedily assigns
// each a lane (like interval-graph coloring): earliest/longest events get
// first pick, so multi-day bars tend to settle near the top instead of
// reshuffling from week to week.
std::vector<RowSegment> BuildRowSegments(const std::string dateKeys[7], const EventsByDate& snapshot) {
    std::unordered_map<std::string, size_t> indexByUid;
    std::vector<RowSegment> segments;
    for (int col = 0; col < 7; ++col) {
        auto it = snapshot.find(dateKeys[col]);
        if (it == snapshot.end()) {
            continue;
        }
        for (const auto& e : it->second) {
            auto found = indexByUid.find(e.uid);
            if (found == indexByUid.end()) {
                indexByUid.emplace(e.uid, segments.size());
                RowSegment seg;
                seg.event = &e;
                seg.firstCol = col;
                seg.lastCol = col;
                segments.push_back(seg);
            } else {
                segments[found->second].lastCol = col;
            }
        }
    }

    std::sort(segments.begin(), segments.end(), [](const RowSegment& a, const RowSegment& b) {
        if (a.event->start_utc != b.event->start_utc) {
            return a.event->start_utc < b.event->start_utc;
        }
        time_t da = a.event->end_utc - a.event->start_utc;
        time_t db = b.event->end_utc - b.event->start_utc;
        if (da != db) {
            return da > db; // longer events first
        }
        return a.event->uid < b.event->uid;
    });

    std::vector<int> laneLastCol;
    for (auto& seg : segments) {
        int lane = -1;
        for (int i = 0; i < (int)laneLastCol.size(); ++i) {
            if (laneLastCol[i] < seg.firstCol) {
                lane = i;
                break;
            }
        }
        if (lane < 0) {
            lane = (int)laneLastCol.size();
            laneLastCol.push_back(-1);
        }
        laneLastCol[lane] = seg.lastCol;
        seg.lane = lane;
    }
    return segments;
}

// Stable per-event color so the same event keeps the same bar color across
// every day/week it appears in, without any category data from the feed to
// key off of.
ImU32 ColorForEvent(const std::string& uid) {
    static const ImU32 kPalette[] = {
        IM_COL32(196, 143, 60, 235),  // amber
        IM_COL32(84, 140, 186, 235),  // steel blue
        IM_COL32(150, 104, 178, 235), // violet
        IM_COL32(92, 163, 118, 235),  // sage green
        IM_COL32(196, 92, 100, 235),  // brick red
        IM_COL32(74, 163, 163, 235),  // teal
        IM_COL32(181, 140, 90, 235),  // ochre
        IM_COL32(130, 130, 190, 235), // periwinkle
    };
    size_t h = std::hash<std::string>{}(uid);
    return kPalette[h % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

// Day number + "+N" overflow hint only - the event bars themselves are
// drawn separately, across the whole row, by RenderWeekRow below.
//
// weekdayLabel is non-null only for the very first row: instead of a
// separate header row/band above the grid (which only ever showed weekday
// names and was otherwise dead space), the top row doubles up as the
// header by printing the weekday name above its day number. Every other
// row still reserves that same line's worth of height (see dayNumHeight in
// RenderCalendarWindow) so all rows stay exactly the same size - it's just
// left blank there.
// Returns true if this cell was clicked this frame. Deliberately doesn't call
// ImGui::OpenPopup itself - it's wrapped in PushID(dateKey) below, and
// OpenPopup/BeginPopup hash their string id against whatever's currently on
// the id stack, same as any other widget id. Calling OpenPopup in here would
// scope "DayDetailPopup" to this cell's per-date id (and, one level further
// out, to the enclosing table's own pushed id - see BeginTable), which would
// never match the plain, unscoped "DayDetailPopup" that RenderDayDetailPopup
// calls BeginPopup with once the table's closed - so it could never open, no
// matter where or how precisely a cell was clicked. The caller opens the
// popup itself, outside both scopes.
bool RenderDayCell(const std::string& dateKey, int day, bool inCurrentMonth, bool isToday,
                    float cellHeight, int hiddenCount, const char* weekdayLabel) {
    ImGui::PushID(dateKey.c_str());

    ImVec2 cellMin = ImGui::GetCursorScreenPos();
    ImVec2 cellSize(ImGui::GetContentRegionAvail().x, cellHeight);

    if (isToday) {
        ImGui::GetWindowDrawList()->AddRectFilled(
            cellMin, ImVec2(cellMin.x + cellSize.x, cellMin.y + cellSize.y),
            IM_COL32(255, 215, 0, 40));
    }

    ImGui::TextDisabled("%s", weekdayLabel ? weekdayLabel : "");

    if (!inCurrentMonth) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    }
    ImGui::Text("%d", day);
    if (!inCurrentMonth) {
        ImGui::PopStyleColor();
    }

    if (hiddenCount > 0) {
        std::string more = "+" + std::to_string(hiddenCount);
        float w = ImGui::CalcTextSize(more.c_str()).x;
        ImGui::SetCursorScreenPos(ImVec2(cellMin.x + cellSize.x - w - 3.0f,
                                          cellMin.y + cellSize.y - ImGui::GetTextLineHeight() - 2.0f));
        ImGui::TextDisabled("%s", more.c_str());
    }

    // Whole-cell click target, positioned over the cell content so clicking
    // anywhere in the cell (not just the day number) opens the day popup.
    ImGui::SetCursorScreenPos(cellMin);
    bool clicked = ImGui::InvisibleButton("cellclick", cellSize);

    ImGui::PopID();
    return clicked;
}

void RenderDayDetailPopup(const EventsByDate& snapshot) {
    if (!ImGui::BeginPopup("DayDetailPopup")) {
        return;
    }
    ImGui::TextUnformatted(g_selectedDate.c_str());
    ImGui::Separator();

    auto it = snapshot.find(g_selectedDate);
    if (it == snapshot.end() || it->second.empty()) {
        ImGui::TextDisabled("No events.");
    } else {
        for (const auto& e : it->second) {
            ImGui::PushID(e.uid.c_str());
            ImGui::BeginGroup();
            ImGui::TextUnformatted(e.title.c_str());
            ImGui::TextDisabled("%s", FormatEventRange(e).c_str());
            if (!e.detail_url.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));
                ImGui::TextUnformatted("View on forum");
                ImGui::PopStyleColor();
                if (ImGui::IsItemClicked()) {
                    OpenForumLink(e.detail_url);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                }
            }
            ImGui::EndGroup();

            // Description/features/bonus-effect data only exists for events
            // the GW2 wiki's "Special event" page tracks (see wikifeed.cpp)
            // - most events have at least a description, but bonus effects
            // in particular are often genuinely absent, not a fetch failure.
            constexpr float kWrapWidth = 380.0f;
            if (!e.description.empty()) {
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kWrapWidth);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                ImGui::TextWrapped("%s", e.description.c_str());
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
            }
            if (!e.features.empty()) {
                ImGui::Spacing();
                ImGui::TextUnformatted("Features");
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kWrapWidth);
                ImGui::TextWrapped("%s", e.features.c_str());
                ImGui::PopTextWrapPos();
            }
            if (!e.bonus_effect_name.empty()) {
                ImGui::Spacing();
                ImGui::TextUnformatted("Bonus effects");
                ImGui::TextUnformatted(e.bonus_effect_name.c_str());
                if (!e.bonus_effect_description.empty()) {
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + kWrapWidth);
                    ImGui::TextWrapped("%s", e.bonus_effect_description.c_str());
                    ImGui::PopTextWrapPos();
                }
            }
            ImGui::Separator();
            ImGui::PopID();
        }
    }

    ImGui::EndPopup();
}

} // namespace

void RenderCalendarWindow() {
    InitViewIfNeeded();

    // Periodic refresh, driven off the render loop rather than a dedicated
    // timer thread - checked once per frame, which is cheap enough.
    time_t now = time(nullptr);
    if (g_lastRefreshCheck == 0) {
        g_lastRefreshCheck = now;
    } else if (now - g_lastRefreshCheck >= (time_t)g_settings.refresh_interval_minutes * 60) {
        g_lastRefreshCheck = now;
        g_eventStore.ForceRefresh();
    }

    if (g_eventStore.Version() != g_lastSeenVersion) {
        g_lastSeenVersion = g_eventStore.Version();
        g_cachedSnapshot = g_eventStore.Snapshot();
    }

    // Grid cells are a fixed size (see rowHeight below) rather than
    // stretched/shrunk to fit - so instead of a manually resizable window
    // that could mismatch that fixed content and need a scrollbar, the
    // window auto-sizes to exactly whatever the current month's row count
    // needs. A 5-row month is a shorter window than a 6-row one; there's
    // never leftover/missing space to scroll.
    ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 0.0f), ImVec2(FLT_MAX, FLT_MAX));

    bool windowOpen = true;
    if (ImGui::Begin("GW2 Event Calendar", &windowOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::ArrowButton("##prevmonth", ImGuiDir_Left)) {
            if (--g_viewMonth < 1) { g_viewMonth = 12; --g_viewYear; }
        }
        ImGui::SameLine();
        ImGui::Text("%s %d", kMonthNames[g_viewMonth - 1], g_viewYear);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##nextmonth", ImGuiDir_Right)) {
            if (++g_viewMonth > 12) { g_viewMonth = 1; ++g_viewYear; }
        }
        ImGui::SameLine();
        if (g_eventStore.IsRefreshing()) {
            ImGui::TextDisabled("Refreshing...");
        } else if (ImGui::Button("Refresh")) {
            g_eventStore.ForceRefresh();
        }
        ImGui::SameLine();
        if (!g_eventStore.IsReady()) {
            ImGui::TextDisabled("Loading...");
        } else if (g_eventStore.HasError()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error fetching events");
        }

        ImGui::Separator();

        std::string today = TodayDateKey();

        int daysInThisMonth = DaysInMonth(g_viewYear, g_viewMonth);
        int firstWeekday = WeekdayToColumn(WeekdayOf(g_viewYear, g_viewMonth, 1));

        int prevMonth = g_viewMonth == 1 ? 12 : g_viewMonth - 1;
        int prevYear = g_viewMonth == 1 ? g_viewYear - 1 : g_viewYear;
        int daysInPrevMonth = DaysInMonth(prevYear, prevMonth);

        int totalCells = firstWeekday + daysInThisMonth;
        int totalRows = (totalCells + 6) / 7;

        // Fixed per-column width rather than stretch-to-fit: with the window
        // now auto-sizing to its content (see ImGuiWindowFlags_AlwaysAutoResize
        // above), a stretch policy would have nothing stable to stretch
        // into. This also means every day cell is always the same size,
        // month to month and launch to launch.
        constexpr float kColumnWidth = 100.0f;
        constexpr int   kVisibleLanes = 3; // fixed event-bar lanes shown per day; rest fold into "+N"

        // Fixed row height at the window's one, never-scaled font size: a
        // weekday-label line, a day-number line, then a constant number of
        // event-bar lanes. Always the same regardless of window size or
        // which month is showing. There's no separate header row/band above
        // the grid - it was otherwise-dead space, since it only ever showed
        // weekday names - the weekday-label line is reserved on every row
        // but only actually printed on the first one (see RenderDayCell),
        // so the whole grid is one uniform size with no extra band on top.
        float dayNumHeight = ImGui::GetTextLineHeightWithSpacing() * 2.0f;
        float barHeight = ImGui::GetTextLineHeight() + 4.0f;
        float laneGap = 2.0f;
        float laneStride = barHeight + laneGap;
        float rowHeight = dayNumHeight + kVisibleLanes * laneStride + 4.0f;
        int maxLanesVisible = kVisibleLanes;

        bool dayCellClicked = false;
        std::string clickedDate;

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(3.0f, 2.0f));
        if (ImGui::BeginTable("MonthGrid", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
            for (int col = 0; col < 7; ++col) {
                ImGui::TableSetupColumn(kDayNames[(WeekStartIndex() + col) % 7], ImGuiTableColumnFlags_WidthFixed,
                                         kColumnWidth);
            }

            int dayCounter = 1 - firstWeekday; // may start <= 0 -> spills into the previous month
            for (int row = 0; row < totalRows; ++row) {
                ImGui::TableNextRow(ImGuiTableRowFlags_None, rowHeight);

                // Pure date math for all 7 columns up front - BuildRowSegments
                // needs the whole week's dates before any cell is drawn.
                std::string dateKeys[7];
                int dayNums[7];
                bool inMonthFlags[7];
                for (int col = 0; col < 7; ++col) {
                    int day = dayCounter;
                    int year = g_viewYear, month = g_viewMonth;
                    bool inCurrentMonth = true;
                    if (day < 1) {
                        month = prevMonth;
                        year = prevYear;
                        day = daysInPrevMonth + day;
                        inCurrentMonth = false;
                    } else if (day > daysInThisMonth) {
                        month = g_viewMonth == 12 ? 1 : g_viewMonth + 1;
                        year = g_viewMonth == 12 ? g_viewYear + 1 : g_viewYear;
                        day = day - daysInThisMonth;
                        inCurrentMonth = false;
                    }
                    dateKeys[col] = FormatDateKey(year, month, day);
                    dayNums[col] = day;
                    inMonthFlags[col] = inCurrentMonth;
                    ++dayCounter;
                }

                std::vector<RowSegment> segments = BuildRowSegments(dateKeys, g_cachedSnapshot);
                int hiddenPerCol[7] = {};
                for (const auto& seg : segments) {
                    if (seg.lane >= maxLanesVisible) {
                        for (int c = seg.firstCol; c <= seg.lastCol; ++c) {
                            ++hiddenPerCol[c];
                        }
                    }
                }

                float colX[7];
                float colWidth = 0.0f;
                float rowTopY = 0.0f;
                for (int col = 0; col < 7; ++col) {
                    ImGui::TableNextColumn();
                    if (col == 0) {
                        rowTopY = ImGui::GetCursorScreenPos().y;
                        colWidth = ImGui::GetContentRegionAvail().x;
                    }
                    colX[col] = ImGui::GetCursorScreenPos().x;
                    const char* weekdayLabel = row == 0 ? kDayNames[(WeekStartIndex() + col) % 7] : nullptr;
                    if (RenderDayCell(dateKeys[col], dayNums[col], inMonthFlags[col], dateKeys[col] == today,
                                       rowHeight, hiddenPerCol[col], weekdayLabel)) {
                        dayCellClicked = true;
                        clickedDate = dateKeys[col];
                    }
                }

                // Bars are drawn last, positioned by absolute screen coords
                // spanning multiple columns - overridden to the full row's
                // clip rect so they aren't clipped to whichever single
                // column's cell rect the table last set. Must be
                // ImGui::PushClipRect (window-level), not
                // ImDrawList::PushClipRect - the draw-list one only affects
                // rendering, not hit-testing, so IsMouseHoveringRect below
                // was still using the table's last per-column clip rect and
                // only registered hovers inside that one column.
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 rowClipMin(colX[0], rowTopY);
                ImVec2 rowClipMax(colX[6] + colWidth, rowTopY + rowHeight);
                ImGui::PushClipRect(rowClipMin, rowClipMax, false);
                for (const auto& seg : segments) {
                    if (seg.lane >= maxLanesVisible) {
                        continue;
                    }
                    float barLeft = colX[seg.firstCol] + 1.0f;
                    float barRight = colX[seg.lastCol] + colWidth - 1.0f;
                    float barTop = rowTopY + dayNumHeight + seg.lane * laneStride;
                    ImVec2 barMin(barLeft, barTop);
                    ImVec2 barMax(barRight, barTop + barHeight);

                    dl->AddRectFilled(barMin, barMax, ColorForEvent(seg.event->uid), 3.0f);

                    // Single-day events only get one column's worth of bar
                    // width, which the icon alone can nearly fill - drop it
                    // there so the title gets a real chance to show instead
                    // of being squeezed out to nothing.
                    float barWidth = barMax.x - barMin.x;
                    Texture_t* tex = GetFallbackTexture();
                    float iconSize = barHeight - 4.0f;
                    bool showIcon = tex && barWidth > iconSize + 24.0f;

                    float textX = barLeft + 4.0f;
                    if (showIcon) {
                        ImVec2 iconMin(barLeft + 2.0f, barTop + 2.0f);
                        ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
                        dl->AddImage(tex->Resource, iconMin, iconMax);
                        textX = iconMax.x + 3.0f;
                    }

                    std::string title = TruncateToWidth(seg.event->title, barMax.x - textX - 3.0f);
                    if (!title.empty()) {
                        ImVec2 textPos(textX, barTop + (barHeight - ImGui::GetTextLineHeight()) * 0.5f);
                        dl->AddText(textPos, IM_COL32(250, 250, 250, 255), title.c_str());
                    }

                    // Every bar gets a tooltip on hover, not just truncated
                    // ones - full title plus the date range, same info as
                    // the day-detail popup.
                    if (ImGui::IsMouseHoveringRect(barMin, barMax)) {
                        ImGui::SetTooltip("%s\n%s", seg.event->title.c_str(), FormatEventRange(*seg.event).c_str());
                    }
                }
                ImGui::PopClipRect();
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();

        // Opened here, outside the table's id scope (BeginTable pushes its
        // own id, popped by the EndTable() above) and outside any per-cell
        // PushID - matching the plain, unscoped id RenderDayDetailPopup
        // opens below. See the comment on RenderDayCell for why this can't
        // just be called from inside the cell click-handling itself.
        if (dayCellClicked) {
            g_selectedDate = clickedDate;
            ImGui::OpenPopup("DayDetailPopup");
        }

        RenderDayDetailPopup(g_cachedSnapshot);
    }
    ImGui::End();

    if (!windowOpen) {
        g_settings.window_visible = false;
        SaveSettings();
    }
}

// main.cpp - Nexus addon entry point.
//
// This is a Nexus addon (a Windows x64 DLL, not a standalone executable):
// Nexus discovers it via the exported GetAddonDef(), hands it a live
// ImGuiContext + AddonAPI_t on Load, and calls Unload before unloading the
// DLL. See vendor/nexus/Nexus.h for the full API surface.
#include <windows.h>

#include <string>

#include "imgui.h"
#include "Nexus.h"
#include "Mumble.h"

#include "calendarui.h"
#include "eventfetcher.h"
#include "settings.h"
#include "fallback_icon_data.h"

// Not static: calendarui.cpp needs g_api for texture creation.
//
// The fallback icon ships baked into the DLL via Textures_GetOrCreateFromMemory
// (see kFallbackIconPng/kFallbackIconPngSize, generated at build time by
// cmake/EmbedIcon.cmake from assets/fallback_calendar_icon.png) - no loose-file
// deploy step needed. This DLL previously tried the *other* embedding route,
// Textures_GetOrCreateFromResource (Win32 RCDATA resource) - that one was
// reverted: Nexus's own log reported "Resource not found ResID: 101" even
// after confirming via a raw Win32 FindResource call that the resource
// genuinely existed in the built DLL, and even after forcing LANG_NEUTRAL in
// case of a language-ID mismatch (this dev machine defaults to German). Root
// cause undetermined - Nexus's loader implementation isn't available to
// inspect. GetOrCreateFromMemory sidesteps that failure entirely since it
// takes a raw pointer + size, no OS resource lookup involved.
AddonAPI_t* g_api = nullptr;

static HMODULE g_hSelf = nullptr;

static void AddonRender();
static void AddonOptions();
static void OnMumbleIdentityUpdated(void* aEventArgs);
static void OnToggleWindowKeybind(const char* aIdentifier, bool aIsRelease);

static AddonDefinition_t g_addonDef = {};

static constexpr const char* kToggleKeybindId = "GW2EVENTCAL_TOGGLE_WINDOW";
static constexpr const char* kQuickAccessId = "GW2EVENTCAL_QUICKACCESS";
static constexpr const char* kFallbackIconTextureId = "GW2EVENTCAL_FALLBACK_ICON";

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ulReasonForCall, LPVOID /*lpReserved*/)
{
    if (ulReasonForCall == DLL_PROCESS_ATTACH) {
        g_hSelf = hModule;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    // Arbitrary, distinct from the skeleton's placeholder (0x4D455441) and
    // from the sibling BigBomb addon's own signature - no collision-check
    // mechanism exists across addons, this just needs to look unique.
    g_addonDef.Signature = 0x47324543; // "G2EC"-ish
    g_addonDef.APIVersion = NEXUS_API_VERSION;
    g_addonDef.Name = "GW2EventCalendar";
    g_addonDef.Version = { 1, 0, 1, 0 };
    g_addonDef.Author = "faker-ino";
    g_addonDef.Description = "Shows upcoming Guild Wars 2 forum events in a WoW-Calendar-style month grid.";
    g_addonDef.Load = [](AddonAPI_t* aApi) {
        g_api = aApi;

        ImGui::SetCurrentContext((ImGuiContext*)g_api->ImguiContext);
        ImGui::SetAllocatorFunctions((void* (*)(size_t, void*))g_api->ImguiMalloc, (void(*)(void*, void*))g_api->ImguiFree);

        LoadSettings(g_api->Paths_GetAddonDirectory("GW2EventCalendar/settings.json"));

        g_eventStore.StartFetch(g_api->Paths_GetAddonDirectory("GW2EventCalendar/events_cache.json"));

        g_api->GUI_Register(RT_Render, AddonRender);
        g_api->GUI_Register(RT_OptionsRender, AddonOptions);
        g_api->Events_Subscribe(EV_MUMBLE_IDENTITY_UPDATED, OnMumbleIdentityUpdated);

        g_api->InputBinds_RegisterWithString(kToggleKeybindId, OnToggleWindowKeybind, "ALT+SHIFT+C");
        if (g_settings.show_quickaccess_icon) {
            g_api->QuickAccess_Add(kQuickAccessId, kFallbackIconTextureId, kFallbackIconTextureId,
                                    kToggleKeybindId, "GW2 Event Calendar");
        }

        g_api->Log(LOGL_INFO, "GW2EventCalendar", "loaded.");
    };
    g_addonDef.Unload = []() {
        g_api->QuickAccess_Remove(kQuickAccessId);
        g_api->InputBinds_Deregister(kToggleKeybindId);
        g_api->GUI_Deregister(AddonRender);
        g_api->GUI_Deregister(AddonOptions);
        g_api->Events_Unsubscribe(EV_MUMBLE_IDENTITY_UPDATED, OnMumbleIdentityUpdated);
        g_api->Log(LOGL_INFO, "GW2EventCalendar", "unloaded.");
    };
    g_addonDef.Flags = AF_None;
    g_addonDef.Provider = UP_None;
    g_addonDef.UpdateLink = nullptr;

    return &g_addonDef;
}

// Fired whenever Mumble Link's identity block changes (character swap, map
// change, loading screen). Not currently used for anything - the calendar
// isn't per-character/per-map - kept subscribed per the skeleton's pattern
// in case map-aware behavior is added later.
static void OnMumbleIdentityUpdated(void* aEventArgs)
{
    (void)aEventArgs;
}

static void OnToggleWindowKeybind(const char* aIdentifier, bool aIsRelease)
{
    (void)aIdentifier;
    if (aIsRelease) {
        return; // toggle on press, not release
    }
    g_settings.window_visible = !g_settings.window_visible;
    SaveSettings();
}

static void AddonRender()
{
    // Make sure the fallback icon texture QuickAccess points at exists as
    // soon as possible, even while the main window is hidden - retried
    // every frame (GetOrCreate is the documented "get or create" pattern,
    // safe to call repeatedly until it stops returning null).
    g_api->Textures_GetOrCreateFromMemory(kFallbackIconTextureId,
                                           (void*)kFallbackIconPng, kFallbackIconPngSize);

    if (!g_settings.window_visible) {
        return;
    }
    RenderCalendarWindow();
}

static void AddonOptions()
{
    if (ImGui::Checkbox("Show Window", &g_settings.window_visible)) {
        SaveSettings();
    }
    if (ImGui::Checkbox("Show QuickAccess Icon", &g_settings.show_quickaccess_icon)) {
        if (g_settings.show_quickaccess_icon) {
            g_api->QuickAccess_Add(kQuickAccessId, kFallbackIconTextureId, kFallbackIconTextureId,
                                    kToggleKeybindId, "GW2 Event Calendar");
        } else {
            g_api->QuickAccess_Remove(kQuickAccessId);
        }
        SaveSettings();
    }

    ImGui::Separator();

    {
        static const char* kWeekStartOptions[] = { "Sunday", "Monday" };
        int weekStart = g_settings.week_starts_monday ? 1 : 0;
        if (ImGui::Combo("Week starts on", &weekStart, kWeekStartOptions, IM_ARRAYSIZE(kWeekStartOptions))) {
            g_settings.week_starts_monday = (weekStart == 1);
            SaveSettings();
        }
    }

    if (ImGui::SliderInt("Months back", &g_settings.months_back, 0, 12)) {
        SaveSettings();
    }
    if (ImGui::SliderInt("Months forward", &g_settings.months_forward, 0, 12)) {
        SaveSettings();
    }
    if (ImGui::SliderInt("Refresh interval (minutes)", &g_settings.refresh_interval_minutes, 60, 180)) {
        SaveSettings();
    }

    if (g_eventStore.IsRefreshing()) {
        ImGui::TextDisabled("Refreshing...");
    } else if (ImGui::Button("Refresh Now")) {
        g_eventStore.ForceRefresh();
    }
}

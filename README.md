# GW2 Event Calendar

A [Nexus](https://github.com/RaidcoreGG/Nexus) addon for Guild Wars 2 that shows a calendar-style month
grid of the game's recurring bonus events and festivals, right inside the game.

## Where the data comes from

- **Primary source**: the official GW2 forum's [Events calendar](https://en-forum.guildwars2.com/events/),
  via its public RSS feed. This gives each event's title, forum link, and exact start/end time, and needs
  no login or setup - it just works out of the box.
- **Enrichment**: the [GW2 Wiki's Special Event page](https://wiki.guildwars2.com/wiki/Special_event) adds
  a short description, a features list, and bonus-effect details (e.g. "+100% WXP") for events the wiki
  tracks. The forum feed only ever carries current and near-future events, so the wiki is also used to
  fill in events just before they're announced on the forum, and recently-ended ones.
- When both sources have the same event, the forum's dates always win - the wiki is only used for the
  extra descriptive text.
- No event ever shows a fetched image/icon - every icon-shaped spot in the UI uses the same built-in
  calendar icon.

Events refresh automatically in the background (every 60-180 minutes, configurable), or on demand with
the "Refresh" button.

## Using it

- Open the calendar from the QuickAccess toolbar icon in-game, or its keybind (set one in Nexus's
  keybind settings, under `GW2EVENTCAL_TOGGLE_WINDOW`).
- Use the arrows next to the month name to navigate months.
- Click any day to see full details for everything happening that day - description, features, and
  bonus effects, plus a link back to the forum post.
- Days with more events than fit are marked "+N" - click the day to see the rest.

### Options

If the anonymous forum feed ever stops working, `settings.json` (next to the addon's own data folder)
also accepts `feed_member_id`/`feed_key` (a personal forum auth token) or a full `feed_url_override` -
these have no in-game UI and are meant as a manual fallback, not routine setup.

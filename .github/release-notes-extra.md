### First release on the new versioning scheme 🎉

This is portable release **v1**. Releases now carry their own simple counter tag (`v1`, `v2`, `v3`, …) instead of the old composite `<library>-<patch>` tags (like `v36.5.0-0-1`). The four numbers that matter — portable version, library version, and both layout versions — are shown separately in the tray tooltip and in the table above.

**New in this release**

- **Dark layout** added. Right-click the tray icon and pick **Classic** or **Dark** — the radio dot marks the active one, and your choice is remembered in `khipro.cfg` next to the exe (created automatically on first switch).
- **New tooltip** showing the portable version, library version, Classic and Dark layout versions, plus the current layout and status on hover.
- Engine updated to [khipro library 2.0.0](https://github.com/KhiproTeam/khipro-library/releases/tag/lib-v2.0.0): Classic layout `37.0.1`, Dark `1.1.0`.
- Typing behavior change from the new layout spec: doubled escapes now emit the literal character (`\\` → `\`, `??` → `?`) instead of collapsing the pair.

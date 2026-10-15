// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 qomarhsn

#pragma once

// Portable version. Shown in the tray tooltip; a release tag is "v" + this.
// Bump it (or let the sync-library workflow bump it) to cut a release.
#define KHIPRO_PORTABLE_VERSION "1.8"

// Icon resource IDs.
#define IDI_ACTIVE 101
#define IDI_INACTIVE 102

// Tray menu command IDs.
#define IDM_TOGGLE 40001
#define IDM_EXIT 40002
#define IDM_LAYOUT_CLASSIC 40003
#define IDM_LAYOUT_DARK 40004
#define IDM_CHANGE_HOTKEY 40005

#define HOTKEY_ID_TOGGLE 50001
#define HOTKEY_CAPTURE_TIMER 50002
#define WM_TRAYICON (WM_APP + 1)

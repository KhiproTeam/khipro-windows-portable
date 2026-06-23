// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 qomarhsn

#pragma once

// Portable-only patch. Bumped manually when this portable changes between
// library releases. Resets to "1" when the linked khipro library version moves.
// Full portable version = "<khipro_library_version()>-<KHIPRO_PORTABLE_PATCH>",
// e.g. "35.0.1-1-1" = layout 35.0.1, library patch 1, portable patch 1.
#define KHIPRO_PORTABLE_PATCH "1"

#define IDI_ACTIVE 101
#define IDI_INACTIVE 102

#define IDM_TOGGLE 40001
#define IDM_EXIT 40002

#define HOTKEY_ID_TOGGLE 50001
#define WM_TRAYICON (WM_APP + 1)

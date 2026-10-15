// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 qomarhsn

#define _WIN32_WINNT 0x0601
#define _WIN32_IE 0x0600
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <khipro/khipro.h>

#include "resources.h"

// Stamped by CI via `make PORTABLE_VERSION=<n>`; local builds report "dev".
#ifndef KHIPRO_PORTABLE_VERSION
#define KHIPRO_PORTABLE_VERSION "dev"
#endif

// Diagnostic hotkey-trace logging to khipro.log next to the exe.
// Build with `make KHIPRO_HOTKEY_DEBUG=1`; compiled out otherwise.
#ifdef KHIPRO_HOTKEY_DEBUG
void HotkeyLog(const char* line) {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring dir(path);
  const size_t slash = dir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    dir.resize(slash + 1);
  }
  dir += L"khipro.log";
  const HANDLE f = CreateFileW(dir.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                               nullptr, OPEN_ALWAYS, 0, nullptr);
  if (f == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD written = 0;
  WriteFile(f, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
  WriteFile(f, "\r\n", 2, &written, nullptr);
  CloseHandle(f);
}
#else
#define HotkeyLog(line) ((void)0)
#endif

namespace {

HINSTANCE g_instance = nullptr;
HWND g_window = nullptr;
HHOOK g_keyboard_hook = nullptr;
NOTIFYICONDATA g_nid{};
HICON g_icon_active = nullptr;
HICON g_icon_inactive = nullptr;
bool g_enabled = false;
bool g_injecting = false;
HWND g_last_focus = nullptr;

std::string g_input_buffer;
std::wstring g_rendered_output;

enum class Layout { Classic, Dark };

Layout g_layout = Layout::Classic;

UINT g_hotkey_mods = MOD_CONTROL;
UINT g_hotkey_vk = VK_SPACE;
bool g_capturing = false;
UINT g_tracked_mods = 0;  // capture-mode modifier state, from hook key events

khipro_variant VariantFor(Layout layout) {
  return layout == Layout::Dark ? KHIPRO_VARIANT_DARK : KHIPRO_VARIANT_CLASSIC;
}

const wchar_t* LayoutName(Layout layout) {
  return layout == Layout::Dark ? L"Dark" : L"Classic";
}

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) {
    return L"";
  }
  int needed = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  if (needed <= 0) {
    return L"";
  }
  std::wstring out(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), needed);
  return out;
}

std::string WideToUtf8(const std::wstring& s) {
  if (s.empty()) {
    return "";
  }
  int needed = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return "";
  }
  std::string out(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), needed, nullptr, nullptr);
  return out;
}

std::string Trim(const std::string& s) {
  const char* ws = " \t\r\n";
  const size_t begin = s.find_first_not_of(ws);
  if (begin == std::string::npos) {
    return "";
  }
  const size_t end = s.find_last_not_of(ws);
  return s.substr(begin, end - begin + 1);
}

std::string Lower(std::string s) {
  for (char& c : s) {
    if (c >= 'A' && c <= 'Z') {
      c += static_cast<char>('a' - 'A');
    }
  }
  return s;
}

// ─── Hotkey naming / parsing (khipro.cfg "hotkey=ctrl+space") ─────────

struct HotkeyName {
  UINT vk;
  const wchar_t* name;
};

const wchar_t* VkName(UINT vk) {
  static const HotkeyName kNames[] = {
      {VK_SPACE, L"Space"},   {VK_TAB, L"Tab"},     {VK_RETURN, L"Enter"},
      {VK_BACK, L"Backspace"}, {VK_ESCAPE, L"Esc"},  {VK_INSERT, L"Ins"},
      {VK_DELETE, L"Del"},    {VK_HOME, L"Home"},   {VK_END, L"End"},
      {VK_PRIOR, L"PageUp"},  {VK_NEXT, L"PageDown"}, {VK_LEFT, L"Left"},
      {VK_RIGHT, L"Right"},   {VK_UP, L"Up"},       {VK_DOWN, L"Down"},
  };
  for (const auto& n : kNames) {
    if (n.vk == vk) {
      return n.name;
    }
  }
  return nullptr;
}

std::wstring HotkeyLabel(UINT mods, UINT vk) {
  std::wstring s;
  if (mods & MOD_CONTROL) s += L"Ctrl+";
  if (mods & MOD_SHIFT) s += L"Shift+";
  if (mods & MOD_ALT) s += L"Alt+";
  if (mods & MOD_WIN) s += L"Win+";
  if (vk >= 'A' && vk <= 'Z') {
    s += static_cast<wchar_t>(L'A' + (vk - 'A'));
  } else if (vk >= '0' && vk <= '9') {
    s += static_cast<wchar_t>(L'0' + (vk - '0'));
  } else if (vk >= VK_F1 && vk <= VK_F24) {
    s += L"F" + std::to_wstring(vk - VK_F1 + 1);
  } else if (const wchar_t* name = VkName(vk)) {
    s += name;
  } else {
    wchar_t buf[16];
    wsprintfW(buf, L"0x%02X", vk);
    s += buf;
  }
  return s;
}

bool HexToUint(const std::string& s, UINT* out) {
  if (s.empty() || s.size() > 4) {
    return false;
  }
  UINT v = 0;
  for (char c : s) {
    int d;
    if (c >= '0' && c <= '9') {
      d = c - '0';
    } else if (c >= 'a' && c <= 'f') {
      d = c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
      d = c - 'A' + 10;
    } else {
      return false;
    }
    v = v * 16 + static_cast<UINT>(d);
  }
  if (v == 0) {
    return false;
  }
  *out = v;
  return true;
}

bool ParseVk(const std::string& token, UINT* vk) {
  if (token.size() == 1) {
    const char c = token[0];
    if (c >= 'a' && c <= 'z') {
      *vk = static_cast<UINT>(c - 'a' + 'A');
      return true;
    }
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      *vk = static_cast<UINT>(c);
      return true;
    }
    return false;
  }
  if ((token[0] == 'f' || token[0] == 'F') && token.size() <= 3) {
    int n = 0;
    for (size_t i = 1; i < token.size(); ++i) {
      if (token[i] < '0' || token[i] > '9') {
        return false;
      }
      n = n * 10 + (token[i] - '0');
    }
    if (n >= 1 && n <= 24) {
      *vk = VK_F1 + static_cast<UINT>(n - 1);
      return true;
    }
    return false;
  }
  static const struct {
    const char* name;
    UINT vk;
  } kNamed[] = {
      {"space", VK_SPACE},  {"tab", VK_TAB},      {"enter", VK_RETURN},
      {"backspace", VK_BACK}, {"esc", VK_ESCAPE}, {"ins", VK_INSERT},
      {"del", VK_DELETE},   {"home", VK_HOME},    {"end", VK_END},
      {"pageup", VK_PRIOR}, {"pagedown", VK_NEXT}, {"left", VK_LEFT},
      {"right", VK_RIGHT},  {"up", VK_UP},        {"down", VK_DOWN},
  };
  const std::string lower = Lower(token);
  for (const auto& n : kNamed) {
    if (lower == n.name) {
      *vk = n.vk;
      return true;
    }
  }
  if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X')) {
    return HexToUint(token.substr(2), vk);
  }
  return false;
}

bool ParseHotkey(const std::string& text, UINT* mods, UINT* vk) {
  *mods = 0;
  *vk = 0;
  bool have_vk = false;
  size_t pos = 0;
  while (pos <= text.size()) {
    const size_t plus = text.find('+', pos);
    const std::string token = Trim(text.substr(pos, plus == std::string::npos ? std::string::npos : plus - pos));
    if (!token.empty()) {
      const std::string lower = Lower(token);
      if (lower == "ctrl") {
        *mods |= MOD_CONTROL;
      } else if (lower == "alt") {
        *mods |= MOD_ALT;
      } else if (lower == "shift") {
        *mods |= MOD_SHIFT;
      } else if (lower == "win") {
        *mods |= MOD_WIN;
      } else if (!have_vk) {
        if (!ParseVk(lower, vk)) {
          return false;
        }
        have_vk = true;
      } else {
        return false;  // second key token
      }
    }
    if (plus == std::string::npos) {
      break;
    }
    pos = plus + 1;
  }
  // Shift-only or keyless combos would hijack normal typing — reject.
  return have_vk && (*mods & (MOD_CONTROL | MOD_ALT | MOD_WIN)) != 0;
}

// ─── khipro.cfg (two lines: layout, hotkey) ───────────────────────────

struct Config {
  Layout layout = Layout::Classic;
  UINT hotkey_mods = MOD_CONTROL;
  UINT hotkey_vk = VK_SPACE;
};

std::wstring ConfigPath() {
  wchar_t path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, path, MAX_PATH);
  std::wstring dir(path);
  const size_t slash = dir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    dir.resize(slash + 1);
  }
  return dir + L"khipro.cfg";
}

Config LoadConfig() {
  Config cfg;
  const HANDLE file = CreateFileW(ConfigPath().c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    HotkeyLog("[cfg] load: no file, defaults");
    return cfg;
  }
  char buf[512]{};
  DWORD read = 0;
  ReadFile(file, buf, sizeof(buf) - 1, &read, nullptr);
  CloseHandle(file);

  const std::string content(buf, read);
  HotkeyLog("[cfg] load: <<<");
  HotkeyLog(content.c_str());
  HotkeyLog("[cfg] load: >>>");
  size_t pos = 0;
  while (pos <= content.size()) {
    const size_t nl = content.find('\n', pos);
    const std::string line = Trim(content.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
    if (line.rfind("layout=", 0) == 0) {
      if (Lower(Trim(line.substr(7))) == "dark") {
        cfg.layout = Layout::Dark;
      }
    } else if (line.rfind("hotkey=", 0) == 0) {
      UINT mods = 0;
      UINT vk = 0;
      if (ParseHotkey(Trim(line.substr(7)), &mods, &vk)) {
        cfg.hotkey_mods = mods;
        cfg.hotkey_vk = vk;
      }
    }
    if (nl == std::string::npos) {
      break;
    }
    pos = nl + 1;
  }
  return cfg;
}

void SaveConfig() {
  const std::string content =
      std::string("layout=") + (g_layout == Layout::Dark ? "dark" : "classic") + "\n"
      "hotkey=" + WideToUtf8(HotkeyLabel(g_hotkey_mods, g_hotkey_vk)) + "\n";
  HotkeyLog("[cfg] save: <<<");
  HotkeyLog(content.c_str());
  HotkeyLog("[cfg] save: >>>");
  const HANDLE file = CreateFileW(ConfigPath().c_str(), GENERIC_WRITE, 0,
                                  nullptr, CREATE_ALWAYS, 0, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;  // read-only location: run without persistence
  }
  DWORD written = 0;
  WriteFile(file, content.c_str(), static_cast<DWORD>(content.size()), &written, nullptr);
  CloseHandle(file);
}

std::string KhiproConvert(const std::string& input) {
  if (input.empty()) {
    return {};
  }
  const int required = khipro_convert(VariantFor(g_layout), input.c_str(), nullptr, 0);
  if (required <= 0) {
    return {};
  }
  std::vector<char> buf(static_cast<size_t>(required));
  khipro_convert(VariantFor(g_layout), input.c_str(), buf.data(), required);
  return std::string(buf.data(), static_cast<size_t>(required - 1));
}

bool PopLastUtf8Codepoint(std::string* s) {
  if (!s || s->empty()) {
    return false;
  }
  size_t i = s->size() - 1;
  while (i > 0 && (((*s)[i] & 0xC0) == 0x80)) {
    --i;
  }
  s->erase(i);
  return true;
}

void SendBackspaces(size_t count) {
  if (count == 0) {
    return;
  }
  std::vector<INPUT> inputs;
  inputs.reserve(count * 2);
  for (size_t i = 0; i < count; ++i) {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = VK_BACK;

    INPUT up{};
    up.type = INPUT_KEYBOARD;
    up.ki.wVk = VK_BACK;
    up.ki.dwFlags = KEYEVENTF_KEYUP;

    inputs.push_back(down);
    inputs.push_back(up);
  }
  SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void SendUnicodeText(const std::wstring& text) {
  if (text.empty()) {
    return;
  }
  std::vector<INPUT> inputs;
  inputs.reserve(text.size() * 2);
  for (wchar_t ch : text) {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.wVk = 0;
    down.ki.wScan = ch;
    down.ki.dwFlags = KEYEVENTF_UNICODE;

    INPUT up{};
    up.type = INPUT_KEYBOARD;
    up.ki.wVk = 0;
    up.ki.wScan = ch;
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

    inputs.push_back(down);
    inputs.push_back(up);
  }
  SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void ResetCompositionState() {
  g_input_buffer.clear();
  g_rendered_output.clear();
}

void ApplyCompositionDelta(const std::wstring& next) {
  size_t common = 0;
  const size_t limit = std::min(g_rendered_output.size(), next.size());
  while (common < limit && g_rendered_output[common] == next[common]) {
    ++common;
  }

  const size_t backspaces = g_rendered_output.size() - common;
  const std::wstring suffix = next.substr(common);

  g_injecting = true;
  SendBackspaces(backspaces);
  SendUnicodeText(suffix);
  g_injecting = false;

  g_rendered_output = next;
}

void RecomputeAndApply() {
  const std::string converted = KhiproConvert(g_input_buffer);
  ApplyCompositionDelta(Utf8ToWide(converted));
}

bool IsModifierDown() {
  return (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000) ||
         (GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000);
}

bool VkToUtf8Char(DWORD vk, DWORD scan, std::string* out) {
  if (!out) {
    return false;
  }
  BYTE key_state[256]{};
  if (!GetKeyboardState(key_state)) {
    return false;
  }

  auto sync_mod = [&](int vk_mod) {
    if (GetAsyncKeyState(vk_mod) & 0x8000) {
      key_state[vk_mod] |= 0x80;
    } else {
      key_state[vk_mod] &= static_cast<BYTE>(~0x80);
    }
  };
  sync_mod(VK_SHIFT);
  sync_mod(VK_LSHIFT);
  sync_mod(VK_RSHIFT);
  sync_mod(VK_CONTROL);
  sync_mod(VK_LCONTROL);
  sync_mod(VK_RCONTROL);
  sync_mod(VK_MENU);
  sync_mod(VK_LMENU);
  sync_mod(VK_RMENU);

  wchar_t buf[8]{};
  const HKL layout = GetKeyboardLayout(0);
  int rc = ToUnicodeEx(static_cast<UINT>(vk), static_cast<UINT>(scan), key_state, buf, 8, 0, layout);
  if (rc <= 0) {
    return false;
  }

  std::wstring ws(buf, buf + rc);
  std::string utf8 = WideToUtf8(ws);
  if (utf8.empty()) {
    return false;
  }
  *out = utf8;
  return true;
}

std::wstring BuildTooltip() {
  std::wstring tip = L"Khipro - Compositional Bangla Layouts\n";
  tip += L"Version: " + Utf8ToWide(KHIPRO_PORTABLE_VERSION) +
         L" · Library: " + Utf8ToWide(khipro_library_version()) +
         L" · Classic: " + Utf8ToWide(KHIPRO_CLASSIC_VERSION) +
         L" · Dark: " + Utf8ToWide(KHIPRO_DARK_VERSION) + L"\n";
  tip += std::wstring(LayoutName(g_layout));
  if (g_capturing) {
    tip += L" · Press shortcut…";
  } else {
    tip += g_enabled ? L" · Active" : L" · Inactive";
  }
  return tip;
}

void UpdateTrayIcon() {
  g_nid.hIcon = g_enabled ? g_icon_active : g_icon_inactive;
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  std::wstring tip = BuildTooltip();
  wcsncpy(g_nid.szTip, tip.c_str(), ARRAYSIZE(g_nid.szTip) - 1);
  g_nid.szTip[ARRAYSIZE(g_nid.szTip) - 1] = L'\0';
  Shell_NotifyIcon(NIM_MODIFY, &g_nid);
}

void ShowBalloon(const wchar_t* text) {
  NOTIFYICONDATA nid = g_nid;
  nid.uFlags = NIF_INFO;
  nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
  wcsncpy(nid.szInfoTitle, L"Khipro", ARRAYSIZE(nid.szInfoTitle) - 1);
  nid.szInfoTitle[ARRAYSIZE(nid.szInfoTitle) - 1] = L'\0';
  wcsncpy(nid.szInfo, text, ARRAYSIZE(nid.szInfo) - 1);
  nid.szInfo[ARRAYSIZE(nid.szInfo) - 1] = L'\0';
  Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void HideBalloon() {
  NOTIFYICONDATA nid = g_nid;
  nid.uFlags = NIF_INFO;
  nid.szInfo[0] = L'\0';
  Shell_NotifyIcon(NIM_MODIFY, &nid);
}

void SetEnabled(bool enabled) {
  g_enabled = enabled;
  ResetCompositionState();
  UpdateTrayIcon();
}

void SetLayout(Layout layout) {
  if (layout == g_layout) {
    return;
  }
  g_layout = layout;
  ResetCompositionState();  // same semantics as a focus change
  SaveConfig();
  UpdateTrayIcon();
}

// Registers the new combo; on failure the old one is restored.
bool TrySetHotkey(UINT mods, UINT vk) {
  const UINT old_mods = g_hotkey_mods;
  const UINT old_vk = g_hotkey_vk;
  {
    char b[96];
    wsprintfA(b, "[hotkey] try mods=%02X vk=%02X (old mods=%02X vk=%02X)", mods, vk, old_mods, old_vk);
    HotkeyLog(b);
  }
  const BOOL unreg = UnregisterHotKey(g_window, HOTKEY_ID_TOGGLE);
  if (!RegisterHotKey(g_window, HOTKEY_ID_TOGGLE, mods | MOD_NOREPEAT, vk)) {
    const DWORD gle = GetLastError();
    const BOOL restored = RegisterHotKey(g_window, HOTKEY_ID_TOGGLE, old_mods | MOD_NOREPEAT, old_vk);
    {
      char b[96];
      wsprintfA(b, "[hotkey] reg FAILED gle=%lu unreg=%d restored=%d", gle, unreg, restored);
      HotkeyLog(b);
    }
    return false;
  }
  g_hotkey_mods = mods;
  g_hotkey_vk = vk;
  HotkeyLog("[hotkey] set OK");
  return true;
}

void CancelHotkeyCapture() {
  if (!g_capturing) {
    return;
  }
  HotkeyLog("[capture] cancel");
  g_capturing = false;
  KillTimer(g_window, HOTKEY_CAPTURE_TIMER);
  HideBalloon();
  UpdateTrayIcon();
}

void StartHotkeyCapture() {
  if (g_capturing) {
    SetTimer(g_window, HOTKEY_CAPTURE_TIMER, 10000, nullptr);  // extend the window
    return;
  }
  HotkeyLog("[capture] start");
  g_capturing = true;
  // Best-effort baseline if modifiers are already held; the hook's own key
  // events keep this accurate from here on.
  g_tracked_mods = 0;
  if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
    g_tracked_mods |= MOD_CONTROL;
  }
  if (GetAsyncKeyState(VK_MENU) & 0x8000) {
    g_tracked_mods |= MOD_ALT;
  }
  if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
    g_tracked_mods |= MOD_SHIFT;
  }
  if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) {
    g_tracked_mods |= MOD_WIN;
  }
  UpdateTrayIcon();
  ShowBalloon(L"Press the new shortcut combination (with Ctrl, Alt or Win). Esc cancels.");
  SetTimer(g_window, HOTKEY_CAPTURE_TIMER, 10000, nullptr);
}

void ShowTrayMenu() {
  HMENU menu = CreatePopupMenu();
  if (!menu) {
    return;
  }

  const wchar_t* label = g_enabled ? L"Inactive" : L"Active";
  AppendMenuW(menu, MF_STRING, IDM_TOGGLE, label);
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_LAYOUT_CLASSIC, L"Classic");
  AppendMenuW(menu, MF_STRING, IDM_LAYOUT_DARK, L"Dark");
  CheckMenuRadioItem(menu, IDM_LAYOUT_CLASSIC, IDM_LAYOUT_DARK,
                     g_layout == Layout::Dark ? IDM_LAYOUT_DARK : IDM_LAYOUT_CLASSIC,
                     MF_BYCOMMAND);
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  const std::wstring hotkey_item =
      L"Change shortcut (" + HotkeyLabel(g_hotkey_mods, g_hotkey_vk) + L")";
  AppendMenuW(menu, MF_STRING, IDM_CHANGE_HOTKEY, hotkey_item.c_str());
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(g_window);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_window, nullptr);
  DestroyMenu(menu);
}

// MOD_* bit for a modifier virtual key, 0 for anything else.
UINT ModifierBit(UINT vk) {
  switch (vk) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
      return MOD_SHIFT;
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
      return MOD_CONTROL;
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
      return MOD_ALT;
    case VK_LWIN:
    case VK_RWIN:
      return MOD_WIN;
  }
  return 0;
}

LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode < 0) {
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
  }

  // Shortcut capture mode: swallow keydowns until a valid combo, Esc, or timeout.
  // Modifier state is tracked from the hook's own key events — GetAsyncKeyState
  // is unreliable inside WH_KEYBOARD_LL.
  if (g_capturing) {
    const auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    if ((k->flags & LLKHF_INJECTED) == 0) {
      const UINT vk = k->vkCode;
      const UINT bit = ModifierBit(vk);
      const bool key_up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
      if (bit != 0) {
        if (key_up) {
          g_tracked_mods &= ~bit;
        } else {
          g_tracked_mods |= bit;
        }
      }
      if (!key_up && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        {
          char b[64];
          wsprintfA(b, "[capture-key] vk=%02X tracked=%02X", vk, g_tracked_mods);
          HotkeyLog(b);
        }
        if (vk == VK_ESCAPE) {
          CancelHotkeyCapture();
          return 1;
        }
        if (bit != 0) {
          return 1;  // modifier-only press: keep waiting
        }
        if ((g_tracked_mods & (MOD_CONTROL | MOD_ALT | MOD_WIN)) == 0 || vk == VK_F12) {
          char b[64];
          wsprintfA(b, "[capture-key] REJECTED tracked=%02X (no ctrl/alt/win or F12)", g_tracked_mods);
          HotkeyLog(b);
          MessageBeep(MB_ICONERROR);  // shift-only/bare key, or debugger-reserved
          return 1;
        }
        if (TrySetHotkey(g_tracked_mods, vk)) {
          SaveConfig();
          g_capturing = false;
          KillTimer(g_window, HOTKEY_CAPTURE_TIMER);
          HideBalloon();
          UpdateTrayIcon();
        } else {
          ShowBalloon(L"That combination is already in use — try another. Esc cancels.");
        }
        return 1;
      }
    } else {
      HotkeyLog("[capture-key] injected event ignored");
    }
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
  }

  if (!g_enabled || g_injecting) {
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
  }

  if (wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN) {
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
  }

  const auto* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
  if ((k->flags & LLKHF_INJECTED) != 0) {
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
  }

  HWND focused = GetForegroundWindow();
  if (focused != g_last_focus) {
    g_last_focus = focused;
    ResetCompositionState();
  }

  if (k->vkCode == VK_SPACE || k->vkCode == VK_RETURN || k->vkCode == VK_TAB || k->vkCode == VK_ESCAPE) {
    ResetCompositionState();
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
  }

  if (k->vkCode >= VK_NUMPAD0 && k->vkCode <= VK_DIVIDE) {
    ResetCompositionState();
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
  }

  if (k->vkCode == VK_BACK) {
    if (g_input_buffer.empty()) {
      return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
    }
    PopLastUtf8Codepoint(&g_input_buffer);
    RecomputeAndApply();
    return 1;
  }

  if (IsModifierDown()) {
    ResetCompositionState();
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
  }

  std::string typed_utf8;
  if (!VkToUtf8Char(k->vkCode, k->scanCode, &typed_utf8)) {
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
  }

  std::wstring typed_w = Utf8ToWide(typed_utf8);
  if (typed_w.empty() || typed_w[0] < 0x20) {
    return CallNextHookEx(g_keyboard_hook, nCode, wParam, lParam);
  }

  g_input_buffer += typed_utf8;
  RecomputeAndApply();
  return 1;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      // Saved combo first; if another app holds it, fall back to the default.
      // If even that fails, run without a hotkey — the menu still toggles.
      if (!RegisterHotKey(hwnd, HOTKEY_ID_TOGGLE, g_hotkey_mods | MOD_NOREPEAT, g_hotkey_vk)) {
        const DWORD gle = GetLastError();
        HotkeyLog("[startup] saved-combo reg FAILED, trying default");
        {
          char b[64];
          wsprintfA(b, "[startup] gle=%lu mods=%02X vk=%02X", gle, g_hotkey_mods, g_hotkey_vk);
          HotkeyLog(b);
        }
        if (RegisterHotKey(hwnd, HOTKEY_ID_TOGGLE, MOD_CONTROL | MOD_NOREPEAT, VK_SPACE)) {
          g_hotkey_mods = MOD_CONTROL;
          g_hotkey_vk = VK_SPACE;
          HotkeyLog("[startup] default ctrl+space registered");
        } else {
          HotkeyLog("[startup] default ALSO failed, running without hotkey");
        }
      } else {
        HotkeyLog("[startup] saved combo registered OK");
      }
      return 0;
    }
    case WM_COMMAND: {
      {
        char b[64];
        wsprintfA(b, "[menu] command id=%lu", LOWORD(wParam));
        HotkeyLog(b);
      }
      switch (LOWORD(wParam)) {
        case IDM_TOGGLE:
          SetEnabled(!g_enabled);
          return 0;
        case IDM_LAYOUT_CLASSIC:
          SetLayout(Layout::Classic);
          return 0;
        case IDM_LAYOUT_DARK:
          SetLayout(Layout::Dark);
          return 0;
        case IDM_CHANGE_HOTKEY:
          StartHotkeyCapture();
          return 0;
        case IDM_EXIT:
          DestroyWindow(hwnd);
          return 0;
      }
      break;
    }
    case WM_TIMER: {
      if (wParam == HOTKEY_CAPTURE_TIMER) {
        HotkeyLog("[capture] timeout fired");
        CancelHotkeyCapture();
      }
      return 0;
    }
    case WM_HOTKEY: {
      if (wParam == HOTKEY_ID_TOGGLE) {
        HotkeyLog("[hotkey] WM_HOTKEY fired");
        SetEnabled(!g_enabled);
      }
      return 0;
    }
    case WM_TRAYICON: {
      if (lParam == WM_LBUTTONUP) {
        SetEnabled(!g_enabled);
      } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
        ShowTrayMenu();
      }
      return 0;
    }
    case WM_DESTROY: {
      UnregisterHotKey(hwnd, HOTKEY_ID_TOGGLE);
      Shell_NotifyIcon(NIM_DELETE, &g_nid);
      if (g_keyboard_hook) {
        UnhookWindowsHookEx(g_keyboard_hook);
        g_keyboard_hook = nullptr;
      }
      PostQuitMessage(0);
      return 0;
    }
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"KhiproSingleInstance");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    return 0;
  }

  g_instance = instance;

  const Config cfg = LoadConfig();
  g_layout = cfg.layout;
  g_hotkey_mods = cfg.hotkey_mods;
  g_hotkey_vk = cfg.hotkey_vk;
  {
    char b[64];
    wsprintfA(b, "[startup] loaded mods=%02X vk=%02X", g_hotkey_mods, g_hotkey_vk);
    HotkeyLog(b);
  }

  WNDCLASSW wc{};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = instance;
  wc.lpszClassName = L"KhiproTrayClass";
  if (!RegisterClassW(&wc)) {
    return 1;
  }

  g_window = CreateWindowExW(0, wc.lpszClassName, L"Khipro", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, nullptr);
  if (!g_window) {
    return 1;
  }

  g_icon_active = LoadIconW(instance, MAKEINTRESOURCEW(IDI_ACTIVE));
  g_icon_inactive = LoadIconW(instance, MAKEINTRESOURCEW(IDI_INACTIVE));
  if (!g_icon_active) {
    g_icon_active = LoadIconW(nullptr, IDI_APPLICATION);
  }
  if (!g_icon_inactive) {
    g_icon_inactive = LoadIconW(nullptr, IDI_ERROR);
  }

  g_nid.cbSize = sizeof(g_nid);
  g_nid.hWnd = g_window;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  g_nid.uCallbackMessage = WM_TRAYICON;
  g_nid.hIcon = g_icon_inactive;
  {
    std::wstring tip = BuildTooltip();
    wcsncpy(g_nid.szTip, tip.c_str(), ARRAYSIZE(g_nid.szTip) - 1);
    g_nid.szTip[ARRAYSIZE(g_nid.szTip) - 1] = L'\0';
  }
  if (!Shell_NotifyIcon(NIM_ADD, &g_nid)) {
    return 1;
  }

  g_keyboard_hook = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, instance, 0);
  if (!g_keyboard_hook) {
    MessageBoxW(nullptr, L"Failed to install keyboard hook", L"Khipro", MB_ICONERROR | MB_OK);
    return 1;
  }

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  return 0;
}

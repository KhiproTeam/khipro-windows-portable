#define _WIN32_WINNT 0x0601
#define _WIN32_IE 0x0600
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "mim_engine.h"
#include "resources.h"

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

std::unique_ptr<khipro::KhiproEngine> g_engine;
std::string g_input_buffer;
std::wstring g_rendered_output;

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

std::string LoadEmbeddedMim() {
  HRSRC resource = FindResourceW(g_instance, MAKEINTRESOURCEW(IDR_MIM), RT_RCDATA);
  if (!resource) {
    return {};
  }
  HGLOBAL loaded = LoadResource(g_instance, resource);
  if (!loaded) {
    return {};
  }
  DWORD size = SizeofResource(g_instance, resource);
  if (size == 0) {
    return {};
  }
  const void* ptr = LockResource(loaded);
  if (!ptr) {
    return {};
  }
  return std::string(static_cast<const char*>(ptr), static_cast<size_t>(size));
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
  if (g_engine) {
    g_engine->Reset();
  }
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
  if (!g_engine) {
    return;
  }
  const std::string converted = g_engine->Convert(g_input_buffer);
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
  std::wstring tip = L"Khipro - The first compositional lowercase keyboard layout concept for Bangla\n";
  tip += L"Version: " + Utf8ToWide(KHIPRO_VERSION) + L"\n";
  tip += L"Status: " + (g_enabled ? std::wstring(L"Active") : std::wstring(L"Inactive"));
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

void SetEnabled(bool enabled) {
  g_enabled = enabled;
  ResetCompositionState();
  UpdateTrayIcon();
}

void ShowTrayMenu() {
  HMENU menu = CreatePopupMenu();
  if (!menu) {
    return;
  }

  const wchar_t* label = g_enabled ? L"Inactive" : L"Active";
  AppendMenuW(menu, MF_STRING, IDM_TOGGLE, label);
  AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(g_window);
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_window, nullptr);
  DestroyMenu(menu);
}

LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode < 0) {
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
    khipro::PopLastUtf8Codepoint(&g_input_buffer);
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
      RegisterHotKey(hwnd, HOTKEY_ID_TOGGLE, MOD_CONTROL, VK_SPACE);
      return 0;
    }
    case WM_COMMAND: {
      switch (LOWORD(wParam)) {
        case IDM_TOGGLE:
          SetEnabled(!g_enabled);
          return 0;
        case IDM_EXIT:
          DestroyWindow(hwnd);
          return 0;
      }
      break;
    }
    case WM_HOTKEY: {
      if (wParam == HOTKEY_ID_TOGGLE) {
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
  g_instance = instance;

  std::string mim = LoadEmbeddedMim();
  if (mim.empty()) {
    MessageBoxW(nullptr, L"Failed to load embedded bn-khipro.mim", L"Khipro", MB_ICONERROR | MB_OK);
    return 1;
  }

  g_engine = std::make_unique<khipro::KhiproEngine>(mim);

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

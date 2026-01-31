#include "system_tray.h"
#include "debug_log.h"
#include "toast_notification.h"
#include "wiimote_manager.h"
#include "wiimote_led_setter.h"
#include <sstream>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

const int WM_TRAYICON = WM_APP + 1;
const int WM_TIMER_UPDATE = WM_APP + 2;
const int ID_TRAY_ICON = 1001;
const int TIMER_ID = 1002;

enum MenuItems {
  ID_STATUS = 1,
  ID_OPEN_PAIRING = 2,
  ID_OPEN_PAIRING_1MIN = 3,
  ID_CLOSE_PAIRING = 4,
  ID_CONNECTED_DEVICES = 5,
  ID_EXIT = 6,
  ID_DEVICE_BASE = 1000,
  ID_DISCONNECT_BASE = 2000,
  ID_FORGET_BASE = 3000
};

static WiimoteManager *g_wiimote_manager = nullptr;

SystemTray *SystemTray::s_instance = nullptr;

void SystemTray::SetWiimoteManager(WiimoteManager *manager) {
  g_wiimote_manager = manager;
}

SystemTray::SystemTray()
    : m_hwnd(nullptr), m_hInstance(nullptr),
      m_current_mode(PairingMode::Closed),
      m_status_message("Wii Remote Pairing Bridge - Status: Idle"),
      m_countdown_seconds(0), m_menu_open(false), m_active_menu(nullptr) {
  s_instance = this;
}

SystemTray::~SystemTray() {
  if (m_hwnd) {
    Shell_NotifyIconW(NIM_DELETE, &m_nid);
    DestroyWindow(m_hwnd);
  }
}

bool SystemTray::Initialize(HINSTANCE hInstance) {
  m_hInstance = hInstance;
  RegisterWindowClass();

  m_hwnd = CreateWindowExW(0, L"WiimoteBridgeClass",
                           L"Wii Remote Pairing Bridge", WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                           CW_USEDEFAULT, nullptr, nullptr, hInstance, this);

  if (!m_hwnd)
    return false;

  ShowWindow(m_hwnd, SW_HIDE);

  SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

  ZeroMemory(&m_nid, sizeof(m_nid));
  m_nid.cbSize = sizeof(NOTIFYICONDATAW);
  m_nid.hWnd = m_hwnd;
  m_nid.uID = ID_TRAY_ICON;
  m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  m_nid.uCallbackMessage = WM_TRAYICON;

  HICON custom_icon = LoadIcon(hInstance, MAKEINTRESOURCE(1));

  if (!custom_icon) {
    custom_icon = (HICON)LoadImageW(nullptr, L"wiimoteicon.ico", IMAGE_ICON, 32,
                                    32, LR_LOADFROMFILE | LR_SHARED);
  }

  if (custom_icon)
    m_nid.hIcon = custom_icon;
  else
    m_nid.hIcon = LoadIcon(nullptr, IDI_APPLICATION);

  wcscpy_s(m_nid.szTip, L"Wii Remote Pairing Bridge");

  if (!Shell_NotifyIconW(NIM_ADD, &m_nid))
    return false;

  m_nid.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &m_nid);

  UpdateTrayIcon();
  return true;
}

void SystemTray::RegisterWindowClass() {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = WindowProc;
  wc.hInstance = m_hInstance;
  wc.lpszClassName = L"WiimoteBridgeClass";
  wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

  RegisterClassW(&wc);
}

void SystemTray::StartPairing60Seconds()
{
  LOG_INFO("Auto-starting 60-second pairing mode");
  m_current_mode = PairingMode::PairingOneMinute;
  m_countdown_seconds = 60;
  m_status_message = "Pairing enabled for 60 seconds";
  UpdateTrayIcon();
  SetTimer(m_hwnd, TIMER_ID, 1000, nullptr);
  if (g_wiimote_manager) {
    g_wiimote_manager->StartPairingForOneMinute();
  }
}

HMENU SystemTray::BuildDevicesSubmenu()
{
  HMENU submenu = CreatePopupMenu();
  
  auto devices = WiimoteLedSetter::Instance().GetConnectedBluetoothDevices();
  
  if (devices.empty())
  {
    AppendMenuW(submenu, MFT_STRING | MFS_GRAYED, 0, L"No devices connected");
    return submenu;
  }

  for (size_t i = 0; i < devices.size(); ++i)
  {
    HMENU deviceMenu = CreatePopupMenu();
    
    AppendMenuW(deviceMenu, MFT_STRING, ID_DISCONNECT_BASE + i, L"Disconnect");
    AppendMenuW(deviceMenu, MFT_STRING, ID_FORGET_BASE + i, L"Forget");
    
    AppendMenuW(submenu, MF_POPUP, (UINT_PTR)deviceMenu, devices[i].device_name.c_str());
  }

  return submenu;
}

LRESULT CALLBACK SystemTray::WindowProc(HWND hwnd, UINT message, WPARAM wParam,
                                        LPARAM lParam) {
  SystemTray *pThis = nullptr;

  if (message == WM_CREATE) {
    CREATESTRUCT *pCreate = reinterpret_cast<CREATESTRUCT *>(lParam);
    pThis = reinterpret_cast<SystemTray *>(pCreate->lpCreateParams);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
  } else {
    pThis =
        reinterpret_cast<SystemTray *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  }

  if (!pThis)
    return DefWindowProc(hwnd, message, wParam, lParam);

  switch (message) {
  case WM_CREATE:
    return 0;

  case WM_TRAYICON: {
    UINT msg = LOWORD(lParam);
    if (msg == WM_RBUTTONUP || msg == WM_CONTEXTMENU) {
      pThis->ShowContextMenu();
    } else if (msg == WM_LBUTTONDBLCLK) {
      pThis->ShowContextMenu();
    }
    return 0;
  }

  case WM_COMMAND: {
    int menu_id = LOWORD(wParam);
    
    if (menu_id >= ID_DISCONNECT_BASE && menu_id < ID_DISCONNECT_BASE + 100)
    {
      int device_index = menu_id - ID_DISCONNECT_BASE;
      auto devices = WiimoteLedSetter::Instance().GetConnectedBluetoothDevices();
      if (device_index < static_cast<int>(devices.size()))
      {
        WiimoteLedSetter::Instance().DisconnectDeviceByAddress(devices[device_index].bt_address);
        LOG_INFO("Disconnected device via menu");
      }
      return 0;
    }
    
    if (menu_id >= ID_FORGET_BASE && menu_id < ID_FORGET_BASE + 100)
    {
      int device_index = menu_id - ID_FORGET_BASE;
      auto devices = WiimoteLedSetter::Instance().GetConnectedBluetoothDevices();
      if (device_index < static_cast<int>(devices.size()))
      {
        const auto& device = devices[device_index];
        WiimoteLedSetter::Instance().ForgetDevice(device.bt_address);
        LOG_INFO("Forgot device via menu");
      }
      return 0;
    }
    
    switch (menu_id) {
    case ID_OPEN_PAIRING:
      LOG_INFO("Menu: Open Pairing selected");
      KillTimer(hwnd, TIMER_ID);
      pThis->m_countdown_seconds = 0;
      pThis->m_current_mode = PairingMode::Pairing;
      pThis->m_status_message =
          "Pairing enabled - Press sync button on Wii Remote";
      pThis->UpdateTrayIcon();
      if (g_wiimote_manager) {
        g_wiimote_manager->StartPairing();
      }
      break;

    case ID_OPEN_PAIRING_1MIN:
      LOG_INFO("Menu: Open Pairing (1 minute) selected");
      pThis->m_current_mode = PairingMode::PairingOneMinute;
      pThis->m_countdown_seconds = 60;
      pThis->m_status_message = "Pairing enabled for 60 seconds";
      pThis->UpdateTrayIcon();
      SetTimer(hwnd, TIMER_ID, 1000, nullptr);
      if (g_wiimote_manager) {
        g_wiimote_manager->StartPairingForOneMinute();
      }
      break;

    case ID_CLOSE_PAIRING:
      LOG_INFO("Menu: Close Pairing selected");
      KillTimer(hwnd, TIMER_ID);
      pThis->m_countdown_seconds = 0;
      pThis->m_current_mode = PairingMode::Closed;
      pThis->m_status_message = "Pairing disabled";
      pThis->UpdateTrayIcon();
      if (g_wiimote_manager) {
        g_wiimote_manager->StopPairing();
      }
      break;

    case ID_EXIT:
      LOG_INFO("Menu: Exit selected");
      KillTimer(hwnd, TIMER_ID);
      if (g_wiimote_manager) {
        g_wiimote_manager->StopPairing();
      }
      Shell_NotifyIconW(NIM_DELETE, &pThis->m_nid);
      DestroyWindow(hwnd);
      PostQuitMessage(0);
      break;
    }
    return 0;
  }

  case WM_TIMER:
    if (wParam == TIMER_ID) {
      if (pThis->m_countdown_seconds > 0) {
        pThis->m_countdown_seconds--;
        pThis->UpdateTrayIcon();

        if (pThis->m_countdown_seconds == 0) {
          LOG_INFO("Pairing timer expired");
          KillTimer(hwnd, TIMER_ID);
          pThis->m_current_mode = PairingMode::Closed;
          pThis->m_status_message = "Pairing mode timed out";
          pThis->UpdateTrayIcon();
          if (g_wiimote_manager) {
            g_wiimote_manager->StopPairing();
          }
        }
      }
    }
    return 0;

  case WM_DESTROY:
    KillTimer(hwnd, TIMER_ID);
    PostQuitMessage(0);
    return 0;

  case WM_WIIMOTE_CONNECTED: {
    wchar_t *deviceName = reinterpret_cast<wchar_t *>(wParam);
    if (deviceName) {
      pThis->ShowToast(L"Wii Remote Connected!",
                       std::wstring(L"Successfully paired: ") + deviceName,
                       true);
      delete[] deviceName;
    } else {
      pThis->ShowToast(L"Wii Remote Connected!",
                       L"A Wii Remote has been paired successfully!", true);
    }
    return 0;
  }

  default:
    return DefWindowProc(hwnd, message, wParam, lParam);
  }

  return 0;
}

void SystemTray::ShowContextMenu() {
  POINT pt;
  GetCursorPos(&pt);

  HMENU hmenu = CreatePopupMenu();

  std::wstring status_text = L"  ";
  switch (m_current_mode) {
  case PairingMode::Closed:
    status_text += L"Idle";
    break;
  case PairingMode::Pairing:
    status_text += L"Pairing enabled";
    break;
  case PairingMode::PairingOneMinute:
    status_text += L"Pairing (";
    status_text += std::to_wstring(m_countdown_seconds);
    status_text += L"s remaining)";
    break;
  }

  AppendMenuW(hmenu, MFT_STRING, ID_STATUS, status_text.c_str());
  EnableMenuItem(hmenu, ID_STATUS, MF_BYCOMMAND | MF_GRAYED);

  AppendMenuW(hmenu, MFT_SEPARATOR, 0, nullptr);
  
  auto devices = WiimoteLedSetter::Instance().GetConnectedBluetoothDevices();
  if (devices.empty())
  {
    AppendMenuW(hmenu, MFT_STRING | MFS_GRAYED, ID_CONNECTED_DEVICES, L"Connected Wiimotes");
  }
  else
  {
    HMENU devicesSubmenu = BuildDevicesSubmenu();
    AppendMenuW(hmenu, MF_POPUP, (UINT_PTR)devicesSubmenu, L"Connected Wiimotes");
  }
  
  AppendMenuW(hmenu, MFT_SEPARATOR, 0, nullptr);
  AppendMenuW(hmenu, MFT_STRING, ID_OPEN_PAIRING, L"Open Pairing");
  AppendMenuW(hmenu, MFT_STRING, ID_OPEN_PAIRING_1MIN,
              L"Open Pairing (1 minute)");
  AppendMenuW(hmenu, MFT_STRING, ID_CLOSE_PAIRING, L"Close Pairing");
  AppendMenuW(hmenu, MFT_SEPARATOR, 0, nullptr);
  AppendMenuW(hmenu, MFT_STRING, ID_EXIT, L"Exit");

  SetForegroundWindow(m_hwnd);
  TrackPopupMenu(hmenu, TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, m_hwnd,
                 nullptr);
  PostMessage(m_hwnd, WM_NULL, 0, 0);

  DestroyMenu(hmenu);
}

void SystemTray::UpdateTrayIcon() {
  std::wstring tooltip = L"Wii Remote Bridge - ";
  switch (m_current_mode) {
  case PairingMode::Closed:
    tooltip += L"Idle";
    break;
  case PairingMode::Pairing:
    tooltip += L"Pairing Enabled";
    break;
  case PairingMode::PairingOneMinute:
    tooltip += L"Pairing (";
    tooltip += std::to_wstring(m_countdown_seconds);
    tooltip += L"s)";
    break;
  }

  wcscpy_s(m_nid.szTip, tooltip.c_str());
  Shell_NotifyIconW(NIM_MODIFY, &m_nid);
}

void SystemTray::SetStatusMessage(const std::string &message) {
  m_status_message = message;
}

void SystemTray::ShowToast(const std::wstring &title,
                           const std::wstring &message, bool isSuccess) {
  std::string titleNarrow, messageNarrow;
  int titleSize = WideCharToMultiByte(CP_UTF8, 0, title.c_str(), -1, nullptr, 0,
                                      nullptr, nullptr);
  int msgSize = WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, nullptr, 0,
                                    nullptr, nullptr);
  if (titleSize > 0) {
    titleNarrow.resize(titleSize - 1);
    WideCharToMultiByte(CP_UTF8, 0, title.c_str(), -1, &titleNarrow[0],
                        titleSize, nullptr, nullptr);
  }
  if (msgSize > 0) {
    messageNarrow.resize(msgSize - 1);
    WideCharToMultiByte(CP_UTF8, 0, message.c_str(), -1, &messageNarrow[0],
                        msgSize, nullptr, nullptr);
  }
  LOG_INFO("Showing toast: " + titleNarrow + " - " + messageNarrow);

  if (isSuccess) {
    ToastNotification::ShowSuccess(m_hwnd, &m_nid, title, message);
  } else {
    ToastNotification::ShowError(m_hwnd, &m_nid, title, message);
  }
}
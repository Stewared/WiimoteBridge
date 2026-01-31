#pragma once

#include <string>
#include <memory>
#include <windows.h>
#include <shellapi.h>
#include <functional>

enum class PairingMode
{
    Closed,
    Pairing,
    PairingOneMinute
};

class WiimoteManager;

const UINT WM_WIIMOTE_CONNECTED = WM_APP + 100;
const UINT WM_WIIMOTE_PAIRING_STATUS = WM_APP + 101;

class SystemTray
{
public:
    SystemTray();
    ~SystemTray();

    bool Initialize(HINSTANCE hInstance);
    void ShowContextMenu();
    void SetStatusMessage(const std::string& message);
    void SetWiimoteManager(WiimoteManager* manager);
    
    int GetCountdownSeconds() const { return m_countdown_seconds; }
    PairingMode GetCurrentMode() const { return m_current_mode; }
    
    void ShowToast(const std::wstring& title, const std::wstring& message, bool isSuccess = true);
    
    HWND GetHwnd() const { return m_hwnd; }
    
    static SystemTray* GetInstance() { return s_instance; }

    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

    void StartPairing60Seconds();

private:
    HWND m_hwnd;
    HINSTANCE m_hInstance;
    NOTIFYICONDATAW m_nid;
    PairingMode m_current_mode;
    std::string m_status_message;
    int m_countdown_seconds;
    bool m_menu_open;
    HMENU m_active_menu;
    
    static SystemTray* s_instance;

    void UpdateTrayIcon();
    void RegisterWindowClass();
    HMENU BuildDevicesSubmenu();
};
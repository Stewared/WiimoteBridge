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

// Custom window messages for inter-thread communication
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
    
    // Get current countdown for external access
    int GetCountdownSeconds() const { return m_countdown_seconds; }
    PairingMode GetCurrentMode() const { return m_current_mode; }
    
    // Toast notifications
    void ShowToast(const std::wstring& title, const std::wstring& message, bool isSuccess = true);
    
    // Get window handle for posting messages from other threads
    HWND GetHwnd() const { return m_hwnd; }
    
    // Get global instance
    static SystemTray* GetInstance() { return s_instance; }

    // Window message processing
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);

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
};

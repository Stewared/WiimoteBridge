#pragma once

#include <windows.h>
#include <shellapi.h>
#include <string>

// Toast notification utility for WiimoteBridge
// Uses balloon tips from system tray icon for broad compatibility

class ToastNotification
{
public:
    enum class Type
    {
        Info,
        Success,
        Warning,
        Error
    };

    // Show a toast notification via the system tray icon
    static void Show(HWND hwnd, NOTIFYICONDATAW* nid, 
                     const std::wstring& title, 
                     const std::wstring& message,
                     Type type = Type::Info)
    {
        if (!nid) return;

        // Create a copy of the notification data for modification
        NOTIFYICONDATAW nidCopy = *nid;
        
        // Set up balloon notification
        nidCopy.uFlags = NIF_INFO | NIF_ICON | NIF_MESSAGE | NIF_TIP;
        wcscpy_s(nidCopy.szInfoTitle, title.c_str());
        wcscpy_s(nidCopy.szInfo, message.c_str());
        nidCopy.uTimeout = 3000;  // 3 seconds (though Windows may ignore this)

        // Set icon based on type
        switch (type)
        {
        case Type::Success:
        case Type::Info:
            nidCopy.dwInfoFlags = NIIF_INFO;
            break;
        case Type::Warning:
            nidCopy.dwInfoFlags = NIIF_WARNING;
            break;
        case Type::Error:
            nidCopy.dwInfoFlags = NIIF_ERROR;
            break;
        }

        Shell_NotifyIconW(NIM_MODIFY, &nidCopy);
    }

    // Convenience methods
    static void ShowInfo(HWND hwnd, NOTIFYICONDATAW* nid, 
                         const std::wstring& title, 
                         const std::wstring& message)
    {
        Show(hwnd, nid, title, message, Type::Info);
    }

    static void ShowSuccess(HWND hwnd, NOTIFYICONDATAW* nid, 
                            const std::wstring& title, 
                            const std::wstring& message)
    {
        Show(hwnd, nid, title, message, Type::Success);
    }

    static void ShowError(HWND hwnd, NOTIFYICONDATAW* nid, 
                          const std::wstring& title, 
                          const std::wstring& message)
    {
        Show(hwnd, nid, title, message, Type::Error);
    }
};

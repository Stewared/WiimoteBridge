#include "registry_utils.h"
#include <shlobj.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Advapi32.lib")

/** 
 * Registry functions to make this program request to launch at boot
 */

bool RegistryUtils::RegisterAutoStart(const std::string& app_path)
{
    HKEY hKey;
    LONG result = RegOpenKeyExA(
        HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0,
        KEY_WRITE,
        &hKey
    );

    if (result != ERROR_SUCCESS)
        return false;

    result = RegSetValueExA(
        hKey,
        "WiimoteBridge",
        0,
        REG_SZ,
        (LPBYTE)app_path.c_str(),
        (DWORD)(app_path.size() + 1)
    );

    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool RegistryUtils::UnregisterAutoStart()
{
    HKEY hKey;
    LONG result = RegOpenKeyExA(
        HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0,
        KEY_WRITE,
        &hKey
    );

    if (result != ERROR_SUCCESS)
        return false;

    result = RegDeleteValueA(hKey, "WiimoteBridge");
    RegCloseKey(hKey);

    return result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND;
}

bool RegistryUtils::IsAutoStartEnabled()
{
    HKEY hKey;
    LONG result = RegOpenKeyExA(
        HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0,
        KEY_READ,
        &hKey
    );

    if (result != ERROR_SUCCESS)
        return false;

    char buffer[MAX_PATH];
    DWORD buffer_size = sizeof(buffer);
    result = RegQueryValueExA(hKey, "WiimoteBridge", nullptr, nullptr, (LPBYTE)buffer, &buffer_size);

    RegCloseKey(hKey);
    return result == ERROR_SUCCESS;
}

bool RegistryUtils::SetAutoStartEnabled(bool enabled)
{
    if (enabled)
    {
        // Get the executable path
        char app_path[MAX_PATH];
        GetModuleFileNameA(nullptr, app_path, MAX_PATH);
        return RegisterAutoStart(app_path);
    }
    else
    {
        return UnregisterAutoStart();
    }
}

std::string RegistryUtils::GetRegistryString(HKEY hKey, const std::string& subKey, const std::string& valueName)
{
    HKEY regKey;
    LONG result = RegOpenKeyExA(hKey, subKey.c_str(), 0, KEY_READ, &regKey);

    if (result != ERROR_SUCCESS)
        return "";

    char buffer[1024];
    DWORD buffer_size = sizeof(buffer);
    result = RegQueryValueExA(regKey, valueName.c_str(), nullptr, nullptr, (LPBYTE)buffer, &buffer_size);

    RegCloseKey(regKey);

    if (result == ERROR_SUCCESS)
        return std::string(buffer);

    return "";
}

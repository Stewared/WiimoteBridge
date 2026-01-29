#pragma once

#include <string>
#include <windows.h>

class RegistryUtils
{
public:
    static bool RegisterAutoStart(const std::string& app_path);
    static bool UnregisterAutoStart();
    static bool IsAutoStartEnabled();
    static bool SetAutoStartEnabled(bool enabled);
    static std::string GetRegistryString(HKEY hKey, const std::string& subKey, const std::string& valueName);
};

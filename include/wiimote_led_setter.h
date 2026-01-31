#pragma once

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <BluetoothAPIs.h>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include "debug_log.h"

#pragma comment(lib, "Hid.lib")
#pragma comment(lib, "Bthprops.lib")

class WiimoteLedSetter
{
public:
    static WiimoteLedSetter& Instance()
    {
        static WiimoteLedSetter instance;
        return instance;
    }

    ~WiimoteLedSetter()
    {
        StopBlinking();
    }

    void StartBlinking()
    {
        if (m_blink_thread_running)
            return;

        m_blink_thread_running = true;
        m_blink_thread = std::thread([this]() { BlinkThreadProc(); });
    }

    void StopBlinking()
    {
        if (!m_blink_thread_running)
            return;

        m_blink_thread_running = false;
        if (m_blink_thread.joinable())
            m_blink_thread.join();
    }

    struct WiimoteDeviceInfo
    {
        std::wstring device_path;
        std::wstring device_name;
        BLUETOOTH_ADDRESS bt_address;
    };

    void RegisterDevice(const std::wstring& device_path, const std::wstring& device_name = L"", const BLUETOOTH_ADDRESS* bt_addr = nullptr)
    {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        if (m_tracked_devices.find(device_path) == m_tracked_devices.end())
        {
            WiimoteDeviceInfo info;
            info.device_path = device_path;
            info.device_name = device_name.empty() ? L"Wii Remote" : device_name;
            if (bt_addr)
                info.bt_address = *bt_addr;
            else
                ZeroMemory(&info.bt_address, sizeof(BLUETOOTH_ADDRESS));
            
            m_tracked_devices[device_path] = info;
            LOG_INFO("Registered Wiimote for LED blinking");
        }
    }

    int SetLedsOnAllWiimotes()
    {
        return EnumerateAndSetLeds(false);
    }

    int DetectAndRegisterNewWiimotes()
    {
        return EnumerateAndSetLeds(true);
    }

    std::vector<WiimoteDeviceInfo> GetConnectedDevices()
    {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        std::vector<WiimoteDeviceInfo> devices;
        for (const auto& pair : m_tracked_devices)
        {
            devices.push_back(pair.second);
        }
        return devices;
    }

    bool DisconnectDevice(const std::wstring& device_path)
    {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        auto it = m_tracked_devices.find(device_path);
        if (it != m_tracked_devices.end())
        {
            HANDLE deviceHandle = CreateFileW(
                device_path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, 0, nullptr);

            if (deviceHandle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(deviceHandle);
            }
            
            m_tracked_devices.erase(it);
            LOG_INFO("Disconnected Wiimote");
            return true;
        }
        return false;
    }

    bool ForgetDevice(const BLUETOOTH_ADDRESS& bt_addr)
    {
        if (BluetoothRemoveDevice(&bt_addr) == ERROR_SUCCESS)
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            for (auto it = m_tracked_devices.begin(); it != m_tracked_devices.end(); ++it)
            {
                if (memcmp(&it->second.bt_address, &bt_addr, sizeof(BLUETOOTH_ADDRESS)) == 0)
                {
                    m_tracked_devices.erase(it);
                    break;
                }
            }
            LOG_INFO("Forgot Wiimote device");
            return true;
        }
        return false;
    }

private:
    WiimoteLedSetter() : m_blink_thread_running(false), m_current_led_pattern(0) {}
    WiimoteLedSetter(const WiimoteLedSetter&) = delete;
    WiimoteLedSetter& operator=(const WiimoteLedSetter&) = delete;

    std::thread m_blink_thread;
    std::atomic<bool> m_blink_thread_running;
    std::map<std::wstring, WiimoteDeviceInfo> m_tracked_devices;
    std::mutex m_devices_mutex;
    int m_current_led_pattern;

    void BlinkThreadProc()
    {
        const int patterns[] = { 0x08, 0x04, 0x02, 0x01 };
        int pattern_index = 0;

        while (m_blink_thread_running)
        {
            m_current_led_pattern = patterns[pattern_index];
            SetLedPattern(m_current_led_pattern);
            
            pattern_index = (pattern_index + 1) % 4;

            for (int i = 0; i < 30 && m_blink_thread_running; ++i)
            {
                Sleep(100);
            }
        }
    }

    void SetLedPattern(int ledMask)
    {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        
        GUID hidGuid;
        HidD_GetHidGuid(&hidGuid);

        HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
            &hidGuid, nullptr, nullptr, 
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

        if (deviceInfoSet == INVALID_HANDLE_VALUE)
            return;

        SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
        deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(
            deviceInfoSet, nullptr, &hidGuid, i, &deviceInterfaceData); ++i)
        {
            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetailW(
                deviceInfoSet, &deviceInterfaceData, 
                nullptr, 0, &requiredSize, nullptr);

            if (requiredSize == 0)
                continue;

            std::vector<BYTE> detailBuffer(requiredSize);
            PSP_DEVICE_INTERFACE_DETAIL_DATA_W detailData = 
                reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            if (!SetupDiGetDeviceInterfaceDetailW(
                deviceInfoSet, &deviceInterfaceData,
                detailData, requiredSize, nullptr, nullptr))
            {
                continue;
            }

            std::wstring devicePath(detailData->DevicePath);
            
            if (m_tracked_devices.find(devicePath) == m_tracked_devices.end())
                continue;

            HANDLE deviceHandle = CreateFileW(
                detailData->DevicePath,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, 0, nullptr);

            if (deviceHandle == INVALID_HANDLE_VALUE)
            {
                m_tracked_devices.erase(devicePath);
                continue;
            }

            HIDD_ATTRIBUTES attributes;
            attributes.Size = sizeof(HIDD_ATTRIBUTES);
            if (HidD_GetAttributes(deviceHandle, &attributes))
            {
                if (attributes.VendorID == 0x057e && 
                    (attributes.ProductID == 0x0306 || attributes.ProductID == 0x0330))
                {
                    BYTE ledReport[2] = { 0x11, static_cast<BYTE>((ledMask & 0x0F) << 4) };
                    DWORD bytesWritten = 0;
                    WriteFile(deviceHandle, ledReport, sizeof(ledReport), &bytesWritten, nullptr);
                }
            }

            CloseHandle(deviceHandle);
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);
    }

    int EnumerateAndSetLeds(bool detect_new)
    {
        GUID hidGuid;
        HidD_GetHidGuid(&hidGuid);

        HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
            &hidGuid, nullptr, nullptr, 
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

        if (deviceInfoSet == INVALID_HANDLE_VALUE)
        {
            LOG_ERROR("SetupDiGetClassDevs failed");
            return 0;
        }

        int count = 0;
        SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
        deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(
            deviceInfoSet, nullptr, &hidGuid, i, &deviceInterfaceData); ++i)
        {
            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetailW(
                deviceInfoSet, &deviceInterfaceData, 
                nullptr, 0, &requiredSize, nullptr);

            if (requiredSize == 0)
                continue;

            std::vector<BYTE> detailBuffer(requiredSize);
            PSP_DEVICE_INTERFACE_DETAIL_DATA_W detailData = 
                reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detailBuffer.data());
            detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

            if (!SetupDiGetDeviceInterfaceDetailW(
                deviceInfoSet, &deviceInterfaceData,
                detailData, requiredSize, nullptr, nullptr))
            {
                continue;
            }

            std::wstring devicePath(detailData->DevicePath);

            HANDLE deviceHandle = CreateFileW(
                detailData->DevicePath,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, 0, nullptr);

            if (deviceHandle == INVALID_HANDLE_VALUE)
                continue;

            HIDD_ATTRIBUTES attributes;
            attributes.Size = sizeof(HIDD_ATTRIBUTES);
            if (HidD_GetAttributes(deviceHandle, &attributes))
            {
                if (attributes.VendorID == 0x057e && 
                    (attributes.ProductID == 0x0306 || attributes.ProductID == 0x0330))
                {
                    if (detect_new)
                    {
                        std::lock_guard<std::mutex> lock(m_devices_mutex);
                        if (m_tracked_devices.find(devicePath) == m_tracked_devices.end())
                        {
                            WiimoteDeviceInfo info;
                            info.device_path = devicePath;
                            info.device_name = (attributes.ProductID == 0x0330) ? L"Wii Remote Plus" : L"Wii Remote";
                            ZeroMemory(&info.bt_address, sizeof(BLUETOOTH_ADDRESS));
                            m_tracked_devices[devicePath] = info;
                            LOG_NOTICE("Detected pre-paired Wiimote, starting LED animation");
                            count++;
                        }
                    }
                    else
                    {
                        RegisterDevice(devicePath);
                        count++;
                    }
                }
            }

            CloseHandle(deviceHandle);
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        return count;
    }
};
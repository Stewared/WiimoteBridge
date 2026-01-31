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
        bool has_bt_address;
    };

    void RegisterDevice(const std::wstring& device_path, const std::wstring& device_name = L"", const BLUETOOTH_ADDRESS* bt_addr = nullptr)
    {
        std::lock_guard<std::mutex> lock(m_devices_mutex);
        if (m_tracked_devices.find(device_path) == m_tracked_devices.end())
        {
            WiimoteDeviceInfo info;
            info.device_path = device_path;
            info.device_name = device_name.empty() ? L"Wii Remote" : device_name;
            if (bt_addr) {
                info.bt_address = *bt_addr;
                info.has_bt_address = true;
            } else {
                ZeroMemory(&info.bt_address, sizeof(BLUETOOTH_ADDRESS));
                info.has_bt_address = false;
            }
            
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

    std::vector<WiimoteDeviceInfo> GetConnectedBluetoothDevices()
    {
        std::vector<WiimoteDeviceInfo> devices;

        constexpr BLUETOOTH_FIND_RADIO_PARAMS radio_params{ .dwSize = sizeof(radio_params) };
        HANDLE radio_handle{};
        auto find_radio = BluetoothFindFirstRadio(&radio_params, &radio_handle);
        if (!find_radio)
            return devices;

        do {
            BLUETOOTH_DEVICE_SEARCH_PARAMS search_params{
                .dwSize = sizeof(search_params),
                .fReturnAuthenticated = true,
                .fReturnRemembered = true,
                .fReturnUnknown = false,
                .fReturnConnected = true,
                .fIssueInquiry = false,
                .hRadio = radio_handle,
            };

            BLUETOOTH_DEVICE_INFO btdi{ .dwSize = sizeof(btdi) };
            auto find_device = BluetoothFindFirstDevice(&search_params, &btdi);
            if (find_device)
            {
                do {
                    std::wstring name(btdi.szName);
                    if (btdi.fConnected &&
                        (name.find(L"Nintendo RVL-CNT") == 0 || name.find(L"Nintendo RVL-WBC") == 0))
                    {
                        WiimoteDeviceInfo info;
                        info.device_path = L"";
                        info.device_name = name;
                        info.bt_address = btdi.Address;
                        info.has_bt_address = true;
                        devices.push_back(info);
                    }
                } while (BluetoothFindNextDevice(find_device, &btdi));
                BluetoothFindDeviceClose(find_device);
            }
            CloseHandle(radio_handle);
        } while (BluetoothFindNextRadio(find_radio, &radio_handle));

        BluetoothFindRadioClose(find_radio);
        return devices;
    }

    bool DisconnectDevice(const std::wstring& device_path)
    {
        WiimoteDeviceInfo device_info;
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            auto it = m_tracked_devices.find(device_path);
            if (it == m_tracked_devices.end())
                return false;
            device_info = it->second;
            m_tracked_devices.erase(it);
        }
        
        // Actually disconnect by disabling HID service via Bluetooth API
        if (device_info.has_bt_address)
        {
            constexpr BLUETOOTH_FIND_RADIO_PARAMS radio_params{ .dwSize = sizeof(radio_params) };
            HANDLE radio_handle{};
            auto find_radio = BluetoothFindFirstRadio(&radio_params, &radio_handle);
            if (find_radio)
            {
                do {
                    BLUETOOTH_DEVICE_INFO btdi{ .dwSize = sizeof(btdi) };
                    btdi.Address = device_info.bt_address;
                    
                    // Disable HID service to disconnect
                    DWORD result = BluetoothSetServiceState(
                        radio_handle, &btdi,
                        const_cast<GUID*>(&HumanInterfaceDeviceServiceClass_UUID),
                        BLUETOOTH_SERVICE_DISABLE);
                    
                    CloseHandle(radio_handle);
                    if (result == ERROR_SUCCESS)
                    {
                        LOG_INFO("Disconnected Wiimote via Bluetooth API");
                        BluetoothFindRadioClose(find_radio);
                        return true;
                    }
                } while (BluetoothFindNextRadio(find_radio, &radio_handle));
                BluetoothFindRadioClose(find_radio);
            }
        }
        
        LOG_INFO("Removed Wiimote from tracking (BT disconnect may not be complete)");
        return true;
    }

    bool DisconnectDeviceByAddress(const BLUETOOTH_ADDRESS& bt_addr)
    {
        constexpr BLUETOOTH_FIND_RADIO_PARAMS radio_params{ .dwSize = sizeof(radio_params) };
        HANDLE radio_handle{};
        auto find_radio = BluetoothFindFirstRadio(&radio_params, &radio_handle);
        if (!find_radio)
            return false;

        bool success = false;
        do {
            BLUETOOTH_DEVICE_INFO btdi{ .dwSize = sizeof(btdi) };
            btdi.Address = bt_addr;

            DWORD result = BluetoothSetServiceState(
                radio_handle, &btdi,
                const_cast<GUID*>(&HumanInterfaceDeviceServiceClass_UUID),
                BLUETOOTH_SERVICE_DISABLE);

            CloseHandle(radio_handle);
            if (result == ERROR_SUCCESS)
            {
                success = true;
                break;
            }
        } while (BluetoothFindNextRadio(find_radio, &radio_handle));

        BluetoothFindRadioClose(find_radio);
        return success;
    }

    bool ForgetDevice(const BLUETOOTH_ADDRESS& bt_addr)
    {
        // First disconnect if connected
        {
            std::lock_guard<std::mutex> lock(m_devices_mutex);
            for (auto it = m_tracked_devices.begin(); it != m_tracked_devices.end(); ++it)
            {
                if (it->second.has_bt_address && 
                    memcmp(&it->second.bt_address, &bt_addr, sizeof(BLUETOOTH_ADDRESS)) == 0)
                {
                    m_tracked_devices.erase(it);
                    break;
                }
            }
        }
        
        // Remove from Bluetooth pairing
        DWORD result = BluetoothRemoveDevice(&bt_addr);
        if (result == ERROR_SUCCESS)
        {
            LOG_INFO("Forgot Wiimote device");
            return true;
        }
        else
        {
            LOG_ERROR("Failed to forget Wiimote device, error: " + std::to_string(result));
            return false;
        }
    }

    // Find Bluetooth address for a device by scanning paired devices
    bool FindBluetoothAddressForDevice(const std::wstring& device_path, BLUETOOTH_ADDRESS* out_addr)
    {
        // Extract device instance ID from HID path to match against BT devices
        constexpr BLUETOOTH_FIND_RADIO_PARAMS radio_params{ .dwSize = sizeof(radio_params) };
        HANDLE radio_handle{};
        auto find_radio = BluetoothFindFirstRadio(&radio_params, &radio_handle);
        if (!find_radio)
            return false;

        bool found = false;
        do {
            BLUETOOTH_DEVICE_SEARCH_PARAMS search_params{
                .dwSize = sizeof(search_params),
                .fReturnAuthenticated = true,
                .fReturnRemembered = true,
                .fReturnUnknown = false,
                .fReturnConnected = true,
                .fIssueInquiry = false,
                .hRadio = radio_handle,
            };

            BLUETOOTH_DEVICE_INFO btdi{ .dwSize = sizeof(btdi) };
            auto find_device = BluetoothFindFirstDevice(&search_params, &btdi);
            if (find_device)
            {
                do {
                    std::wstring name(btdi.szName);
                    // Check if this is a connected Wiimote
                    if (btdi.fConnected && 
                        (name.find(L"Nintendo RVL-CNT") == 0 || name.find(L"Nintendo RVL-WBC") == 0))
                    {
                        *out_addr = btdi.Address;
                        found = true;
                        break;
                    }
                } while (BluetoothFindNextDevice(find_device, &btdi));
                BluetoothFindDeviceClose(find_device);
            }
            CloseHandle(radio_handle);
            if (found) break;
        } while (BluetoothFindNextRadio(find_radio, &radio_handle));
        BluetoothFindRadioClose(find_radio);
        
        return found;
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

    // Get actual Bluetooth device name for a Wiimote
    std::wstring GetBluetoothDeviceName(const std::wstring& device_path, USHORT productId)
    {
        // Default fallback names
        std::wstring defaultName = (productId == 0x0330) ? L"Wii Remote Plus" : L"Wii Remote";
        
        // Scan connected Bluetooth devices to find matching Wiimote name
        constexpr BLUETOOTH_FIND_RADIO_PARAMS radio_params{ .dwSize = sizeof(radio_params) };
        HANDLE radio_handle{};
        auto find_radio = BluetoothFindFirstRadio(&radio_params, &radio_handle);
        if (!find_radio)
            return defaultName;

        std::wstring foundName;
        do {
            BLUETOOTH_DEVICE_SEARCH_PARAMS search_params{
                .dwSize = sizeof(search_params),
                .fReturnAuthenticated = true,
                .fReturnRemembered = true,
                .fReturnUnknown = false,
                .fReturnConnected = true,
                .fIssueInquiry = false,
                .hRadio = radio_handle,
            };

            BLUETOOTH_DEVICE_INFO btdi{ .dwSize = sizeof(btdi) };
            auto find_device = BluetoothFindFirstDevice(&search_params, &btdi);
            if (find_device)
            {
                do {
                    std::wstring name(btdi.szName);
                    // Check if this is a connected Wiimote - return actual BT name
                    if (btdi.fConnected && 
                        (name.find(L"Nintendo RVL-CNT") == 0 || name.find(L"Nintendo RVL-WBC") == 0))
                    {
                        foundName = name;
                        break;
                    }
                } while (BluetoothFindNextDevice(find_device, &btdi));
                BluetoothFindDeviceClose(find_device);
            }
            CloseHandle(radio_handle);
            if (!foundName.empty()) break;
        } while (BluetoothFindNextRadio(find_radio, &radio_handle));
        BluetoothFindRadioClose(find_radio);
        
        return foundName.empty() ? defaultName : foundName;
    }

    // Find Bluetooth address by device name
    bool FindBluetoothAddressForDeviceByName(const std::wstring& device_name, BLUETOOTH_ADDRESS* out_addr)
    {
        constexpr BLUETOOTH_FIND_RADIO_PARAMS radio_params{ .dwSize = sizeof(radio_params) };
        HANDLE radio_handle{};
        auto find_radio = BluetoothFindFirstRadio(&radio_params, &radio_handle);
        if (!find_radio)
            return false;

        bool found = false;
        do {
            BLUETOOTH_DEVICE_SEARCH_PARAMS search_params{
                .dwSize = sizeof(search_params),
                .fReturnAuthenticated = true,
                .fReturnRemembered = true,
                .fReturnUnknown = false,
                .fReturnConnected = true,
                .fIssueInquiry = false,
                .hRadio = radio_handle,
            };

            BLUETOOTH_DEVICE_INFO btdi{ .dwSize = sizeof(btdi) };
            auto find_device = BluetoothFindFirstDevice(&search_params, &btdi);
            if (find_device)
            {
                do {
                    std::wstring name(btdi.szName);
                    // Match by name
                    if (name == device_name)
                    {
                        *out_addr = btdi.Address;
                        found = true;
                        break;
                    }
                } while (BluetoothFindNextDevice(find_device, &btdi));
                BluetoothFindDeviceClose(find_device);
            }
            CloseHandle(radio_handle);
            if (found) break;
        } while (BluetoothFindNextRadio(find_radio, &radio_handle));
        BluetoothFindRadioClose(find_radio);
        
        return found;
    }

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
                            
                            // Try to get actual Bluetooth device name
                            std::wstring bt_name = GetBluetoothDeviceName(devicePath, attributes.ProductID);
                            info.device_name = bt_name;
                            
                            // Try to find and store the BT address
                            BLUETOOTH_ADDRESS addr;
                            if (FindBluetoothAddressForDeviceByName(bt_name, &addr))
                            {
                                info.bt_address = addr;
                                info.has_bt_address = true;
                            }
                            else
                            {
                                ZeroMemory(&info.bt_address, sizeof(BLUETOOTH_ADDRESS));
                                info.has_bt_address = false;
                            }
                            
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
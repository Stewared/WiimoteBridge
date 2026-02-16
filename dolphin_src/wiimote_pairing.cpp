// Adapted from Dolphin Emulator (https://github.com/dolphin-emu/dolphin)
// Licensed under GPL 2.0 or later
// This derivative work is licensed under GPL 3.0

#include "wiimote_pairing.h"
#include "debug_log.h"
#include "system_tray.h"
#include "wiimote_led_setter.h"
#include <Windows.h>
#include <BluetoothAPIs.h>
#include <Setupapi.h>
#include <devpkey.h>
#include <cfgmgr32.h>
#include <sstream>
#include <array>
#include <thread>
#include <atomic>
#include <functional>

#pragma comment(lib, "Bthprops.lib")
#pragma comment(lib, "SetupAPI.lib")
#pragma comment(lib, "Cfgmgr32.lib")

// BluetoothAuthenticateDevice is marked as deprecated, but BluetoothAuthenticateDeviceEx
// requires additional setup. Suppress the deprecation warning.
#pragma warning(push)
#pragma warning(disable : 4995)

// Constants from Dolphin
constexpr int DEFAULT_INQUIRY_LENGTH = 3;  // ~3.84 seconds per inquiry
constexpr int ITERATION_COUNT = 3;         // Number of scan iterations like Dolphin

// Helper function to convert wide string to narrow string for logging
static std::string WideToNarrow(const std::wstring& wide)
{
    if (wide.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &result[0], size, nullptr, nullptr);
    return result;
}

// Device name validation functions from Dolphin WiimoteReal.cpp
static bool IsWiimoteName(const std::wstring& name)
{
    // Wii software similarly checks just the start of the name.
    // Nintendo RVL-CNT-01 = standard Wii Remote
    // Nintendo RVL-CNT-01-TR = Wii Remote Plus (TR = with Motion Plus inside)
    // Nintendo RVL-CNT-01-UC = Wii U Pro Controller
    return name.find(L"Nintendo RVL-CNT") == 0;
}

static bool IsBalanceBoardName(const std::wstring& name)
{
    // Nintendo RVL-WBC-01 = Wii Balance Board
    return name.find(L"Nintendo RVL-WBC") == 0;
}

static bool IsValidWiimoteDevice(const std::wstring& name)
{
    return IsWiimoteName(name) || IsBalanceBoardName(name);
}

// Authentication method enum (from Dolphin)
enum class AuthenticationMethod
{
    OneTwo,     // 1+2 buttons - uses device's own address
    SyncButton  // Sync button - uses host's address (preferred, allows reconnection with any button)
};

WiimotePairingHandler::WiimotePairingHandler()
    : m_is_pairing(false), m_should_stop(false), m_last_status("Not initialized")
{
}

WiimotePairingHandler::~WiimotePairingHandler()
{
    StopPairing();
    if (m_pairing_thread_handle.joinable())
    {
        m_pairing_thread_handle.join();
    }
}

bool WiimotePairingHandler::Initialize()
{
    LOG_INFO("WiimotePairingHandler initialized");
    m_last_status = "Initialized and ready";
    return true;
}

bool WiimotePairingHandler::StartPairing()
{
    if (m_is_pairing)
    {
        LOG_INFO("Already pairing, ignoring start request");
        m_last_status = "Already pairing";
        return false;
    }

    m_is_pairing = true;
    m_should_stop = false;
    m_last_status = "Pairing mode enabled - press sync button on Wii Remote";
    LOG_INFO("Starting pairing mode");

    // Start pairing in a background thread
    if (m_pairing_thread_handle.joinable())
    {
        m_pairing_thread_handle.join();
    }
    
    m_pairing_thread_handle = std::thread([this]() {
        PairingThreadProc();
    });

    return true;
}

bool WiimotePairingHandler::StopPairing()
{
    if (!m_is_pairing)
    {
        return false;
    }

    LOG_INFO("Stopping pairing mode");
    m_should_stop = true;
    m_is_pairing = false;
    m_last_status = "Pairing mode disabled";
    
    return true;
}

std::string WiimotePairingHandler::GetStatusMessage()
{
    std::lock_guard<std::mutex> lock(m_status_mutex);
    return m_last_status;
}

void WiimotePairingHandler::SetStatus(const std::string& status)
{
    std::lock_guard<std::mutex> lock(m_status_mutex);
    m_last_status = status;
    LOG_INFO(status);
}

void WiimotePairingHandler::PairingThreadProc()
{
    LOG_INFO("Pairing thread started");
    
    while (m_is_pairing && !m_should_stop)
    {
        try
        {
            // Step 1: Remove unusable (remembered but not authenticated) devices
            // This is critical - Windows keeps disconnected remotes around but can't reconnect them
            SetStatus("Removing unusable remembered devices...");
            int removed = RemoveUnusableWiimoteDevices();
            if (removed > 0)
            {
                LOG_INFO(LogFormat("Removed %d unusable device(s)", removed));
            }

            // Step 2: Discover and pair Wiimotes (like Dolphin's FindAndAuthenticateWiimotes)
            // Do multiple iterations to improve success rate
            int total_paired = 0;
            for (int iteration = 0; iteration < ITERATION_COUNT && m_is_pairing && !m_should_stop; ++iteration)
            {
                SetStatus(LogFormat("Scanning for Wii Remotes (attempt %d/%d)...", iteration + 1, ITERATION_COUNT));
                int paired = DiscoverAndPairWiimotes(DEFAULT_INQUIRY_LENGTH, AuthenticationMethod::SyncButton);
                total_paired += paired;
                
                if (paired > 0)
                {
                    SetStatus(LogFormat("Paired %d Wii Remote(s) this iteration", paired));
                }
            }

            if (total_paired > 0)
            {
                SetStatus(LogFormat("Successfully paired %d Wii Remote(s)", total_paired));
            }
            else
            {
                SetStatus("No Wii Remotes found - press sync button on controller");
            }

            // Brief pause before next scan cycle
            for (int i = 0; i < 10 && m_is_pairing && !m_should_stop; ++i)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        catch (const std::exception& e)
        {
            SetStatus(std::string("Pairing error: ") + e.what());
            LOG_ERROR(std::string("Exception in pairing thread: ") + e.what());
        }
    }

    LOG_INFO("Pairing thread stopped");
    m_is_pairing = false;
}

int WiimotePairingHandler::DiscoverAndPairWiimotes(int inquiry_length, AuthenticationMethod auth_method)
{
    int success_count = 0;

    // Enumerate Bluetooth radios
    constexpr BLUETOOTH_FIND_RADIO_PARAMS radio_params{
        .dwSize = sizeof(radio_params),
    };

    HANDLE radio_handle{};
    const auto find_radio = BluetoothFindFirstRadio(&radio_params, &radio_handle);
    if (find_radio == nullptr)
    {
        DWORD error = GetLastError();
        LOG_ERROR(LogFormat("BluetoothFindFirstRadio failed with error %lu", error));
        return 0;
    }

    do
    {
        if (m_should_stop) break;

        BLUETOOTH_RADIO_INFO radio_info{.dwSize = sizeof(radio_info)};
        if (BluetoothGetRadioInfo(radio_handle, &radio_info) != ERROR_SUCCESS)
        {
            LOG_ERROR("BluetoothGetRadioInfo failed");
            CloseHandle(radio_handle);
            continue;
        }

        LOG_DEBUG(LogFormat("Using Bluetooth radio: %s", WideToNarrow(radio_info.szName).c_str()));

        // Search for Bluetooth devices
        BLUETOOTH_DEVICE_SEARCH_PARAMS search_params{
            .dwSize = sizeof(search_params),
            .fReturnAuthenticated = true,
            .fReturnRemembered = true,
            .fReturnUnknown = true,
            .fReturnConnected = true,
            .fIssueInquiry = inquiry_length > 0,
            .cTimeoutMultiplier = static_cast<UCHAR>(inquiry_length),
            .hRadio = radio_handle,
        };

        BLUETOOTH_DEVICE_INFO btdi{.dwSize = sizeof(btdi)};
        const auto find_device = BluetoothFindFirstDevice(&search_params, &btdi);
        if (find_device == nullptr)
        {
            DWORD error = GetLastError();
            if (error != ERROR_NO_MORE_ITEMS)
            {
                LOG_ERROR(LogFormat("BluetoothFindFirstDevice failed with error %lu", error));
            }
            CloseHandle(radio_handle);
            continue;
        }

        do
        {
            if (m_should_stop) break;

            std::wstring device_name(btdi.szName);
            
            // Check if this is a Wiimote device (using Dolphin's name matching)
            if (!IsValidWiimoteDevice(device_name))
            {
                continue;
            }

            LOG_INFO(LogFormat("Found Wiimote device: %s", WideToNarrow(device_name).c_str()));
            LOG_DEBUG(LogFormat("  Connected: %s, Authenticated: %s, Remembered: %s",
                btdi.fConnected ? "yes" : "no", 
                btdi.fAuthenticated ? "yes" : "no", 
                btdi.fRemembered ? "yes" : "no"));

            // Skip already connected remotes
            if (btdi.fConnected)
            {
                LOG_DEBUG("  Device already connected, skipping");
                continue;
            }

            // Already-paired devices can still need an explicit HID service enable
            // to reconnect reliably on Windows.
            if (btdi.fAuthenticated && btdi.fRemembered)
            {
                LOG_DEBUG("  Device already paired (authenticated + remembered), attempting HID reconnect");
            }

            // Authenticate if needed
            if (!btdi.fAuthenticated)
            {
                LOG_INFO("  Attempting to authenticate device...");
                if (AuthenticateWiimote(radio_handle, radio_info, &btdi, auth_method))
                {
                    LOG_INFO("  Authentication successful");
                }
                else
                {
                    LOG_ERROR("  Authentication failed");
                    continue;
                }
            }

            // Enable HID service to connect the device
            LOG_INFO("  Enabling HID service...");
            const DWORD service_result = BluetoothSetServiceState(
                radio_handle, &btdi,
                const_cast<GUID*>(&HumanInterfaceDeviceServiceClass_UUID),
                BLUETOOTH_SERVICE_ENABLE);

            if (service_result == ERROR_SUCCESS)
            {
                ++success_count;
                LOG_NOTICE(LogFormat("Successfully paired and connected: %s", WideToNarrow(device_name).c_str()));
                
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
                WiimoteLedSetter::Instance().SetLedsOnAllWiimotes();
                
                SystemTray* tray = SystemTray::GetInstance();
                if (tray && tray->GetHwnd())
                {
                    wchar_t* nameCopy = new wchar_t[device_name.length() + 1];
                    wcscpy_s(nameCopy, device_name.length() + 1, device_name.c_str());
                    PostMessage(tray->GetHwnd(), WM_WIIMOTE_CONNECTED, 
                               reinterpret_cast<WPARAM>(nameCopy), 0);
                }
            }
            else
            {
                // FYI: Tends to fail with ERROR_INVALID_PARAMETER
                LOG_ERROR(LogFormat("BluetoothSetServiceState failed with error %lu", service_result));

                // Some remembered/authenticated entries are stale on Windows and cannot
                // be reconnected via service enable. Remove them so the next scan can
                // perform a clean authentication flow.
                if (service_result == ERROR_INVALID_PARAMETER && btdi.fRemembered &&
                    btdi.fAuthenticated && !btdi.fConnected)
                {
                    LOG_NOTICE("  Stale remembered device detected, removing for clean re-pair");
                    const DWORD remove_result = BluetoothRemoveDevice(&btdi.Address);
                    if (remove_result == ERROR_SUCCESS)
                    {
                        LOG_NOTICE("  Removed stale remembered device");
                    }
                    else
                    {
                        LOG_ERROR(LogFormat("  Failed to remove stale remembered device: %lu", remove_result));
                    }
                }
            }

        } while (BluetoothFindNextDevice(find_device, &btdi) && !m_should_stop);

        BluetoothFindDeviceClose(find_device);
        CloseHandle(radio_handle);

    } while (BluetoothFindNextRadio(find_radio, &radio_handle) && !m_should_stop);

    BluetoothFindRadioClose(find_radio);

    return success_count;
}

bool WiimotePairingHandler::AuthenticateWiimote(HANDLE radio_handle,
    const BLUETOOTH_RADIO_INFO& radio_info, BLUETOOTH_DEVICE_INFO* btdi,
    AuthenticationMethod auth_method)
{
    // Adapted from Dolphin's AuthenticateWiimote function
    // When pressing the sync button, the remote expects the host's address as the pass key.
    // When pressing 1+2 it expects its own address.
    // The sync button method is preferred as it allows reconnection with any button press.
    
    const auto& bdaddr_to_use = (auth_method == AuthenticationMethod::SyncButton) 
        ? radio_info.address 
        : btdi->Address;

    // The Bluetooth device address is stored in the typical order (reverse of display order).
    // Pass the 6 bytes of the address directly as the pass key.
    std::array<WCHAR, 6> pass_key;
    for (size_t i = 0; i < 6; ++i)
    {
        pass_key[i] = static_cast<WCHAR>(bdaddr_to_use.rgBytes[i]);
    }

    LOG_DEBUG(LogFormat("Using %s address for authentication",
        (auth_method == AuthenticationMethod::SyncButton) ? "host" : "device"));

    const DWORD auth_result = BluetoothAuthenticateDevice(
        nullptr, radio_handle, btdi, pass_key.data(), static_cast<ULONG>(pass_key.size()));

    if (auth_result != ERROR_SUCCESS)
    {
        // Common errors: ERROR_NO_MORE_ITEMS or ERROR_GEN_FAILURE
        LOG_ERROR(LogFormat("BluetoothAuthenticateDevice failed with error %lu", auth_result));
        return false;
    }

    LOG_DEBUG("BluetoothAuthenticateDevice succeeded");

    // Must enumerate installed services to make the remote remember the pairing
    DWORD pc_services = 0;
    const DWORD services_result = BluetoothEnumerateInstalledServices(
        radio_handle, btdi, &pc_services, nullptr);

    if (services_result != ERROR_SUCCESS && services_result != ERROR_MORE_DATA)
    {
        LOG_ERROR(LogFormat("BluetoothEnumerateInstalledServices failed with error %lu", services_result));
        return false;
    }

    LOG_DEBUG(LogFormat("Device has %lu installed services", pc_services));
    return true;
}

int WiimotePairingHandler::RemoveUnusableWiimoteDevices()
{
    // Adapted from Dolphin's RemoveUnusableWiimoteBluetoothDevices
    // Windows is problematic with remembering disconnected Wii remotes.
    // If they are authenticated, the remote can reestablish the connection with any button.
    // If they are *not* authenticated there's apparently no feasible way to reconnect them.
    // We remove these problematic remembered devices so we can reconnect them.
    
    constexpr BLUETOOTH_FIND_RADIO_PARAMS radio_params{
        .dwSize = sizeof(radio_params),
    };

    HANDLE radio_handle{};
    const auto find_radio = BluetoothFindFirstRadio(&radio_params, &radio_handle);
    if (find_radio == nullptr)
    {
        return 0;
    }

    int removed_count = 0;
    do
    {
        // Search for remembered (but not connected and not authenticated) devices only
        BLUETOOTH_DEVICE_SEARCH_PARAMS search_params{
            .dwSize = sizeof(search_params),
            .fReturnAuthenticated = true,
            .fReturnRemembered = true,
            .fReturnUnknown = false,
            .fReturnConnected = false,
            .fIssueInquiry = false,  // Don't need inquiry for remembered devices
            .hRadio = radio_handle,
        };

        BLUETOOTH_DEVICE_INFO btdi{.dwSize = sizeof(btdi)};
        const auto find_device = BluetoothFindFirstDevice(&search_params, &btdi);
        if (find_device == nullptr)
        {
            CloseHandle(radio_handle);
            continue;
        }

        do
        {
            std::wstring device_name(btdi.szName);
            
            // Check if it's a Wiimote and if it's unusable (remembered but not authenticated)
            if (IsValidWiimoteDevice(device_name) &&
                btdi.fRemembered && !btdi.fConnected && !btdi.fAuthenticated)
            {
                LOG_INFO(LogFormat("Removing unusable device: %s (remembered but not authenticated)",
                    WideToNarrow(device_name).c_str()));
                    
                if (BluetoothRemoveDevice(&btdi.Address) == ERROR_SUCCESS)
                {
                    removed_count++;
                    LOG_NOTICE("Device removed successfully");
                }
                else
                {
                    LOG_ERROR("Failed to remove device");
                }
            }
        } while (BluetoothFindNextDevice(find_device, &btdi));

        BluetoothFindDeviceClose(find_device);
        CloseHandle(radio_handle);

    } while (BluetoothFindNextRadio(find_radio, &radio_handle));

    BluetoothFindRadioClose(find_radio);
    return removed_count;
}

#pragma warning(pop)

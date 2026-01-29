#pragma once

#include <string>
#include <windows.h>
#include <BluetoothAPIs.h>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

enum class AuthenticationMethod;

class WiimotePairingHandler
{
public:
    WiimotePairingHandler();
    ~WiimotePairingHandler();

    bool Initialize();
    bool StartPairing();
    bool StopPairing();
    std::string GetStatusMessage();

private:
    std::thread m_pairing_thread_handle;
    std::atomic<bool> m_is_pairing;
    std::atomic<bool> m_should_stop;
    std::string m_last_status;
    std::mutex m_status_mutex;

    // Main pairing thread function
    void PairingThreadProc();
    
    // Set status with thread safety
    void SetStatus(const std::string& status);

    // Bluetooth operations (adapted from Dolphin IOWin.cpp)
    int DiscoverAndPairWiimotes(int inquiry_length, AuthenticationMethod auth_method);
    bool AuthenticateWiimote(HANDLE radio_handle, const BLUETOOTH_RADIO_INFO& radio_info,
                             BLUETOOTH_DEVICE_INFO* btdi, AuthenticationMethod auth_method);
    int RemoveUnusableWiimoteDevices();
};

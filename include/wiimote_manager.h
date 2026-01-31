#pragma once

#include <string>
#include <chrono>
#include <memory>
#include <thread>
#include "wiimote_pairing.h"

class WiimoteManager
{
public:
    WiimoteManager();
    ~WiimoteManager();

    bool StartPairing();
    bool StartPairingForOneMinute();
    bool StopPairing();
    bool GetStatus(std::string& status_out);

    void Tick();

private:
    std::unique_ptr<WiimotePairingHandler> m_pairing_handler;
    std::chrono::steady_clock::time_point m_pairing_start_time;
    std::chrono::steady_clock::time_point m_last_detection_check;
    bool m_one_minute_mode;
    bool m_is_pairing;

    bool EndPairing();
    void CheckForPrePairedDevices();
};
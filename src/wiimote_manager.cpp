#include "wiimote_manager.h"
#include "wiimote_led_setter.h"
#include "debug_log.h"

WiimoteManager::WiimoteManager()
    : m_one_minute_mode(false), m_is_pairing(false)
{
    m_pairing_handler = std::make_unique<WiimotePairingHandler>();
    m_pairing_handler->Initialize();
    m_last_detection_check = std::chrono::steady_clock::now();
    
    WiimoteLedSetter::Instance().StartBlinking();
    
    CheckForPrePairedDevices();
    
    LOG_INFO("WiimoteManager created");
}

WiimoteManager::~WiimoteManager()
{
    if (m_is_pairing)
    {
        EndPairing();
    }
    WiimoteLedSetter::Instance().StopBlinking();
    LOG_INFO("WiimoteManager destroyed");
}

bool WiimoteManager::StartPairing()
{
    if (m_is_pairing)
    {
        LOG_INFO("StartPairing called but already pairing");
        return false;
    }

    LOG_INFO("Starting continuous pairing mode");
    m_one_minute_mode = false;
    m_is_pairing = true;
    return m_pairing_handler->StartPairing();
}

bool WiimoteManager::StartPairingForOneMinute()
{
    if (m_is_pairing)
    {
        LOG_INFO("StartPairingForOneMinute called but already pairing");
        return false;
    }

    LOG_INFO("Starting 1-minute pairing mode");
    m_one_minute_mode = true;
    m_is_pairing = true;
    m_pairing_start_time = std::chrono::steady_clock::now();
    return m_pairing_handler->StartPairing();
}

bool WiimoteManager::StopPairing()
{
    if (!m_is_pairing)
    {
        LOG_DEBUG("StopPairing called but not currently pairing");
        return false;
    }

    LOG_INFO("Stopping pairing mode");
    return EndPairing();
}

bool WiimoteManager::GetStatus(std::string& status_out)
{
    status_out = m_pairing_handler->GetStatusMessage();
    return true;
}

void WiimoteManager::Tick()
{
    if (m_is_pairing && m_one_minute_mode)
    {
        auto elapsed = std::chrono::steady_clock::now() - m_pairing_start_time;
        if (elapsed >= std::chrono::minutes(1))
        {
            LOG_INFO("One-minute pairing timeout reached");
            EndPairing();
        }
    }

    auto now = std::chrono::steady_clock::now();
    if (now - m_last_detection_check >= std::chrono::seconds(5))
    {
        CheckForPrePairedDevices();
        m_last_detection_check = now;
    }
}

bool WiimoteManager::EndPairing()
{
    LOG_INFO("EndPairing called");
    m_is_pairing = false;
    m_one_minute_mode = false;
    return m_pairing_handler->StopPairing();
}

void WiimoteManager::CheckForPrePairedDevices()
{
    int detected = WiimoteLedSetter::Instance().DetectAndRegisterNewWiimotes();
    if (detected > 0)
    {
        LOG_NOTICE(LogFormat("Detected %d pre-paired Wiimote(s), LED animation started", detected));
    }
}
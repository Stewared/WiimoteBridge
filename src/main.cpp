#include "system_tray.h"
#include "wiimote_manager.h"
#include "registry_utils.h"
#include "debug_log.h"
#include <windows.h>
#include <memory>
#include <thread>
#include <chrono>

class Application
{
public:
    Application() : m_tray(nullptr), m_wiimote_mgr(nullptr), m_running(false) {}

    bool Initialize(HINSTANCE hInstance)
    {
        LOG_INFO("WiimoteBridge application starting");
        
        m_tray = std::make_unique<SystemTray>();
        if (!m_tray->Initialize(hInstance))
        {
            LOG_ERROR("Failed to initialize system tray");
            return false;
        }

        m_wiimote_mgr = std::make_unique<WiimoteManager>();
        
        // Connect the manager to the tray for callbacks
        m_tray->SetWiimoteManager(m_wiimote_mgr.get());

        // Register for auto-start
        RegistryUtils::SetAutoStartEnabled(true);

        m_running = true;
        LOG_INFO("WiimoteBridge initialized successfully");
        return true;
    }

    void Run()
    {
        MSG msg = {};

        while (m_running && GetMessage(&msg, nullptr, 0, 0) > 0)
        {
            // Tick the wiimote manager (handles 1-minute timeouts)
            m_wiimote_mgr->Tick();

            TranslateMessage(&msg);
            DispatchMessage(&msg);

            // Small sleep to prevent CPU spinning
            Sleep(100);
        }
        
        LOG_INFO("WiimoteBridge application exiting");
    }

    void Stop()
    {
        m_running = false;
    }

private:
    std::unique_ptr<SystemTray> m_tray;
    std::unique_ptr<WiimoteManager> m_wiimote_mgr;
    bool m_running;
};

static std::unique_ptr<Application> g_app;

int WINAPI WinMain(_In_ HINSTANCE hInstance,
                   _In_opt_ HINSTANCE hPrevInstance,
                   _In_ LPSTR lpCmdLine,
                   _In_ int nShowCmd)
{
    g_app = std::make_unique<Application>();

    if (!g_app->Initialize(hInstance))
    {
        MessageBoxA(nullptr,
                    "Failed to initialize WiimoteBridge",
                    "Error",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    g_app->Run();

    // Cleanup auto-start on exit (optional - comment out if you want to keep it running)
    // RegistryUtils::SetAutoStartEnabled(false);

    return 0;
}

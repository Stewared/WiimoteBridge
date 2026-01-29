#pragma once

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <string>
#include <vector>
#include "debug_log.h"

#pragma comment(lib, "Hid.lib")

// Helper class to set Wii Remote LEDs after pairing
// This makes the Wiimote stop flashing by assigning it a "player number"

class WiimoteLedSetter
{
public:
    // Set LEDs on all connected Wiimotes
    // ledMask: bit 0 = LED1, bit 1 = LED2, bit 2 = LED3, bit 3 = LED4
    static int SetLedsOnAllWiimotes(int ledMask = 0x01)
    {
        LOG_INFO("Setting LEDs on all connected Wiimotes");
        
        // Get the HID device interface GUID
        GUID hidGuid;
        HidD_GetHidGuid(&hidGuid);

        // Get list of HID devices
        HDEVINFO deviceInfoSet = SetupDiGetClassDevs(
            &hidGuid, nullptr, nullptr, 
            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

        if (deviceInfoSet == INVALID_HANDLE_VALUE)
        {
            LOG_ERROR("SetupDiGetClassDevs failed");
            return 0;
        }

        int setCount = 0;
        SP_DEVICE_INTERFACE_DATA deviceInterfaceData;
        deviceInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(
            deviceInfoSet, nullptr, &hidGuid, i, &deviceInterfaceData); ++i)
        {
            // Get required size for device detail
            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetailW(
                deviceInfoSet, &deviceInterfaceData, 
                nullptr, 0, &requiredSize, nullptr);

            if (requiredSize == 0)
                continue;

            // Allocate and get device detail
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

            // Try to open the device
            HANDLE deviceHandle = CreateFileW(
                detailData->DevicePath,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, 0, nullptr);

            if (deviceHandle == INVALID_HANDLE_VALUE)
                continue;

            // Get device attributes
            HIDD_ATTRIBUTES attributes;
            attributes.Size = sizeof(HIDD_ATTRIBUTES);
            if (HidD_GetAttributes(deviceHandle, &attributes))
            {
                // Check if it's a Wiimote (Nintendo VID 0x057e, PID 0x0306 or 0x0330)
                if (attributes.VendorID == 0x057e && 
                    (attributes.ProductID == 0x0306 || attributes.ProductID == 0x0330))
                {
                    LOG_INFO("Found Wiimote HID device, setting LEDs");
                    
                    // Send LED report
                    // Report format: [report ID, LED flags]
                    // LED flags: bits 4-7 are LED1-LED4, bit 0 is rumble
                    BYTE ledReport[2] = { 0x11, static_cast<BYTE>((ledMask & 0x0F) << 4) };
                    
                    DWORD bytesWritten = 0;
                    if (WriteFile(deviceHandle, ledReport, sizeof(ledReport), &bytesWritten, nullptr))
                    {
                        LOG_NOTICE("Successfully set LEDs on Wiimote");
                        setCount++;
                    }
                    else
                    {
                        LOG_ERROR(LogFormat("Failed to write LED report, error: %lu", GetLastError()));
                    }
                }
            }

            CloseHandle(deviceHandle);
        }

        SetupDiDestroyDeviceInfoList(deviceInfoSet);
        
        LOG_INFO(LogFormat("Set LEDs on %d Wiimote(s)", setCount));
        return setCount;
    }
};

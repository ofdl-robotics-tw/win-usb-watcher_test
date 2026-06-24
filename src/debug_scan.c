/*
 * debug_scan.c
 * Dumps all USB device interface paths found by SetupDiGetClassDevs
 * using GUID_DEVINTERFACE_USB_DEVICE.
 *
 * Compile (MINGW32):
 *   gcc -o debug_scan.exe debug_scan.c -lsetupapi -static-libgcc -static
 *
 * Run while NXT/Arduino is plugged in and check if it appears.
 */

#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    GUID usbGuid;
    HDEVINFO hDevInfo;
    SP_DEVICE_INTERFACE_DATA ifData;
    DWORD i;
    int count = 0;

    /* GUID_DEVINTERFACE_USB_DEVICE */
    usbGuid.Data1    = 0xA5DCBF10;
    usbGuid.Data2    = 0x6530;
    usbGuid.Data3    = 0x11D2;
    usbGuid.Data4[0] = 0x90; usbGuid.Data4[1] = 0x1F;
    usbGuid.Data4[2] = 0x00; usbGuid.Data4[3] = 0xC0;
    usbGuid.Data4[4] = 0x4F; usbGuid.Data4[5] = 0xB9;
    usbGuid.Data4[6] = 0x51; usbGuid.Data4[7] = 0xED;

    printf("Scanning USB device interfaces...\n");
    printf("==================================================\n");

    hDevInfo = SetupDiGetClassDevs(
        &usbGuid, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );

    if (hDevInfo == INVALID_HANDLE_VALUE) {
        printf("SetupDiGetClassDevs failed, error: %lu\n", GetLastError());
        goto done;
    }

    ZeroMemory(&ifData, sizeof(ifData));
    ifData.cbSize = sizeof(ifData);

    for (i = 0;
         SetupDiEnumDeviceInterfaces(hDevInfo, NULL, &usbGuid, i, &ifData);
         i++) {
        char pathbuf[512];
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *pDetail;
        DWORD needed = 0;

        /* Get required size */
        SetupDiGetDeviceInterfaceDetailA(
            hDevInfo, &ifData, NULL, 0, &needed, NULL);

        if (needed == 0 || needed > sizeof(pathbuf)) {
            printf("[%lu] Cannot get detail (needed=%lu)\n", i, needed);
            continue;
        }

        pDetail = (SP_DEVICE_INTERFACE_DETAIL_DATA_A *)pathbuf;
        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (!SetupDiGetDeviceInterfaceDetailA(
                hDevInfo, &ifData, pDetail, needed, NULL, NULL)) {
            printf("[%lu] GetDeviceInterfaceDetail failed, error: %lu\n",
                   i, GetLastError());
            continue;
        }

        printf("[%lu] %s\n", i, pDetail->DevicePath);
        count++;
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);

    printf("==================================================\n");
    printf("Total: %d device(s) found\n", count);

done:
    printf("\nPress Enter to exit...");
    getchar();
    return 0;
}

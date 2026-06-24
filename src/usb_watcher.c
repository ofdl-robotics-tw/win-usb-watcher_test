/*
 * usb_watcher.c
 * Generic USB device attach/detach watcher.
 * Monitors a specific VID+PID and prints "ATTACH" or "DETACH" to stdout.
 *
 * Usage:
 *   usb_watcher.exe <VID> <PID>
 *   usb_watcher.exe 0694 0002        <- LEGO NXT
 *   usb_watcher.exe 2341 0043        <- Arduino Uno
 *   usb_watcher.exe 0403 6015        <- FTDI FT230X
 *
 *   VID and PID are hex, with or without 0x prefix.
 *
 * Output:
 *   ATTACH   <- device plugged in (also printed on startup if already present)
 *   DETACH   <- device unplugged
 *   ERROR:<msg>  <- bad arguments or startup failure
 *
 * Compile (MinGW32, XP compatible):
 *   gcc -o usb_watcher.exe usb_watcher.c -lsetupapi -luser32 \
 *       -mwindows -m32 -march=i486 -static-libgcc -static \
 *       -D_WIN32_WINNT=0x0501
 *
 * Compile (MSVC):
 *   cl usb_watcher.c /link setupapi.lib user32.lib
 *
 * Supports: Windows XP ~ Windows 11 (pure Win32 API, zero polling)
 */

#include <windows.h>
#include <dbt.h>
#include <setupapi.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Target VID/PID - set from command line arguments */
static unsigned int g_vid = 0;
static unsigned int g_pid = 0;

/* GUID_DEVINTERFACE_USB_DEVICE: {A5DCBF10-6530-11D2-901F-00C04FB951ED} */
static const GUID USB_DEVICE_GUID = {
    0xA5DCBF10, 0x6530, 0x11D2,
    { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED }
};

static void init_stdout(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
}

/*
 * Parse VID or PID string (hex, with or without 0x prefix).
 * Returns 1 on success, 0 on failure.
 */
static int parse_hex(const char *s, unsigned int *out) {
    char *end;
    unsigned long val;
    if (!s || !*s) return 0;
    /* Skip optional 0x / 0X prefix */
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    if (!*s) return 0;
    val = strtoul(s, &end, 16);
    if (*end != '\0') return 0;   /* trailing non-hex chars */
    if (val > 0xFFFF) return 0;   /* VID/PID are 16-bit */
    *out = (unsigned int)val;
    return 1;
}

/*
 * Check if a USB device path matches g_vid / g_pid.
 * Path format: \\?\usb#vid_xxxx&pid_xxxx#...
 */
static int path_matches(const char *path) {
    unsigned int vid = 0, pid = 0;
    const char *p;
    char upper[512];

    strncpy(upper, path, sizeof(upper) - 1);
    upper[sizeof(upper) - 1] = '\0';
    CharUpperA(upper);

    p = strstr(upper, "VID_");
    if (!p) return 0;
    if (sscanf(p, "VID_%X&PID_%X", &vid, &pid) != 2) return 0;
    return (vid == g_vid && pid == g_pid);
}

/*
 * Check if a WM_DEVICECHANGE broadcast matches g_vid / g_pid.
 */
static int broadcast_matches(DEV_BROADCAST_DEVICEINTERFACE *pDev) {
    char name[512];
#ifdef UNICODE
    WideCharToMultiByte(CP_ACP, 0, pDev->dbcc_name, -1,
                        name, sizeof(name), NULL, NULL);
#else
    strncpy(name, pDev->dbcc_name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
#endif
    return path_matches(name);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg,
                                 WPARAM wParam, LPARAM lParam) {
    if (msg == WM_DEVICECHANGE) {
        DEV_BROADCAST_HDR *pHdr = (DEV_BROADCAST_HDR *)lParam;

        if (wParam == DBT_DEVICEARRIVAL &&
            pHdr && pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            if (broadcast_matches((DEV_BROADCAST_DEVICEINTERFACE *)pHdr))
                printf("ATTACH\n");
        }

        if (wParam == DBT_DEVICEREMOVECOMPLETE &&
            pHdr && pHdr->dbch_devicetype == DBT_DEVTYP_DEVICEINTERFACE) {
            if (broadcast_matches((DEV_BROADCAST_DEVICEINTERFACE *)pHdr))
                printf("DETACH\n");
        }

        return TRUE;
    }

    if (msg == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, msg, wParam, lParam);
}

/*
 * Scan currently connected USB devices and print initial ATTACH/DETACH.
 */
static void initial_scan(void) {
    HDEVINFO hDevInfo;
    SP_DEVICE_INTERFACE_DATA ifData;
    DWORD i;
    int found = 0;

    hDevInfo = SetupDiGetClassDevs(
        &USB_DEVICE_GUID, NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );

    if (hDevInfo == INVALID_HANDLE_VALUE) {
        printf("DETACH\n");
        return;
    }

    ZeroMemory(&ifData, sizeof(ifData));
    ifData.cbSize = sizeof(ifData);

    for (i = 0;
         SetupDiEnumDeviceInterfaces(hDevInfo, NULL,
                                     &USB_DEVICE_GUID, i, &ifData);
         i++) {
        char pathbuf[512];
        SP_DEVICE_INTERFACE_DETAIL_DATA_A *pDetail;
        DWORD needed = 0;

        SetupDiGetDeviceInterfaceDetailA(
            hDevInfo, &ifData, NULL, 0, &needed, NULL);

        if (needed == 0 || needed > sizeof(pathbuf)) continue;

        pDetail = (SP_DEVICE_INTERFACE_DETAIL_DATA_A *)pathbuf;
        pDetail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);

        if (!SetupDiGetDeviceInterfaceDetailA(
                hDevInfo, &ifData, pDetail, needed, NULL, NULL)) continue;

        if (path_matches(pDetail->DevicePath)) {
            found = 1;
            break;
        }
    }

    SetupDiDestroyDeviceInfoList(hDevInfo);
    printf(found ? "ATTACH\n" : "DETACH\n");
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev,
                   LPSTR lpCmd, int nShow) {
    int argc;
    char **argv;
    WNDCLASS wc;
    HWND hWnd;
    MSG msg;
    HDEVNOTIFY hNotify;
    DEV_BROADCAST_DEVICEINTERFACE filter;

    (void)hPrev; (void)lpCmd; (void)nShow;

    init_stdout();

    /* Parse command line via CommandLineToArgvW + convert to ANSI */
    {
        LPWSTR *wargv;
        char buf[64];
        int i;

        wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!wargv) {
            printf("ERROR:CommandLineToArgvW failed\n");
            return 1;
        }

        if (argc < 3) {
            printf("ERROR:Usage: usb_watcher.exe <VID> <PID>  (hex)\n");
            LocalFree(wargv);
            return 1;
        }

        /* Convert wargv[1] and wargv[2] to ANSI */
        for (i = 1; i <= 2; i++) {
            WideCharToMultiByte(CP_ACP, 0, wargv[i], -1,
                                buf, sizeof(buf), NULL, NULL);
            if (i == 1) {
                if (!parse_hex(buf, &g_vid)) {
                    printf("ERROR:Invalid VID: %s\n", buf);
                    LocalFree(wargv);
                    return 1;
                }
            } else {
                if (!parse_hex(buf, &g_pid)) {
                    printf("ERROR:Invalid PID: %s\n", buf);
                    LocalFree(wargv);
                    return 1;
                }
            }
        }

        LocalFree(wargv);
    }

    /* Create hidden message window */
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = TEXT("UsbWatcher");
    RegisterClass(&wc);

    hWnd = CreateWindow(
        TEXT("UsbWatcher"), TEXT(""),
        0, 0, 0, 0, 0,
        HWND_MESSAGE,   /* XP SP3+ */
        NULL, hInst, NULL
    );

    if (!hWnd) {
        /* Fallback for XP SP2 and earlier */
        hWnd = CreateWindow(
            TEXT("UsbWatcher"), TEXT(""),
            WS_OVERLAPPEDWINDOW,
            0, 0, 0, 0,
            NULL, NULL, hInst, NULL
        );
        if (!hWnd) {
            printf("ERROR:CreateWindow failed (%lu)\n", GetLastError());
            return 1;
        }
    }

    /* Register for USB device interface notifications */
    ZeroMemory(&filter, sizeof(filter));
    filter.dbcc_size       = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid  = USB_DEVICE_GUID;

    hNotify = RegisterDeviceNotification(
        hWnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE
    );
    /* hNotify may be NULL on old XP - DBT_DEVNODES_CHANGED still works */

    /* Print initial state */
    initial_scan();

    /* Message loop - zero CPU when idle */
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (hNotify) UnregisterDeviceNotification(hNotify);
    return (int)msg.wParam;
}

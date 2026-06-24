/*
 * test_watcher.c
 * Tests that usb_watcher.exe works correctly.
 *
 * Compile (MINGW32):
 *   gcc -o test_watcher.exe test_watcher.c -static-libgcc -static
 *
 * Usage:
 *   Place test_watcher.exe and usb_watcher.exe in the same folder,
 *   then run test_watcher.exe.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define WATCHER_EXE   "usb_watcher.exe"
#define TIMEOUT_INIT  3000
#define TOTAL_TIME   30000
#define BUF_SIZE      1024

static HANDLE g_hWatcherProc = NULL;

static void sep(void) {
    printf("--------------------------------------------------\n");
}

static BOOL WINAPI CtrlHandler(DWORD type) {
    (void)type;
    if (g_hWatcherProc) {
        TerminateProcess(g_hWatcherProc, 0);
        g_hWatcherProc = NULL;
    }
    return FALSE;
}

/* Known devices reference table */
static void print_known_devices(void) {
    printf("  Known devices (for reference):\n");
    printf("\n");
    printf("  %-24s  VID    PID\n", "Device");
    printf("  %-24s  -----  -----\n", "------------------------");
    printf("  %-24s  0694   0002\n", "LEGO Mindstorms NXT");
    printf("  %-24s  0403   6001\n", "FTDI FT232R");
    printf("  %-24s  0403   6015\n", "FTDI FT230X");
    printf("  %-24s  2341   0043\n", "Arduino Uno (ATmega16U2)");
    printf("  %-24s  2341   0001\n", "Arduino Uno (old)");
    printf("  %-24s  1A86   7523\n", "CH340 (Arduino clone)");
    printf("\n");
}

/* Prompt user for VID and PID, validate hex input */
static int prompt_vid_pid(char *vidStr, char *pidStr, size_t bufLen) {
    unsigned int val;
    char input[64];
    char *end;

    sep();
    print_known_devices();

    /* VID */
    while (1) {
        printf("  Enter VID (hex): ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) return 0;
        /* Strip newline */
        input[strcspn(input, "\r\n")] = '\0';
        /* Strip optional 0x */
        {
            char *s = input;
            if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
            val = (unsigned int)strtoul(s, &end, 16);
            if (*end == '\0' && end != s && val <= 0xFFFF) {
                snprintf(vidStr, bufLen, "%04X", val);
                break;
            }
        }
        printf("  Invalid VID, please enter a 4-digit hex value (e.g. 0694)\n");
    }

    /* PID */
    while (1) {
        printf("  Enter PID (hex): ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin)) return 0;
        input[strcspn(input, "\r\n")] = '\0';
        {
            char *s = input;
            if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
            val = (unsigned int)strtoul(s, &end, 16);
            if (*end == '\0' && end != s && val <= 0xFFFF) {
                snprintf(pidStr, bufLen, "%04X", val);
                break;
            }
        }
        printf("  Invalid PID, please enter a 4-digit hex value (e.g. 0002)\n");
    }

    return 1;
}

int main(void) {
    char vidStr[16], pidStr[16];
    char cmdLine[256];
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa;
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    char buf[BUF_SIZE];
    char line[BUF_SIZE];
    int lineLen = 0;
    DWORD bytesRead;
    DWORD startTick, elapsed;
    BOOL gotInitial = FALSE;
    int attachCount = 0, detachCount = 0;

    printf("\n");
    sep();
    printf("  usb_watcher.exe Test\n");
    sep();

    /* Ask user for VID/PID */
    if (!prompt_vid_pid(vidStr, pidStr, sizeof(vidStr))) {
        printf("[FAIL] Failed to read VID/PID.\n");
        return 1;
    }

    sep();
    printf("\n[TEST 1] Starting %s with VID=%s PID=%s ...\n",
           WATCHER_EXE, vidStr, pidStr);

    /* Build command line: usb_watcher.exe <VID> <PID> */
    snprintf(cmdLine, sizeof(cmdLine), "%s %s %s",
             WATCHER_EXE, vidStr, pidStr);

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    /* Create pipe */
    sa.nLength              = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle       = TRUE;

    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) {
        printf("[FAIL] CreatePipe failed, error: %lu\n", GetLastError());
        return 1;
    }
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    ZeroMemory(&si, sizeof(si));
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = hWrite;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(NULL, cmdLine,
                        NULL, NULL, TRUE,
                        CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        printf("[FAIL] Cannot start %s, error: %lu\n",
               WATCHER_EXE, GetLastError());
        printf("       Make sure %s is in the same folder.\n", WATCHER_EXE);
        CloseHandle(hRead);
        CloseHandle(hWrite);
        return 1;
    }

    CloseHandle(hWrite);
    g_hWatcherProc = pi.hProcess;
    printf("[PASS] %s started, PID=%lu\n\n", WATCHER_EXE, pi.dwProcessId);

    /* TEST 2: wait for initial status */
    printf("[TEST 2] Waiting for initial status (timeout %d s)...\n",
           TIMEOUT_INIT / 1000);

    startTick = GetTickCount();

    while (1) {
        DWORD avail = 0;
        elapsed = GetTickCount() - startTick;

        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            /* Check if watcher printed an ERROR: line */
            printf("[FAIL] %s exited unexpectedly.\n", WATCHER_EXE);
            goto cleanup;
        }

        if (!PeekNamedPipe(hRead, NULL, 0, NULL, &avail, NULL) ||
            avail == 0) {
            if (!gotInitial && elapsed > TIMEOUT_INIT) {
                printf("[FAIL] No initial status within %d s.\n",
                       TIMEOUT_INIT / 1000);
                goto cleanup;
            }
            if (gotInitial && elapsed > TOTAL_TIME) break;
            Sleep(100);
            continue;
        }

        ZeroMemory(buf, sizeof(buf));
        if (!ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, NULL) ||
            bytesRead == 0) break;

        /* Parse lines */
        {
            DWORD i;
            for (i = 0; i < bytesRead; i++) {
                char c = buf[i];
                if (c == '\r') continue;
                if (c == '\n') {
                    line[lineLen] = '\0';
                    lineLen = 0;
                    if (strlen(line) == 0) continue;

                    elapsed = GetTickCount() - startTick;

                    /* ERROR: prefix from usb_watcher */
                    if (strncmp(line, "ERROR:", 6) == 0) {
                        printf("[FAIL] usb_watcher reported: %s\n", line);
                        goto cleanup;
                    }

                    if (strcmp(line, "ATTACH") == 0) {
                        if (!gotInitial) {
                            printf("[PASS] Initial status: ATTACH"
                                   " (device connected), %lu ms\n", elapsed);
                            gotInitial = TRUE;
                            printf("\n");
                            sep();
                            printf("[TEST 3] Listening for %d s.\n"
                                   "         Plug/unplug the device now.\n",
                                   TOTAL_TIME / 1000);
                            sep();
                        } else {
                            attachCount++;
                            printf("  [+%4lu ms] ATTACH  <-- device plugged in\n",
                                   elapsed);
                        }
                    } else if (strcmp(line, "DETACH") == 0) {
                        if (!gotInitial) {
                            printf("[PASS] Initial status: DETACH"
                                   " (device not connected), %lu ms\n", elapsed);
                            gotInitial = TRUE;
                            printf("\n");
                            sep();
                            printf("[TEST 3] Listening for %d s.\n"
                                   "         Plug/unplug the device now.\n",
                                   TOTAL_TIME / 1000);
                            sep();
                        } else {
                            detachCount++;
                            printf("  [+%4lu ms] DETACH  <-- device unplugged\n",
                                   elapsed);
                        }
                    } else {
                        printf("  [+%4lu ms] Unknown output: \"%s\"\n",
                               elapsed, line);
                    }
                } else {
                    if (lineLen < BUF_SIZE - 1)
                        line[lineLen++] = c;
                }
            }
        }

        if (gotInitial &&
            (GetTickCount() - startTick) > (DWORD)TOTAL_TIME) break;
    }

cleanup:
    TerminateProcess(pi.hProcess, 0);
    g_hWatcherProc = NULL;
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hRead);

    printf("\n");
    sep();
    printf("  Summary\n");
    sep();
    printf("  Device         : VID=%s PID=%s\n", vidStr, pidStr);
    printf("  Initial status : %s\n", gotInitial ? "PASS" : "FAIL");
    printf("  ATTACH events  : %d\n", attachCount);
    printf("  DETACH events  : %d\n", detachCount);
    sep();
    printf("\nPress Enter to exit...");
    getchar();
    return gotInitial ? 0 : 1;
}

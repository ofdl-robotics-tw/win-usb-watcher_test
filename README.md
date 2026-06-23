# win-usb-watcher

A lightweight Windows USB device attach/detach monitor designed for inter-process communication.  
Outputs `ATTACH` or `DETACH` to stdout when a specific USB device (identified by VID+PID) is plugged or unplugged.

**Zero polling** — uses `WM_DEVICECHANGE` + `RegisterDeviceNotification`, so CPU usage is effectively 0% while idle.  
**Wide compatibility** — Windows XP SP2 through Windows 11, pure Win32 API, no runtime dependencies.

<img width="953" height="762" alt="image" src="https://github.com/user-attachments/assets/81812ca7-4d46-4ee9-ab67-0d54f12af8ca" />


The reason for this is that I am making a feature for NXT-Scratch that aims to display the connection status of NXT in real time. However, constantly polling the NXT driver or related tools does not feel like a good approach. Therefore, I plan to write a small tool to receive messages from the USB device in the background (interrupt-based, without consuming resources) and provide them to Node JS for reading. Since it requires Windows XP - Windows 11 support, I will implement it with the minimum possible calls to WinAPI.

---

## Similar tools and why this is different

| Tool | Approach | Persistent monitor | XP support | IPC-friendly |
|---|---|---|---|---|
| [usbSearch](https://github.com/todbot/usbSearch) | One-shot scan | ✗ | ✓ | ✗ |
| [USB-device-monitor](https://github.com/BaseMax/USB-device-monitor) | Cross-platform log | ✓ | ✗ | ✗ |
| **usb-watcher** | Event-driven monitor | ✓ | ✓ | ✓ |

The key design goal is to be **spawned as a child process** by a parent application (Node.js, Python, etc.) and communicate via stdout lines — no polling, no library dependencies.

---

## Files

| File | Description |
|---|---|
| `src/usb_watcher.c` | Main watcher — monitors a specific VID+PID, outputs `ATTACH`/`DETACH` to stdout |
| `src/test_watcher.c` | Interactive test tool — prompts for VID/PID and listens for 30 seconds |
| `src/debug_scan.c` | Debug tool — lists all USB device interfaces currently visible to the system |

---

## Usage

```
usb_watcher.exe <VID> <PID>
```

VID and PID are hexadecimal, with or without `0x` prefix:

```
usb_watcher.exe 0694 0002        # LEGO Mindstorms NXT
usb_watcher.exe 2341 0043        # Arduino Uno
usb_watcher.exe 0403 6015        # FTDI FT230X
usb_watcher.exe 0x16C0 0x0483    # Teensy
```

**Output:**
```
ATTACH          <- device currently connected (printed once on startup)
DETACH          <- device not connected (printed once on startup)
ATTACH          <- device plugged in
DETACH          <- device unplugged
ERROR:<message> <- bad arguments or startup failure
```

---

## Integration example (Node.js)

```javascript
var path  = require('path');
var spawn = require('child_process').spawn;

var watcher = spawn('usb_watcher.exe', ['0694', '0002'], {
    windowsHide: true,
    stdio: ['ignore', 'pipe', 'ignore']
});

var buf = '';
watcher.stdout.on('data', function (data) {
    buf += data.toString();
    var lines = buf.split('\n');
    buf = lines.pop();
    lines.forEach(function (line) {
        line = line.trim();
        if (line === 'ATTACH') console.log('NXT connected');
        if (line === 'DETACH') console.log('NXT disconnected');
        if (line.indexOf('ERROR:') === 0) console.error(line);
    });
});

watcher.on('exit', function () {
    // Optionally restart after unexpected exit
    setTimeout(function () { /* restart */ }, 3000);
});

// Kill watcher when parent exits
process.on('exit', function () { watcher.kill(); });
```

---

## How it works

1. **Startup scan** — calls `SetupDiGetClassDevs` with `GUID_DEVINTERFACE_USB_DEVICE` + `DIGCF_PRESENT | DIGCF_DEVICEINTERFACE` to enumerate currently connected USB devices. Prints initial `ATTACH` or `DETACH`.

2. **Event loop** — creates a hidden message-only window and calls `RegisterDeviceNotification` to receive `WM_DEVICECHANGE` messages from the Windows kernel (`usbhub.sys`). No polling; the process sleeps in `GetMessage` between events.

3. **VID/PID matching** — on `DBT_DEVICEARRIVAL` and `DBT_DEVICEREMOVECOMPLETE`, parses the `dbcc_name` field (format: `\\?\usb#vid_xxxx&pid_xxxx#...`) and compares against the requested VID/PID.

`GUID_DEVINTERFACE_USB_DEVICE` (`{A5DCBF10-6530-11D2-901F-00C04FB951ED}`) is registered by `usbhub.sys` for every USB device regardless of its upper-level driver, so this works with any USB device class (Vendor Specific, HID, CDC, etc.).

---

## Building

### Requirements

- MinGW-w64 (i686 toolchain for XP-compatible 32-bit output)
- Or MSVC

### MinGW (recommended, XP compatible)

Install MSYS2, then in a **MSYS2 MINGW32** terminal:

```bash
pacman -S mingw-w64-i686-gcc

# Switch to MINGW32 environment if not already
MSYSTEM=MINGW32 bash --login

# Build usb_watcher.exe
gcc -o usb_watcher.exe src/usb_watcher.c \
    -lsetupapi -luser32 \
    -mwindows -m32 -march=i486 \
    -static-libgcc -static \
    -D_WIN32_WINNT=0x0501

# Build test tools (console, no -mwindows)
gcc -o test_watcher.exe src/test_watcher.c -static-libgcc -static
gcc -o debug_scan.exe   src/debug_scan.c   -lsetupapi -static-libgcc -static
```

### MSVC

```
cl src\usb_watcher.c  /link setupapi.lib user32.lib
cl src\test_watcher.c
cl src\debug_scan.c   /link setupapi.lib
```

### Verify (MinGW)

```bash
# Should only list kernel32.dll, msvcrt.dll, setupapi.dll, user32.dll
objdump -p usb_watcher.exe | grep "DLL Name"
```

All four DLLs are present on Windows XP and later; no external redistribution required.

---

## Windows compatibility

| Windows version | Supported | Notes |
|---|---|---|
| XP SP2 | ✓ | `HWND_MESSAGE` fallback used for older SP |
| XP SP3 | ✓ | |
| Vista / 7 / 8 / 8.1 | ✓ | |
| 10 | ✓ | |
| 11 | ✓ | Tested on 24H2 |

---

## Background: why not libusb or node-usb?

For this specific use case (monitoring a specific device, spawned from NW.js 0.14.7 on Windows XP), both `libusb` and `node-usb` were ruled out:

- `libusb` 1.0.24+ dropped Windows XP support
- `node-usb` requires N-API (Node.js v8.6+); NW.js 0.14.7 uses Node.js v5.x (ABI 47)
- Both require native addon compilation with `nw-gyp`, which is fragile on XP

A small standalone Win32 executable that communicates via stdout is simpler, more portable, and has no runtime dependencies.

---

## License

MIT

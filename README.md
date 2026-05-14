<p align="center">
# Oculus SetUP
</p>

<p align="center">
  <a href="https://github.com/DeziXsteroid/Oculus_Proxy_Helper/releases/latest/download/OculusSetUP-1.1.exe">
    <img alt="Download for Windows" src="https://img.shields.io/badge/Download-Windows%20EXE-ffffff?style=for-the-badge&logo=windows&logoColor=111111&labelColor=111111">
  </a>
  <img alt="Version" src="https://img.shields.io/badge/version-1.1-ffffff?style=for-the-badge&labelColor=111111">
  <img alt="Qt" src="https://img.shields.io/badge/C%2B%2B-Qt%206-ffffff?style=for-the-badge&logo=qt&logoColor=41CD52&labelColor=111111">
</p>

Oculus SetUP is a compact Windows helper for first-time Meta Quest setup, VPN proxy creation, ADB driver recovery, SideQuest performance setup, and QLoader instructions.

## Download

Download the latest one-file build from GitHub Releases:

[**Download OculusSetUP-1.1.exe**](https://github.com/DeziXsteroid/Oculus_Proxy_Helper/releases/latest/download/OculusSetUP-1.1.exe)

The release executable is a single self-extracting file. It includes the Qt runtime and required DLL files, then unpacks them into `%LOCALAPPDATA%\vrpProxy` on first launch.

## Features

- Update by USB guide with the official Meta Quest update page.
- PROXY CREATE screen for using the PC VPN from a headset Wi-Fi proxy.
- HTTP/HTTPS proxy support.
- SOCKS5 CONNECT fallback support for clients that try SOCKS5.
- Firewall-friendly default ports: `8888`, `8080`, `3128`, `8000`, `8118`.
- QLoader guide with certificate instructions.
- Graphic/CPU guide through SideQuest.
- ADB/Driver Helper for missing ADB authorization prompts.

## VPN Proxy Setup

1. Enable a good VPN on the PC.
2. Make sure the VPN uses TUN mode.
3. Open `PROXY CREATE`.
4. Keep `Auto via Windows/VPN` selected unless you know you need a specific adapter.
5. Use port `8888`.
6. Copy the shown Hostname and Port into the headset Wi-Fi proxy settings.

Windows Firewall rule name used by the project:

```powershell
vrpProxy inbound proxy ports
```

Allowed TCP ports:

```text
8080, 8888, 3128, 8000, 8118
```

## Build From Source

Requirements:

- Qt 6.8.2 MinGW
- CMake
- MinGW Makefiles

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\6.8.2\mingw_64\bin;$env:PATH"
$src = "C:\Users\Ivann\OneDrive\Desktop\50F3~1\vrpProxy"
cmake -S $src -B "$src\build" -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:\Qt\6.8.2\mingw_64
cmake --build "$src\build" -j 4
```

Deploy Qt runtime:

```powershell
C:\Qt\6.8.2\mingw_64\bin\windeployqt.exe --release --no-translations "$src\build\vrpProxy.exe"
```

## Links Used In App

- Meta Quest Software Update Tool: <https://www.meta.com/help/quest/software_update/>
- QLoader download: <https://cloud.dipvr.ru/download/trailers/>
- QLoader certificate bot: <https://t.me/qlfixbot>
- Required Telegram channel: <https://t.me/VRGamesRUS>
- SideQuest setup: <https://sidequestvr.com/setup-howto>
- Oculus ADB drivers: <https://developers.meta.com/horizon/downloads/package/oculus-adb-drivers/?locale=ru_RU>

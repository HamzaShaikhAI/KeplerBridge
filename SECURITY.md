# Security Policy

## 1. Supported Versions

Security and stability updates are applied to the active development branch and release versions:

| Version | Supported |
| :--- | :---: |
| 0.8.x (Phase 8 RC) | :white_check_mark: |
| < 0.8.0 | :x: |

---

## 2. Reporting a Vulnerability

If you discover a security vulnerability, buffer overflow, or unauthorized privilege escalation in KeplerBridge, please do **not** disclose it in a public GitHub issue.

Instead, please report it privately via GitHub Private Vulnerability Reporting or contact the project maintainers directly with:
1. A description of the issue and potential security impact.
2. Step-by-step instructions or proof-of-concept code to reproduce the behavior.
3. Your operating system version, GPU model, and driver version.

We will acknowledge receipt within 48 hours and coordinate a fix and advisory prior to public disclosure.

---

## 3. Anti-Cheat and Terms of Service Notice

KeplerBridge is a DirectX proxy library that replaces the system `d3d12.dll` inside the local application folder. 

* **Multiplayer Warning:** Modern multiplayer games incorporate anti-cheat systems (such as Easy Anti-Cheat, BattlEye, Riot Vanguard, Blizzard Warden, and Valve Anti-Cheat). These systems monitor DLL signatures and memory integrity. Loading KeplerBridge into a multiplayer game will likely trigger anti-cheat protections, resulting in game launch failures or account bans.
* **Strict Offline Policy:** KeplerBridge is intended exclusively for offline single-player applications, legacy hardware research, and game preservation.
* **Non-Circumvention:** KeplerBridge does not incorporate anti-detection mechanisms, kernel drivers, or anti-cheat bypasses. Maintainers will immediately reject and close any issue or pull request aimed at bypassing anti-cheat software.

---

## 4. Privacy & Telemetry

KeplerBridge values user privacy:
* All logging occurs strictly to local storage (`kepler_bridge.log`).
* File paths written to the log are sanitized to redact personal Windows user directory names (`Users\[USER]\`).
* KeplerBridge performs zero network telemetry, analytics collection, or external network requests.

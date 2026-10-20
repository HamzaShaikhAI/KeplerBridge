---
name: Game Compatibility Report
about: Report compatibility results for a specific game
title: "[COMPAT] <Game Name>"
labels: compatibility
assignees: ''

---

## Game Information
- **Game Title:** 
- **Developer / Publisher:** 
- **Game Version / Store Platform:** (Steam / GOG / Epic Games / Other)
- **Engine:** (e.g. Unreal Engine 4/5, Unity, REDengine, RE Engine, Custom)
- **Renderer API:** (D3D12 only / D3D11 & D3D12 toggle / `-dx11` supported)
- **Anti-Cheat:** (None / EAC / BattlEye / Denuvo - Was it tested offline?)

## Test System Specs
- **GPU:** (e.g. NVIDIA GeForce GT 740 GK208 4GB DDR3)
- **Driver Version:** (e.g. 456.71)
- **OS:** Windows 10 x64 / Windows 11 x64

## Compatibility Status
Please select one tier:
- [ ] VERIFIED WORKING (Boots, renders, playable)
- [ ] PARTIALLY WORKING (Reaches menu/in-game, visual bugs, or requires manual tweaks)
- [ ] TESTED - FAILED (Intercepts but crashes with identified error)
- [ ] BLOCKED / INCOMPATIBLE (Hardware hard limit: DXR, Mesh Shaders, VRS, DirectStorage, Anti-Cheat)

## Configuration (`kepler_bridge.ini`)
```ini
[General]
LogLevel=3
LogToFile=true
EnableFeatureLevelSpoof=true
TargetFeatureLevel=0xc000
EnableD3D11Translation=true
EnableShaderDiskCache=true
```

## Observations & Diagnostic Telemetry
Describe the launch behavior, framerate, visual rendering quality, and paste the startup banner or diagnostic error from `kepler_bridge.log`:
```text
[Paste diagnostic summary here]
```

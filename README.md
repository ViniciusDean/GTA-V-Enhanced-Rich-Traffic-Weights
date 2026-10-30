# GTA V Enhanced Rich Traffic Weights

Balanced and varied DLC traffic for **GTA V Enhanced Story Mode**, with support for NaturalVision Enhanced (NVE).

This package expands the civilian traffic pool while keeping ordinary and premium vehicles common, luxury cars appropriate to wealthy districts, and sports/exotic vehicles noticeable without dominating every street.

> Offline Story Mode only. Do not use this ASI plugin in GTA Online.

## Highlights

- 389 unique civilian models across the primary traffic groups.
- Rich traffic mixes everyday, premium, luxury and rotating sports cars.
- 109 everyday models and 35 luxury/GT models remain eligible in controlled areas.
- 28 of 98 sports/exotic models rotate every 30 seconds.
- Global anti-repeat control detects duplicate driven cars within 180 metres and temporarily suppresses additional spawns.
- Existing vehicles are never deleted, replaced or force-despawned.
- HSW/tuner-style cars can appear, while dedicated Drift variants remain rare and regional.
- The `vivanite2` KnoWay autonomous taxi is intentionally excluded.
- Story and `_mp` traffic groups are kept aligned where applicable.

## Requirements

- GTA V Enhanced for PC.
- The official [Script Hook V](https://www.dev-c.com/gtav/scripthookv/) with ASI loading enabled.
- NaturalVision Enhanced is optional; the included path is compatible with its `onigiri` structure.
- A trainer setting that prevents DLC vehicles from despawning is recommended for offline testing.

## Installation

Close the game and back up your existing files first.

1. Copy `dist/popgroups.ymt` to:

   ```text
   <GTA V Enhanced>\onigiri\platform\levels\gta5\popgroups.ymt
   ```

2. Copy these files to the GTA V Enhanced root directory:

   ```text
   dist\RichTrafficWeights.asi
   dist\RichTrafficWeights.ini
   ```

3. Start Story Mode offline. The log beside the plugin should contain:

   ```text
   Inicializado v1.2.0; rich=109/35/98; ativos=109/35/28; vivanite2=REMOVIDO
   ```

Press `F10` to toggle the plugin. Disabling it releases every model suppression immediately.

## Configuration

The defaults in `RichTrafficWeights.ini` are the tested profile:

```ini
[RichTrafficWeights]
Enabled=true
ZonePollMs=1000
SportCycleSeconds=30
DailySlots=109
LuxurySlots=35
SportSlots=28

AntiRepeatEnabled=true
RepeatScanMs=5000
RepeatRadiusMeters=180
RepeatLimit=2
RepeatCooldownSeconds=90
MaxRepeatCooldownModels=32

ToggleKey=F10
DebugOverlay=false
DebugTelemetry=false
LogAntiRepeat=true
```

Rich/central weighting is applied in these zones:

```text
ROCKF, RICHM, RGLEN, BURTON, GOLF, CHIL,
DOWNT, PBOX, LEGSQU, SKID, TEXTI, ALTA
```

Outside those zones, the regional `popgroups` composition remains in control and only anti-repeat stays active.

## Building the ASI

Use Visual Studio with the MSVC x64 toolchain, CMake 3.20 or newer, and the official ScriptHookV SDK. The SDK is not redistributed here.

```powershell
cmake -S . -B build -A x64 -DSCRIPHOOK_SDK="C:\path\to\ScriptHookV_SDK"
cmake --build build --config Release --target RichTrafficWeightsPolicyTests -- /m:1
cmake --build build --config Release --target RichTrafficWeights -- /m:1
ctest --test-dir build -C Release --output-on-failure
```

The plugin uses the static MSVC runtime. The release binary depends on `ScriptHookV.dll` and Windows system libraries.

## Validation

Version 1.2.0 passed:

- Production policy unit tests.
- XML duplicate and spelling checks.
- Binary YMT import/export/recompile round-trip.
- SHA-256 comparison between the compiled candidate and round-trip output.

Checksums for the downloadable files are included in `dist/SHA256SUMS.txt` and in the GitHub release.

## Uninstall / restore

1. Remove `RichTrafficWeights.asi` and `RichTrafficWeights.ini` from the game root.
2. Restore the `popgroups.ymt` backup created before installation.

The mod does not modify NVE graphics, ReShade, `gameconfig.xml`, ScriptHookV or the ScriptHookV Trainer.

## Credits and disclaimer

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

This is an unofficial fan-made mod. It is not affiliated with or endorsed by Rockstar Games, Take-Two Interactive, NaturalVision, Script Hook V, CodeWalker, GTACars.net, Google or Waymo. All trademarks belong to their respective owners.

---

## Resumo em português

Mod de tráfego para GTA V Enhanced em Story Mode offline. Ele amplia a variedade de veículos DLC, mantém carros cotidianos predominantes, controla esportivos por rotação e bloqueia temporariamente novos spawns de modelos que começam a se repetir perto do jogador. A instalação pronta está na seção [Releases](../../releases).


## v1.4.1

- Fix: drift-tuned variants (driftdominator9, drifthardy, driftrt3000, ...) were in the traffic groups since v1.2 and slid around instead of driving. They are removed from `popgroups.ymt` and the plugin now suppresses all 27 known drift variants everywhere (`BanDriftVariants=true`).
- A model weighted `0` in `[Models]` is now banned on the whole map, including cars outside the mod's catalog.

## v1.4.0

- New Exotic tier: 44 supercars and hypercars split from Sport (now 68), with their own lower chance per zone.
- Per-model multipliers in the `[Models]` INI section (`spawnname=0..10`; 0 bans a model).
- Rain, thunder and snow scale the sport and exotic chances by `RainFastMultiplier` (default 0.5).
- `ReloadKey` (default F11) re-reads the INI in game; an invalid INI keeps the previous settings.
- Logs show game display names for vanilla models instead of `hash_XXXXXXXX`, and are now fully in English.
- The debug overlay shows the current daily/luxury/sport/exotic chances.

## v1.3.0

- Catalog grew to 266 models (116 daily / 38 luxury / 112 sport): 2023-2026 DLC cars such as coquette6, jester5, horus, pipistrello, fmj2, dominator9/10, warden. They are also added to `veh_rich` in `popgroups.ymt.pso.xml`.
- Real weights: each zone has a probability per tier (daily/luxury/sport). Models are re-rolled every `SportCycleSeconds` and on entering a new zone; at least one model per tier always stays active.
- 28 built-in zones (rich and poor) that can be overridden or extended in the INI via `[Zone.NAME]` sections with `Daily`, `Luxury`, `Sport` (0.0-1.0).
- Night boost for sports (`NightSportMultiplier`, `NightStartHour`, `NightEndHour`).
- An invalid INI now falls back to built-in defaults instead of disabling the plugin.
- Removed `DailySlots`, `LuxurySlots` and `SportSlots`.

# FH2 Sim Immersion for Azimuth

**Status: 0.9.7.** Head tracking was tested with Beam Eye Tracker. The wheel, assists, steering lock, in-game wheel and draw distance are experimental.

Realism and immersion features for Forza Horizon 2 running in Azimuth:

* [Head tracking](#head-tracking): webcam head tracking in the cockpit camera.
* [Steering wheel](#steering-wheel-experimental) (experimental): DirectInput wheels with force feedback, pedals, shifter and handbrake.
* [Realistic assists](#realistic-assists-experimental) (experimental): ABS, traction control, stability control and clutch per real car.
* [Steering lock and in-game wheel](#steering-lock-and-the-in-game-wheel) (experimental): real lock-to-lock rotation and a cockpit wheel that turns like the real one.
* [Pop-in killer](#pop-in-killer): scenery, trees, fences and grass keep their detail farther away.

**Unofficial.** Not affiliated with Azimuth / Almost There Games, Microsoft or Playground Games. **Before reporting any Azimuth bug, remove `dinput8.dll`, the `mods\Sim Immersion` folder and ExtraOptions (its `winmm.dll` and `mods` folder), and reproduce it.**

**Supported:** Forza Horizon 2 **1.0.0.64** (with or without Storm Island) on Azimuth 1.0.4 with ExtraOptions 0.4.4 or newer. On any other build the plugin changes nothing and logs why.

## How it works

ExtraOptions (its `winmm.dll` next to `azimuth.exe`) loads `mods\Sim Immersion\plugins\FH2SimImmersion.dll`. The `dinput8.dll` next to `azimuth.exe` only keeps Azimuth off the wheel. `FH2SimImmersion.exe` is the settings window. The plugin checks the build (by hash and an opcode skeleton) and patches memory only. No game or Azimuth file is edited, and this repository has no game code or data.

* Head tracking feeds the game's own Kinect head-tracking camera layers.
* The wheel is a virtual Xbox wheel. The game drives it with its own steering and force feedback.

## Install

### Requirements

* Azimuth 1.0.4.
* [ExtraOptions](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions) 0.4.4 or newer (the Azimuth build).
* Optional: [FH2 Streaming Radio](https://github.com/RussFactorShow/FH2-Streaming-Radio).

### Steps

1. Install ExtraOptions by its own readme: copy the files and the `mods` folder from the `Azimuth` folder of its zip into the folder of `azimuth.exe`.
2. From this mod's zip (its `FH2SimImmersion` folder), copy `dinput8.dll` into the folder of `azimuth.exe` (replacing any older copy), and the `mods\Sim Immersion` folder into `mods`.
3. Start Azimuth once. Check `FH2_ExtraOptions.log` for `FH2SimImmersion.dll | initialized`, or the **Setup** section on the Logging tab of `FH2SimImmersion.exe`.

### Before you play

* **Beam Eye Tracker:** Game Connection -> **Head Tracking Device Emulation**, Driver `Compatibility`.
* **[OpenTrack](https://github.com/opentrack/opentrack/releases)** with a webcam: Input `neuralnet tracker`; Output `freetrack 2.0 Enhanced` or `UDP over network` (`127.0.0.1`, port `4242`); Filter `Accela`; bind a key to **Center**. If both trackers run, FreeTrack is used.
* **Wheel:** plug it in before the game. Set its rotation in your wheel software (900 degrees recommended, e.g. Logitech G HUB) and disable any game profile that remaps it.
* Head tracking works in the **cockpit camera** only.

### Why dinput8.dll is still needed

ExtraOptions loads from `winmm.dll`, but Azimuth opens DirectInput before any mod loads, so `dinput8.dll` must stay next to `azimuth.exe`. While the Wheel check is on, it stops Azimuth from taking the wheel exclusively, which would block force feedback.

### Upgrading from 0.9.x

* Delete the old `FH2SimImmersion.dll` and `FH2SimImmersion.exe` from the folder of `azimuth.exe`, and replace `dinput8.dll`.
* Move `FH2SimImmersion.ini` and `FH2SimImmersion_cars.ini` into `mods\Sim Immersion` (do step 2 first).

### Other mods

Put [FH2 Streaming Radio](https://github.com/RussFactorShow/FH2-Streaming-Radio) in `mods`. Its skip-song combo (LB+X) also works from the wheel: press the left paddle and X together.

### Uninstall

Delete `dinput8.dll` from the folder of `azimuth.exe` and the `mods\Sim Immersion` folder. ExtraOptions drops the mod from `loadorder.txt` on its next start.

## Settings window

**Apply** saves to `FH2SimImmersion.ini` next to the window; the game reads it at start. **Revert** reloads the saved values of the current tab. **Defaults (this tab)** fills in the defaults. Hover a setting for help.

* **Head tracking:** source, smoothing, gains, invert, limits, hotkeys and recenter buttons, with a live preview.
* **Wheel (experimental):** Test wheel box, wheel mapping, force feedback and Advanced.
* **Simulation:** realistic assists, the Steering wheel group, and a car list with what each real car had.
* **Pop-in killer:** a slider per kind of scenery, from the game's x1 to x8.
* **Logging:** the log switch, detailed logs, and a Setup section that says what is missing (ExtraOptions, `dinput8.dll` or the plugin).

Each tab starts with a check that turns that part on or off:

| Tab | INI key | Off means |
|---|---|---|
| Head tracking | `[General] OverrideAzimuthHeadTracking` | head tracking is left to Azimuth |
| Wheel | `[Wheel] Enabled` | the steering wheel is left to Azimuth |
| Simulation | `[Simulation] Enabled` | no realistic assists, steering lock or in-game wheel changes |
| Pop-in killer | `[DrawDistance] Enabled` | detail switches stay with the game and Azimuth's own settings |
| Logging | `[Logging] Enabled` | no `FH2SimImmersion.log` is written |

Field of view is set in [ExtraOptions](https://github.com/RokkuDayo/ForzaHorizon2_ExtraOptions)' camera settings; this mod no longer changes it.

## Head tracking

`Source=auto` (default) uses FreeTrack shared memory (Beam, or OpenTrack's `freetrack 2.0 Enhanced`) when it exists, otherwise OpenTrack UDP on `127.0.0.1`, port `4242` (`UdpPort`). The plugin centres and smooths the pose and feeds the cockpit camera layers, so the game's camera physics apply.

Look straight ahead and recenter:

| Action | Default |
|---|---|
| Recenter | **L3 + R3** on an Xbox controller |
| Toggle head tracking | `End`, or `Ctrl+Shift+Y` |

You can also recenter in your tracker. Remap in `mods\Sim Immersion\FH2SimImmersion.ini` (restart the game after editing): `RecenterButtons` (e.g. `BACK+START`), `WheelRecenterButtons` (default `4+5`, both G29 paddles), `RecenterKey` / `ChordRecenterKey`.

Tuning (the defaults were tested with Beam):

* **Direction:** `YawInvert`, `PitchInvert`, `RollInvert`, `XInvert`, `YInvert`, `ZInvert` set to `1` if an axis is backwards. `RotOrder` / `PosOrder` swap axes.
* **Strength:** the curves in your tracker, or `YawGain`, `PitchGain` and so on.
* **Left/right balance:** `YawLeftGain` / `YawRightGain`. If sides are swapped, set `YawLeftIsPositive=1`.
* **Range:** `Limit` per axis; `Limit=0` turns an axis off.
* **Smoothing:** `Smoothing` (e.g. `0.2`).

## Steering wheel (experimental)

The game supports the Logitech G920 on Xbox. The plugin adds a virtual Xbox wheel and feeds it from your DirectInput wheel, instead of Azimuth's own wheel support. Tested with a G29, pedals and the Driving Force Shifter.

* **Works:** steering, throttle, brake, clutch, A/B/X/Y, View, Menu, D-pad, shift paddles, the H-shifter (set the game's transmission to **Manual with clutch**) and force feedback.
* **Not supported:** buttons the G920 does not have (G29 L2/R2, L3/R3, +/-, dial, Enter, PS).

The wheel is on by default. The log shows `wheel: using "..."` and `wheel: virtual wheel added to the controller list`.

`[Wheel]` settings:

* `DeviceMatch`: part of the wheel's name; empty uses the first wheel with force feedback.
* `PedalsDevice`, `ShifterDevice`, `HandbrakeDevice`: part of the name of separate USB devices; empty means they are on the wheel.
* `WheelRange`: the rotation set in your wheel software, in degrees.
* `SteeringAxis`, `ThrottleAxis`, `BrakeAxis`, `ClutchAxis`, `PedalsInverted`, `PedalDeadzone`: axis mapping (the G29 defaults fit).
* `ButtonA` ... `ButtonRB`: DirectInput button numbers, 0-based. `HShifter=1` with `GearButtons` / `ReverseButton` for the shifter. `UserMode=none` if the game ignores the wheel in menus.
* Handbrake: `HandbrakeButton`, `HandbrakeAxis`, `HandbrakeAxisInvert`, `HandbrakeThreshold`. While pulled, the game button in `HandbrakeGameButton` (default `B`) is pressed.
* To find numbers, use **Test input** or **Press to assign** on the Wheel tab, or `Diagnostics=1` (under `[Input]`) for `wheel diag raw:` lines.

### Force feedback

The wheel gets the game's own forces as one DirectInput force: steering torque (it builds as you turn, levels off and goes light when the front tyres slide), centring spring, damping and rumble. The mod adds a soft lock at the car's steering lock and the Smallest force lift. Extra forces were tried and removed: a recorded lap showed the game's steering torque already behaves like a real front tyre.

* `FFB=0` turns force feedback off. `FFBInvert=1` reverses it, for a wheel that pushes away from the centre in menus.
* `FFBGain` (1.0): strength; 1.0 is the game's own force, and a force of 1.0 is the wheel's maximum. Raise it if the wheel feels light.
* `FFBTorqueGain` (steering torque), `FFBSpringGain` (centring spring), `FFBRumbleGain` (kerbs, cobbles, gravel; 0 - 5). 1.0 is the game default.
* `FFBMinForce` (`0.05`, 0 - 0.3): the smallest force sent. Gear and belt wheels such as the G29 do not move for a few percent, so without it the centre feels empty. Use `0` on direct drive.
* `FFBLockStrength` / `FFBLockRampDeg`: the soft lock, see [Steering lock](#steering-lock-and-the-in-game-wheel).
* `FFBLimitUnknown` (`1`): on wheels other than the G25, G27, G29, G920, G923 and Driving Force, the soft lock uses at most half the force. `0` removes the limit.
* `FFBForeground` (`0`): `1` sends force only while the game has focus. Use it if the log says `exclusive cooperative level failed`.
* The force goes to the axis the wheel reports as motorised. If that is not steering, the log shows `wheel ffb: force axis offset`.

Calibration (Wheel tab):

* **Test force: push right** pushes the wheel for half a second. If it turns left, tick **Invert force direction**.
* **Calibrate force** sets `FFBTorqueGain` so the strongest steering force reaches about 85% of the wheel. No drive is needed: the game's force is its tyre force (at most 1) times fixed factors and the in-game force feedback scale. The mod saves that maximum in `FH2SimImmersion_ffb.ini` when you are in a car; before that, the default 0.205 is used. Press Apply to keep the result.
* **Record drive:** press it, drive a few minutes, then **Stop recording and calibrate**. The strongest 1% of that drive is set to reach about 85%; press Apply to keep it. The app and mod talk through `FH2SimImmersion_record.ini` and `FH2SimImmersion_ffb.ini`. Restarting the game during a recording starts it again.

## Realistic assists (experimental)

Each car gets the assists its real counterpart had: a 1960s Ferrari has none, a modern supercar has all three. Only what the plugin decides is overridden; the rest of your menu choices stay.

Keys in `[Simulation]`:

* `RealisticABS=1`, `RealisticTC=1`, `RealisticSTM=1` turn on each assist (default `0`: the game setting is used).
* `OptionalAssists=game|on|off`: for cars where the real car had the aid only as an option. `game` (default) keeps your setting. It applies to the three assists and the clutch.
* `RealisticClutch=1`: cars with a clutch get "manual with clutch", cars without one get "manual without clutch". Only manual driving changes; automatic stays automatic. Works alone or with `RealisticAutomatic`.
* `RealisticAutomatic=1`: cars whose real gearbox was automatic-only (`at`, `cvt`, `ev`) always shift automatically.

Per-car overrides go in `mods\Sim Immersion\FH2SimImmersion_cars.ini`:

```
[Cars]
; car Id = ABS,TC,STM,CLUTCH    each on, off or auto (auto = use the research data)
; for CLUTCH, on = the car has a clutch, off = it has none, automatic = always shift automatically
1105=on,auto,off,on
```

Old lines with two or three fields still work; the missing ones are `auto`. A clutch override of `on` or `off` cancels `RealisticAutomatic` for that car.

The data is `data/car_assists.csv`, checked by hand but possibly wrong. Unknown cars keep the game's setting and rotation. To check it, look for `car: <id> <name> (ABS ..., TC ..., ...)` and `assists: ABS forced ... for <name>` in the log.

## Steering lock and the in-game wheel

Real cars turn their wheel from about 540 degrees lock to lock (race car) to 900-1080 (most road cars) or more (trucks, old cars). The game uses one fixed rotation for every car.

**Set your wheel software's rotation to the same value as `WheelRange` (900 for the G29), or the degrees below will be wrong.**

* `RealisticSteeringLock=1` (`[Simulation]`, default `0`) makes full steering happen at the real car's lock. The data is `SteeringLockDeg` in `data/car_assists.csv` and the **Lock** column of the car list. Fix a car with **Lock override** or in `FH2SimImmersion_cars.ini`:

```
[SteeringLock]
; car Id = lock to lock in degrees (90..3600)
1105=720
```

* `MaxWheelRotation` (degrees, `0` = `WheelRange`) is the most your wheel may turn. `SteeringLockMode=cap` (default) uses it for bigger locks and keeps smaller ones 1:1: a 1080 degree truck uses 900, a 540 degree car stays 540. `scale` shrinks every car proportionally so the largest lock (1908 degrees) uses the maximum: 1908 becomes 900, 540 becomes 255.
* **Soft lock:** `FFBLockStrength` (default `1.0`, `0` = off) builds an end stop at the lock. `FFBLockRampDeg` (default `3`): full force comes a third of this many degrees past the lock; larger is softer.
* **In-game wheel:** `RealCockpitWheel=1` turns the cockpit model's wheel by your wheel's full angle, not at most 90 degrees each way. Add `HideDriverInCockpit=1`, since the arms cannot follow past 90 degrees. `CockpitWheelDirection` (`1` or `-1`) fixes a reversed turn.

The log shows `steering lock: car <id> <name> real <deg> deg -> wheel <deg> deg (<source>)` and `cockpit: in-game wheel follows the real wheel`. The options are in the Simulation tab (Steering wheel group); the soft lock is on the Wheel tab.

## Pop-in killer

`[DrawDistance]` fights pop-in: the game switches trees, buildings, fences and grass to a coarser model (or a flat billboard) close to you. Each value is a **multiplier** of where that switch happens, `1` to `8`; `0` (default) keeps the game's. Far trees and billboards keep the game's own distances. Values change in memory as the game loads; `Enabled=0` turns the section off. While the section is on, the plugin also keeps the size of the game's three shadow layers steady: the game refits each layer to the camera every frame and rounds its size to whole metres, so the shadows in it jump as you drive. With Azimuth's Shadow distance on High or Ultra the middle layer reaches far out, and that shows as a slow flicker of the shadows on trees.

| Key | What it controls | Game value | Range |
|---|---|---|---|
| `SceneryDetail` | buildings, props, terrain and other scenery drawn per model. Azimuth's **Scenery detail** Ultra is 4; with both on, the stronger applies. Farther out, the finest loaded level at or past the scaled choice is drawn. The game's own coarser level is drawn too, so nothing is ever missing; in the main view, and as a shadow caster, it is hidden while the finer level of the same model is really being drawn there, because two levels on the same spot flicker and the coarser one's shadow falls on the finer one. How far trees and scenery are drawn grows by the larger of this and `TreeDetail` | x1 | up to 8 |
| `TreeDetail` | trees and bushes: where full trees switch to simpler ones and to billboards. Full trees are drawn that many times farther, as far as the game has them loaded: the game only loads tree models within its own streaming distance (a few hundred metres), which no setting here changes. The billboards keep the game's distances and stay behind the full tree, so where a full tree cannot be drawn yet its billboard still is, and nothing disappears. Azimuth does not reach them. How far trees and scenery are drawn (game: 360 m, 200 m on some tracks) grows by the larger of this and `SceneryDetail`, up to 3000 m | full trees to about 100 m, billboards from about 190 m | up to 8 |
| `FenceDetail` | fences, barriers and things that can be knocked over (signs, bollards): where they switch to a simpler model. How far they are drawn (game: 450 m) grows with it, up to 3000 m | about 100 m | up to 8 |
| `GrassDetail` | grass and crops: how far their detail reaches. Grass is drawn at most 500 m away, and only where the game has it loaded | about 100 m | up to 8 |
| `ShadowMapSize` | the largest width, in pixels, of the four shadow maps that hold the shadows of trees, buildings and terrain (default `1024`). The game blends every shadow over 4 x 4 pixels of its map, whatever their size, so with Azimuth's **Shadow detail** on High or Ultra the shadows of swaying leaves are sharp, and flash. In a test at one spot, 0.6 to 0.9 % of the screen's pixels flickered with Ultra's maps and 0.02 to 0.1 % with maps 1024 wide. Smaller = softer shadows that flash less; `0` leaves the sizes to the game and Azimuth | 2048, 1536, 1536, 1024 from near to far | 256 to 16384 |

How it works: trees, fences and grass are placed from the game's template libraries. As each library loads, the plugin multiplies the end of every near detail range that ends by 500 m, and where a coarser object takes over there (a fence's simpler model), moves its start along; the coarser object's own end caps the move. A tree's billboard is not moved: it stays under the full tree. Grass has a second limit: each frame the game draws only the grass patches on its current zone's list, which reaches about 200 m, so farther grass shows up in whole blocks. With `GrassDetail` above 1 the plugin adds every grass patch the game has loaded to that list, and the grass fades in instead. Ranges that end farther out are left alone.

Higher values cost frame rate; raise one at a time. The log shows `drawdist: ...` lines as the game loads. If the game build differs from the offsets, it says `mismatch; unchanged` and that part does nothing.


## Troubleshooting

A good run logs something like this (`layer` lines only appear in cockpit view):

```
FH2 Sim Immersion 0.9.7 loaded into pid ... (...\azimuth.exe)
waiting for the Forza Horizon 2 image to be mapped...
game image found at 0000...
patch: 5 head-tracking inputs redirected (thunks at ...)
freetrack: shared memory found
input: using freetrack
layer ... input 31 output 4 Param.x 12.000 -> rotation (cockpit)
layer ... input 28 output 2 Param.x 0.030 -> position (cockpit)
```

* **No log file:** set `[Logging] Enabled=1`. If still none, the plugin did not load: check the Setup section, `FH2_ExtraOptions.log`, and antivirus.
* **"this game build is not recognised":** not FH2 1.0.0.64. Nothing was changed.
* **"bind ... failed":** another program uses UDP port 4242. Change `UdpPort` here and in OpenTrack.
* **No head tracking:** check the tracker runs, the log shows `freetrack: shared memory found` or UDP packets, you are in the cockpit camera, and `End` did not turn it off.
* **Patched, but no `layer` lines in cockpit view:** report it with the log.
* **View moves more than your head (Beam):** Beam's Immersive Camera amplifies rotation and eye-gaze extension. Lower it in Beam, or reduce `YawGain` / `PitchGain`. If `in yaw` keeps growing while your head is still, the tracker causes it.
* **Jitter:** better face lighting, OpenTrack's Accela filter, or `Smoothing=0.2`.
* **Recenter does not work:** without an Xbox controller, use `WheelRecenterButtons`, the keyboard key, or your tracker.
* **Wheel not found:** plug it in before the game, check `Enabled=1` and `DeviceMatch`, and close programs that grab the wheel.
* **Wheel ignored or odd in menus:** `UserMode=none`; if it pushes away from the centre, `FFBInvert=1`.
* **Wrong pedals or axes:** read the `wheel diag raw:` lines (or **Test input**), then fix the axes and `PedalsInverted`.
* **Steering degrees wrong:** `WheelRange` must match your wheel software.
* **`mismatch; unchanged`:** the game build differs from the offsets; that part does nothing.
* **Azimuth bug:** reproduce it without the mod before reporting (see the Unofficial note at the top).

## Building from source

```
src/dll/        plugin loaded into azimuth.exe
src/common/     shared headers, car database (generated from data/car_assists.csv)
src/proxy/      dinput8.dll: keeps Azimuth off the wheel
src/app/        FH2SimImmersion.exe: settings window
tests/          unit tests, integration harness, fake azimuth.exe
tools/          gen_car_db.py (generates src/common/car_db.h)
docs/           reverse-engineering notes
```

* **Windows (Visual Studio 2022 or MSYS2 MinGW64):** `cmake -S . -B build -A x64`, then `cmake --build build --config Release`.
* **`./build.sh`** on Linux, WSL or MSYS2 cross-builds. It needs `x86_64-w64-mingw32-g++` and `windres` (mingw-w64), a native `g++`, `python3` and `zip`. It packages `dist/FH2SimImmersion-0.9.7.zip`: one `FH2SimImmersion` folder with `dinput8.dll` and `mods\Sim Immersion\` (plugin, app, `mod.json`, README, `LICENSE.txt`).

The harness needs your own copy of the game executable; none is included. It maps the game into memory, loads the DLL, sends OpenTrack packets and runs the game's `CameraLayer::EvaluateInput`. Add `--deep` for the fallback memory scan. It runs on Windows or Wine:

```
harness.exe "<game>\Mount\forza_durango_release_final.exe" build\FH2SimImmersion.dll [--deep]
```

The analysis and every offset are in [docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md) and `src/dll/game_layout.h`.

## License

MIT License. See [LICENSE](LICENSE) for details.

Forza Horizon 2 is a trademark of Microsoft. This project is unofficial and not affiliated with Microsoft, Playground Games, Turn 10 or Almost There Games (Azimuth).

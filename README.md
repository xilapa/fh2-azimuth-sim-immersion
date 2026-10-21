# FH2 Sim Immersion for Azimuth

**Status: 0.9.2. Head tracking tested in-game with Beam Eye Tracker (FreeTrack input).
Steering wheel support is experimental.**

Realism and immersion add-ons for Forza Horizon 2 running in Azimuth:

* **Webcam head tracking** (see below).
* **Steering wheel support, experimental.** Uses a DirectInput wheel such as the Logitech
  G29 as the game's own Xbox wheel. See [Steering wheel (experimental)](#steering-wheel-experimental).

Head tracking drives the game's **own head-tracking camera layers**, the ones built
for Kinect, with your head pose from OpenTrack. You get yaw, pitch and roll, plus lean
left/right, up/down and forward/back, in the cockpit view.

Unofficial. Not affiliated with Azimuth / Almost There Games, Microsoft or Playground Games.
The Azimuth developer does not support modified setups: **close FH2SimImmersion.exe before
reporting any Azimuth bug.**

Supported: Forza Horizon 2 **1.0.0.64** (with or without Storm Island) on Azimuth 0.9.x.
On any other game build the plugin detects the difference, changes nothing, and logs why.

## How it works

The game evaluates a head-tracking input for each camera layer every frame
(`HeadTrackingX/Y/Z/Yaw/Pitch` in CameraPhysics.xml). In this build the Kinect side of
those inputs is almost empty. X, Y and Z come from a function that always returns zero,
pitch is always zero, and yaw is only body lean, capped at about 12°.

The plugin redirects those five input reads. For the cockpit layers it supplies your
OpenTrack pose:

* rotation goes through the cockpit `HeadTrackingYaw → CameraSpaceYPROffset` layer
* position goes through the cockpit `HeadTrackingX → CarSpaceXYZOffset` layer

The game then applies them with its own layer maths and spring smoothing. Nothing is
written to the Azimuth or game folders. Camera.zip is not modified; the layer ranges
are widened in memory.

## Install

1. Set up a head tracker. Either of these works. When both are running, FreeTrack is used:
   * **Beam Eye Tracker:** Game Connection → **Head Tracking Device Emulation**, with
     Driver set to `Compatibility`. Beam then publishes the FreeTrack/TrackIR shared memory,
     and the plugin reads it directly.
   * **[OpenTrack](https://github.com/opentrack/opentrack/releases)** with a webcam:
     * **Input:** `neuralnet tracker`.
     * **Output:** `freetrack 2.0 Enhanced`, or `UDP over network` (`127.0.0.1`, port `4242`).
     * **Filter:** `Accela`.
     * Bind a key to **Center**.
2. Unzip this folder anywhere **outside** the Azimuth folder, e.g. `C:\Games\FH2SimImmersion`.
3. Start `FH2SimImmersion.exe`. It opens the **settings window** and, while the window is open,
   loads the plugin into any `azimuth.exe` that starts. Use either:
   * **A:** Start Azimuth as usual (or press **Start Azimuth** on the **Main** tab, after choosing
     `azimuth.exe` with **Browse...**). Leave the window open while you play.
   * **B:** Drag `azimuth.exe` onto `FH2SimImmersion.exe`, or use a shortcut:
     `FH2SimImmersion.exe "E:\XOne\Forza Horizon 2 Azimuth 0.9.10\azimuth-windows-0.9.10\azimuth.exe"`.
     It starts Azimuth with the plugin; the window stays open after Azimuth exits.
   Starting `FH2SimImmersion.exe` a second time brings the open window to the front.
4. Drive in the **cockpit camera**.

Windows Defender may flag FH2SimImmersion.exe, because it loads a DLL into another process,
which is what injectors do. If it does, allow it, or use the ASI loader option below.

**Alternative without the injector:** copy Ultimate ASI Loader's `dinput8.dll` next to
`azimuth.exe`, renamed to `winmm.dll`, and copy `FH2SimImmersion.dll` there renamed to
`FH2SimImmersion.asi`. This does put files in the Azimuth folder.

## Settings window

`FH2SimImmersion.exe` is the settings window. Change values, then press **Apply**. They are saved
in `FH2SimImmersion.ini` next to it and take effect the next time the game starts (the plugin
reads the file when it loads). **Revert** reloads the saved values of the current tab, and
**Defaults (this tab)** puts the default values in the current tab's controls (press Apply to
keep them). Hover a setting for its explanation.

* **Head tracking:** tracker input and network, smoothing, every rotation and position gain,
  invert and limit, the hotkeys and the recenter buttons. The picture shows a head from above,
  the side and the front with the axis you are editing highlighted; the arrows follow the limits.
* **Wheel (experimental):** device, axes, buttons, H-shifter and all force feedback settings.
  The picture shows the wheel, pedals and shifter with the setting of each part; the part of
  the control you edit is highlighted. **Test input** reads the wheel without the game and shows
  its axes and the numbers of the pressed buttons, to find the right button numbers.
* **Simulation:** field of view per camera, realistic assists and a list of all known cars with
  what the real car had. Select a car and choose how its ABS / traction control is forced
  (auto, on or off); Apply saves `FH2SimImmersion_cars.ini` (a car with both on auto is removed).
* **Main:** the path of `azimuth.exe`, Start Azimuth, the diagnostics switch, and a status box
  with what the launcher did and the last lines of `FH2SimImmersion.log`.

The INI file still works when edited by hand, and everything the window does is also described below.

## Controls

| Action | Default |
|---|---|
| Recenter | **L3 + R3** on an Xbox controller (both sticks clicked together) |
| Toggle head tracking | `End`, or `Ctrl+Shift+Y` |

Both clicks also reach the game, as usual. You can also recenter in your tracker
(Beam / OpenTrack). All of these are remappable in `FH2SimImmersion.ini`:
* `RecenterButtons`, e.g. `BACK+START` (also on the Head tracking tab).
* `WheelRecenterButtons`: steering wheel buttons held together to recenter (default `4+5`,
  both G29 shift paddles).
* `RecenterKey` / `ChordRecenterKey` to add a keyboard key. That file is created next to the DLL on the
first run. Edit it, then restart the game.

## Steering wheel (experimental)

The game supports the Logitech G920 on Xbox, but Azimuth only passes gamepads to it. The
plugin adds a virtual Xbox wheel to the game's controller list and feeds it from your
DirectInput wheel. The game then uses its own wheel controls and steering. Tested with a
Logitech G29, with pedals and the Driving Force Shifter.

What works:
* steering, throttle, brake and clutch;
* A/B/X/Y, View, Menu, D-pad and the shift paddles;
* the H-shifter (set the game's transmission to **Manual with clutch**);
* force feedback (new, still being tuned): the game's steering torque, centring spring,
  damping and rumble, played as one DirectInput force.

Not supported:
* Buttons the G920 does not have (G29 L2/R2, L3/R3, +/−, dial, Enter, PS) do nothing.

Setup:
1. Plug in the wheel before starting the game. Set the rotation in Logitech G HUB
   (900° recommended) and disable any G HUB game profile that remaps the wheel.
2. Start FH2SimImmersion.exe and the game as usual. The wheel is on by default; set
   `Enabled=0` under `[Wheel]` in `FH2SimImmersion.ini` to turn it off.
3. The log shows `wheel: using "..."` and `wheel: virtual wheel added to the controller list`.

`[Wheel]` settings in `FH2SimImmersion.ini`:
* `DeviceMatch`: part of the device name (default `G29`). Leave it empty to use the first wheel found.
* `WheelRange`: the rotation set in G HUB, in degrees.
* `SteeringAxis`, `ThrottleAxis`, `BrakeAxis`, `ClutchAxis`, `PedalsInverted`, `PedalDeadzone`:
  axis mapping. The defaults fit the G29.
* `ButtonA` … `ButtonRB`: DirectInput button numbers (0-based) for the game's buttons.
* `HShifter=1` with `GearButtons` / `ReverseButton` for the shifter.
* `UserMode=none` if the game ignores the wheel or acts oddly in menus.
* `FFB=0` turns force feedback off. `FFBGain` sets its strength (1.0 = the game's maximum
  force is the wheel's maximum). `FFBInvert=1` reverses it: use this if the wheel pushes
  away from the centre in menus instead of returning to it.
* Force feedback feel: `FFBTorqueGain` (the game's steering torque, 1.0 = game default),
  `FFBSpringGain` (centring spring), `FFBSlipSpringFloor` (share of the centring spring kept
  when the rear slides), `FFBSlipGain` (counter-steer force in a slide, 0 = off),
  `FFBSlipStart` / `FFBSlipFull` (rear slip range for that force) and `FFBSlipInvert`.
  `FFBSlipInvert=1` is the default: the counter-steer force pushes the wheel toward the
  direction of travel, which is correct for the G29. If it turns into the slide instead, set
  `FFBSlipInvert=0`. If the wheel feels too light off-road, raise `FFBTorqueGain` or
  `FFBSlipSpringFloor`.
* Front-tyre slides (understeer, spins, handbrake turns): `FFBAlignGain` (0.6, 0 = off) is a
  self-aligning force that turns the wheel toward the direction of travel while the front
  tyres slide; `FFBAlignStart` / `FFBAlignFull` (0.05 / 0.5) are the front slip where it starts
  and where it is full. It follows `FFBSlipInvert`.
* `FFBRumbleGain` (1.5, 0 - 5) scales the road-surface and impact vibration (kerbs,
  cobblestones, gravel). 1.0 = game default.
* Handbrake: the game reads the handbrake only as a button (its wheel layouts use **B**, layout 4
  uses **A**). Set `HandbrakeButton` (a DirectInput button number, -1 = none) and/or
  `HandbrakeAxis` (an axis name, NONE = none; `HandbrakeAxisInvert=1` if it reads maximum when
  released, `HandbrakeThreshold` = travel from 0.05 to 0.95 that counts as pulled). While the
  handbrake is pulled the plugin presses the game button named by `HandbrakeGameButton`
  (A, B, X, Y, LB or RB; default B). The settings window has a Handbrake group and a lever in the
  wheel picture.

To find axis and button numbers, set `Diagnostics=1` (under `[Input]`). Every 2 seconds the log shows a
`wheel diag raw:` line with every axis value and the pressed buttons.

## Field of view

The `[Simulation]` section widens (or narrows) the field of view of the cockpit, driver, hood
and bumper cameras: `FovCockpit`, `FovDriver`, `FovHood` and `FovBumper`. Each value is the
number of degrees added to the game's own field of view; `0` leaves the game default alone.
For example `FovCockpit=10`. The log shows the game's value once per camera as
`fov: ... base`.

## Realistic assists (experimental)

Forza lets you switch ABS, traction control and stability control on or off for every car. With
this option each car gets what its real counterpart had: a 1960s Ferrari drives without any of
them, a modern supercar with all three. The three assists are independent: each has its own
switch and its own research data. The plugin overrides the game's assist settings for the car you are
driving, so your menu choices stay untouched for everything it does not decide.

The `[Simulation]` section has the keys:

- `RealisticABS=1`, `RealisticTC=1` and `RealisticSTM=1` turn the feature on for ABS, traction
  control and stability control, each on its own (default `0`: the game setting is used for every car).
- `OptionalAssists=game|on|off` is what to do for cars where the real car had the aid only as an
  option: `game` keeps your game setting (default), `on` or `off` forces it. It applies to all
  three assists.

`TieStabilityToTC` from version 0.9.0 is no longer used (the log says so); use `RealisticSTM`.

Per-car overrides go in an optional file `FH2SimImmersion_cars.ini` next to the DLL:

```
[Cars]
; car Id = ABS,TC,STM    each on, off or auto (auto = use the research data)
1105=on,auto,off
```

Old lines with two fields (`Id=ABS,TC`) still work; the stability control is then `auto`.

The data comes from research of each real car and is in `data/car_assists.csv` (Id, name, year, ABS,
TC, STM, confidence and notes for every car). It can contain mistakes: corrections are welcome. Cars
marked unknown, and cars that are not in the list, keep your game setting.

To check that it works, look in `FH2SimImmersion.log` for `car: <id> <name> (ABS ..., TC ..., STM ...)` when
a race starts, and `assists: ABS forced ... for <name>` (likewise `TC` and `stability control`) when the plugin changes a setting. With
`Diagnostics=1` the log also shows `assists diag:` lines with how often the game asked for the
settings.

## Checking that it works

`FH2SimImmersion.log` is written next to the DLL. A good run looks like this:

```
FH2 Sim Immersion 0.9.2 loaded into pid ... (...\azimuth.exe)
waiting for the Forza Horizon 2 image to be mapped...
game image found at 0000...
patch: 5 head-tracking inputs redirected (thunks at ...)
freetrack: shared memory found
input: using freetrack
layer ... input 31 output 4 Param.x 12.000 -> rotation (cockpit)
layer ... input 28 output 2 Param.x 0.030 -> position (cockpit)
```

The `layer ...` lines only appear once you are driving in cockpit view.

## Tuning

The defaults have been tested with Beam. Use these settings if your tracker behaves
differently:

* **Direction:** if an axis moves the wrong way, set its `Invert` to `1`: `YawInvert`,
  `PitchInvert`, `RollInvert`, `XInvert`, `YInvert`, `ZInvert`. To swap axes, use
  `RotOrder=` / `PosOrder=`.
* **Strength:** set the curves in your tracker (Beam's sensitivity, or OpenTrack's Mapping
  window), or use `YawGain`, `PitchGain` and so on here.
* **Left/right balance:** `YawLeftGain` and `YawRightGain` add strength to one side only
  (for example `YawLeftGain=1.3` if looking left feels shorter than looking right). If left
  and right are swapped, set `YawLeftIsPositive=0`.
* **Range:** `Limit` caps the camera movement. The defaults keep the view inside the
  cabin. `Limit=0` turns an axis off.

## Troubleshooting

* **The view moves much more than your head, or keeps turning after you face forward
  (Beam):** Beam's "Immersive Camera" already amplifies head rotation and adds eye-gaze
  extension, so the view keeps panning while your eyes rest near the edge of the screen.
  Lower the camera sensitivity or turn off eye-based extension in Beam, or reduce
  `YawGain`/`PitchGain` here.
  Set `Diagnostics=1` and the log shows what arrives every 2 seconds
  (`diag freetrack: in yaw ...`). If `in yaw` keeps growing while your head is still, the
  extra movement comes from the tracker.

* **No log file:** the DLL was not loaded. Make sure FH2SimImmersion.exe was running
  before or while Azimuth started, and that antivirus did not block it.
* **"this game build is not recognised":** you have a different FH2 version than
  1.0.0.64. Nothing was changed.
* **"bind ... failed":** another program is already using UDP port 4242 (e.g. a second
  tracker). Change `UdpPort` here and in OpenTrack.
* **Patched, but no `layer` lines in cockpit view:** the game is not evaluating its
  head-tracking layers in your setup. Please report it, with the log.
* **Jitter:** improve the lighting on your face, use OpenTrack's Accela filter, or set
  `Smoothing=0.2`.

## Uninstall

Close FH2SimImmersion.exe and delete its folder. Nothing else was changed.

Upgrading from FH2HeadTracking: delete the old FH2HeadTracking.exe/.dll. Your
FH2HeadTracking.ini is renamed automatically.

## Building from source

```
src/dll/        plugin loaded into azimuth.exe (hook, tracker, config)
src/common/     shared headers: default settings text, car database
src/app/        FH2SimImmersion.exe: settings window and launcher (Win32, GDI pictures, app.rc/app.manifest)
tests/          pose unit tests, integration harness, fake azimuth.exe
docs/           reverse-engineering notes
```

**Windows (Visual Studio 2022 or MSYS2 MinGW64):**
```
cmake -S . -B build -A x64
cmake --build build --config Release
```

**Linux cross-build (mingw-w64):** run `./build.sh`. It builds both binaries and the harness,
runs the unit tests (pose, force feedback, settings parser), and packages `dist/`.

**Integration test.** `harness.exe` needs your own copy of the game executable; none is
included. It maps the real game executable, loads the DLL, sends OpenTrack packets and runs
the game's own `CameraLayer::EvaluateInput` against fake layers. Add `--deep` to exercise the
fallback memory scan. It runs on Windows or Wine:
```
harness.exe "<game>\Mount\forza_durango_release_final.exe" build\FH2SimImmersion.dll [--deep]
```

How the game was analysed, and every address and offset used, is documented in
[docs/REVERSE_ENGINEERING.md](docs/REVERSE_ENGINEERING.md) and `src/dll/game_layout.h`.
This repository contains no game code or data. Code is matched by hash and by an opcode skeleton.

## License

Public domain, under [The Unlicense](LICENSE). Anyone may use, copy, modify and
distribute it for any purpose, with no conditions.

Author: Dirceu da Silva Junior (see [AUTHORS](AUTHORS)).

Forza Horizon 2 is a trademark of Microsoft. This project is unofficial and not affiliated
with Microsoft, Playground Games, Turn 10 or Almost There Games (Azimuth).

# Controllers

NR Engine gives the game a digital PlayStation controller on port 1 and,
optionally, on port 2. Each port can be driven by the keyboard and by one
game controller at the same time. Everything is configured in the launcher's
**Controls** tab; the engine window has no controller menu.

## 1. Two ways to read a controller

The launcher lists every controller it can see, each tagged with how it is
read:

| Tag in the launcher | How it works | When to use it |
|---|---|---|
| **system driver** | SDL2 reads the controller through the operating system's driver (XInput, DirectInput/Raw Input or HID on Windows; evdev on Linux). Controllers known to SDL's controller database get the standard layout automatically (south button = ✕, east = ◯, west = ▢, north = △) | first choice for every controller: Xbox, PlayStation, Switch, 8BitDo, Logitech and most branded pads |
| **direct USB** | the engine opens the USB HID device itself (Windows: `hid.dll` + SetupAPI; Linux: `/dev/hidrawN`), parses its HID report descriptor and reads the raw reports | generic pads and PS1/PS2-to-USB adapters that the system driver maps badly, report the D-pad strangely, or are missing from SDL's database |
| **no permission** | the device exists but the current user cannot open its raw HID node (Linux) | install the udev rule (section 4) or use "system driver" |

"Automatic" (the default) uses the first controller seen through the system
driver for port 1 and the second for port 2.

Keyboards, mice and other non-game HID devices are never listed for direct
reading: only HID collections with the Generic Desktop usage *Joystick* or
*Game Pad* or *Multi-axis Controller* are considered.

## 2. Generic controllers most people own

Most cheap "USB gamepads" sold since the 2000s are built on a handful of
controller chips. These were the targets for the default layouts and the udev
rule:

| Vendor ID | Typical names | Notes |
|---|---|---|
| `0079` (DragonRise) | "Generic USB Joystick", "USB Gamepad", "PC TWIN SHOCK", many SNES/PS-style clones (`0079:0006`, `0079:0011`, `0079:181c`) | the most common generic chip. Different products share the same VID:PID but number their buttons differently, so SDL cannot tell them apart by ID and may choose a wrong layout ([SDL forum report](https://discourse.libsdl.org/t/duplicated-conflicting-guids-for-the-dragonrise-pc-twin-shock-controllers/24112)). Use direct USB or remap in the launcher |
| `0810` (Personal Communication Systems) | "Twin USB Joystick", PS1/PS2 to USB adapters (`0810:0001`, `0810:0003`) | two-port adapters appear as two devices |
| `0e8f` (GreenAsia / Mayflash) | "MaxFire", Mayflash PS/SNES adapters | |
| `0583` (Padix / Rockfire) | SNES-style USB pads (`0583:2060`) | D-pad reported as two axes |
| `12bd`, `2563` (ShanWan), `11c0`/`11c1` (Betop), `1a34` | unbranded pads, PS3-style clones | |
| `046d` (Logitech) | Dual Action (`c216`), RumblePad 2 (`c218`), F310/F510/F710 in DirectInput mode (`c219`) | well supported through the system driver |
| `054c` (Sony), `057e` (Nintendo), `2dc8` (8BitDo), `0f0d` (HORI), `0738` (Mad Catz), `044f` (Thrustmaster), `06a3` (Saitek), `0e6f` (PDP), `20d6` (PowerA), `2e24` (Hyperkin) | branded pads | normally best through the system driver |

Default layout used for unmapped and direct-USB generic pads (the numbering of
the classic "PC Twin Shock" boards; button numbers start at 0):

| PlayStation | Default binding |
|---|---|
| D-pad | hat 0, or axes 0/1 when the pad has no hat |
| △ ◯ ✕ ▢ | buttons 0, 1, 2, 3 |
| L2 R2 L1 R1 | buttons 4, 5, 6, 7 |
| Select / Start | buttons 8 / 9 |

Clones differ, so every button can be re-bound.

## 3. Binding buttons in the launcher

1. Open **Controls**, choose **Player 1** or **Player 2**.
2. Pick the device (or "Keyboard only" / "Automatic (first controller found)").
3. Click the *Controller* column of a PlayStation button and press the button
   (or move the stick/D-pad) you want. Click the *Keyboard* column to bind a key.
4. "Left stick also moves the D-pad" lets the analog stick steer (the game only reads
   digital buttons).
5. **Restore defaults** restores the standard bindings; **Save** writes
   `nrengine.ini`.

Binding names stored in `nrengine.ini` (`[pad1]`, `[pad2]`):

| Form | Meaning |
|---|---|
| `a`, `b`, `x`, `y`, `leftshoulder`, `dpup`, `lefttrigger`, `leftx-`… | SDL standard controller names (mapped controllers) |
| `b3` | raw button 3 |
| `a1-`, `a0+` | raw axis 1 negative, axis 0 positive |
| `h0.up` | hat 0, up |

The device is stored as `sdl:<GUID>@n` (system driver) or
`hid:<vid>:<pid>@n` (direct USB), where `n` distinguishes identical
controllers.

## 4. Linux: permission for direct USB reading

`/dev/hidrawN` nodes belong to root by default. The `.deb` package installs
`/usr/lib/udev/rules.d/70-nrengine-gamepads.rules`, which gives the user
logged in at the computer (systemd-logind `uaccess`) read access to hidraw
nodes **only** of the game-controller vendors listed above. Without the
package:

```sh
sudo cp packaging/linux/70-nrengine-gamepads.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger --subsystem-match=hidraw
```

Then unplug and plug the controller again. To allow another controller, add a
line with its vendor ID (shown in the launcher and by `lsusb`):

```
KERNEL=="hidraw*", ATTRS{idVendor}=="1234", TAG+="uaccess"
```

The system-driver path needs no rule: SDL2 reads `/dev/input/event*`, which
desktop distributions already open to the logged-in user.

## 5. Windows notes

- Direct USB reading uses the standard HID class driver that Windows loads for
  every USB gamepad; no extra driver (no libusb, no Zadig) is needed. Devices
  whose driver was replaced by WinUSB/libusb are not visible to HID.
- XInput controllers (Xbox) are exposed by Windows to HID without their
  triggers as separate axes; use them through the system driver.

## 6. Keyboard defaults

| PlayStation | Player 1 key |
|---|---|
| D-pad | arrow keys |
| ✕ / ◯ / ▢ / △ | Z / X / A / S |
| L1 / R1 / L2 / R2 | Q / W / E / R |
| Start / Select | Enter / Space |

Player 2 has no keys by default. Engine keys: **Esc** pause and quit dialog,
**F11** full screen, **F12** screenshot.

## 7. Validation status

The HID descriptor parser and the report decoding are covered by unit tests
with synthetic descriptors (a "PC Twin Shock"-style pad with 5 axes, a hat,
12 buttons and vendor padding; a game pad using report IDs). The system-driver path is SDL2's own code. **No physical
controller was connected during development of this release**, so direct USB
reading has not been exercised with real hardware; please report the
controller's name and VID:PID with the engine log if something does not work.

Sources: [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB)
(community mappings used by SDL), [SDL forum report on DragonRise layouts](https://discourse.libsdl.org/t/duplicated-conflicting-guids-for-the-dragonrise-pc-twin-shock-controllers/24112),
USB HID Usage Tables 1.4 (usage page 0x01, usages 0x04 Joystick, 0x05 Game
Pad and 0x08 Multi-axis Controller).

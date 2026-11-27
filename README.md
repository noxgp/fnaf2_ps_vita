# FNaF 2 for PS Vita

An unofficial PS Vita port of **Five Nights at Freddy's 2** (Android version 2.0.7, also works with 2.0.6).

It is not an emulator or a transpiler. It is a Clickteam Fusion runtime written from scratch in C. It loads the original game data from the Android APK and runs the game's events, animations, movements and collisions the way the official runtime does. Rendering uses vita2d on the GPU, and audio goes through a custom mixer.

**This repository contains no game files.** You need your own legally obtained copy of the FNaF 2 Android APK.

## Features

- Full game: all nights, Custom Night, minigames, cutscenes, endings, Options and subtitles
- Runs at 60 fps, stretched to the full Vita screen
- Touch controls and physical buttons
- Saves to `ux0:data/fnaf2/save`

## Controls

| Where | Button | Action |
|---|---|---|
| Office | ← / → | Look left / right |
| Office | □ / ○ | Left / right vent light |
| Office | ✕ | Hallway flashlight |
| Office | L | Freddy mask |
| Office | R | Open / close the camera monitor |
| Cameras | arrows | Switch cameras |
| Cameras | ✕ | Camera flashlight |
| Cameras | △ | Wind the music box (Prize Corner) |
| Minigames | arrows / left stick | Move |
| Anywhere | Select / Start | Mute button |

Everything else works with the touch screen, like on Android.

## Installation

You need a Vita with HENkaku/Ensō and VitaShell, plus a PC with **Python 3.8+** and **ffmpeg**. Windows, macOS and Linux all work.

1. **Get the APK.** Use the FNaF 2 Android APK from your own purchase.

   | Version | Status |
   |---|---|
   | **2.0.7** | ✅ Recommended (latest) |
   | 2.0.6 | ✅ Supported |
   | Other | ⚠️ Untested. The Data Tool builds it with a warning. Please report problems with your version number. |

2. **Build the game data on your PC.** Download `FNaF2_Vita_DataTool.zip` from [Releases](../../releases) and extract it. (The same files are in `tools/` here.)

   | System | Install Python + ffmpeg | Run |
   |---|---|---|
   | **Windows** | Python from [python.org](https://www.python.org/downloads/) (tick *Add python.exe to PATH*), then `winget install ffmpeg` | Drag the APK onto `Prepare-Game-Data.cmd` |
   | **macOS** | `brew install python ffmpeg` | `./prepare-game-data.sh fnaf2.apk` |
   | **Linux** | `sudo apt install python3 python3-venv ffmpeg` (or your distro's equivalent) | `./prepare-game-data.sh fnaf2.apk` |

   The script installs the Python packages it needs (on macOS/Linux into a private `.venv` folder) and creates `fnaf2_data/game/` (about 90 MB) in a minute or two. If ffmpeg isn't on your PATH, put `ffmpeg`/`ffmpeg.exe` next to the script or pass `--ffmpeg /path/to/ffmpeg`.

   Manual alternative on any system: `pip install -r requirements.txt` then `python build_data.py fnaf2.apk`.
3. **Copy the data to the Vita.** Copy the `game` folder to `ux0:data/fnaf2/`, so you end up with `ux0:data/fnaf2/game/application.ccn`. Watch out for a nested `game/game` folder.
4. **Install the VPK.** Install the latest `FNaF2_Vita_v*.vpk` from the [Releases](../../releases) page with VitaShell.

If the game says *"Game data not found"*, check the folder path from step 3.

The generated `game` folder contains the game's files. Keep it for yourself and don't share it.

## Changelog

**v1.0.2**
- Options > Display Mode: the 4:3 setting now keeps the original aspect ratio with black borders.
- Options > Vibrations: rumble on DualShock controllers on PlayStation TV (the handheld Vita has no vibration motor).
- Options > Joystick Size now also changes how far the arrow buttons push the minigame joystick.
- Office touch controls: turning left now works anywhere on the left side of the screen (before, after looking right, only the top and bottom of the left side worked).
- Start/Select only mutes the phone call while the mute button is on screen (it no longer turns the view left).
- Subtitle text is drawn in front of its dark background again.
- Mask: Toy Bonnie now stays in the office while you wear the mask (the screen flickers dark) and leaves after a few seconds. Before, he vanished instantly.

**v1.0.1**
- Support for FNaF 2 **v2.0.7** (latest Android version). Fixes a black screen with only "No" and the copyright text when the data was built from a 2.0.7 APK.
- Data Tool detects the APK version: 2.0.7 recommended, 2.0.6 supported, other versions build with a warning.
- Data Tool runs on Windows, macOS and Linux (`Prepare-Game-Data.cmd` / `prepare-game-data.sh`).
- Panorama (perspective) effect in the office now works on the Vita.
- Touch areas in the office follow the panorama view.

**v1.0**
- First release.

## Known limitations

- **Unlocks (Survival Kit) can't be bought.** On Android they're Google Play purchases, so the Vita shows "Network unavailable".
- **No vibration**, because the Vita has no rumble motor.
- **Japanese, Chinese and Korean subtitles don't display**, because the bundled font has no CJK glyphs.
- **The picture is wider than the original.** The game is 4:3 and is stretched to 16:9.

## Building from source

There are two targets. The **Vita build** needs [VitaSDK](https://vitasdk.org); the **PC build** is a headless Linux build used for testing and debugging.

```
make -f Makefile.vita        # build/fnaf2.vpk: engine + font, no game data
make                         # ./fp2 <datadir> [commands]: headless Linux test build
```

`make -f Makefile.vita DATA=fnaf2_data/game` builds an all-in-one VPK for personal use.

### Source layout

| Path | Contents |
|---|---|
| `src/load.c` | .ccn loader: frames, objects, events, image pack |
| `src/evt.c` | Event engine: conditions, actions, expressions, object selection |
| `src/run.c` | Frames, objects, animations, path/ball movement, drawing, global objects |
| `src/ext.c` | Extensions: Kyso, Perspective, Ini, MultipleTouch, Layer, StringParser, CalcRect, Clock |
| `src/audio.c` | Mixer: WAV/IMA ADPCM, OGG and MP3 |
| `src/font.c` | Text rendering (stb_truetype) |
| `src/plat_vita.c` | Vita backend: vita2d, GPU memory pool, input mapping, audio thread |
| `src/plat_soft.c` | Headless Linux backend for testing |
| `tools/build_data.py` | APK → Vita data: 8-bit palettized LZ4 texture pack, ADPCM audio (`Prepare-Game-Data.cmd` / `prepare-game-data.sh` wrap it) |
| `tools/dev/` | Analysis scripts: `evdump.py` dumps a frame's events in readable form |

### Third-party code

| Library | Used for | License |
|---|---|---|
| libvita2d | Rendering (additive blend changed to SRC_ALPHA, ONE) | MIT |
| LZ4 | Texture pack compression | BSD-2 |
| stb_truetype, stb_vorbis, stb_image | Fonts, OGG audio, images | Public domain / MIT |
| dr_mp3 | MP3 audio | Public domain / MIT-0 |
| DejaVu Sans | Font | Free license, see `third/dejavu/LICENSE` |

## Credits

- Port by **Nox**.
- *Five Nights at Freddy's 2* © Scott Cawthon. This is a non-commercial fan project, not affiliated with or endorsed by Scott Cawthon or Clickteam. Please buy the game.

# FNaF 2 for PS Vita

An unofficial PS Vita port of **Five Nights at Freddy's 2** (Android version 2.0.6).

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

You need a Vita with HENkaku/Ensō and VitaShell. You also need a PC with Python 3.8+ and ffmpeg.

1. **Get the APK.** Use the FNaF 2 Android APK version 2.0.6 from your own purchase.
2. **Build the game data on your PC.**
   ```
   pip install numpy pillow lz4 imagequant
   python tools/build_data.py fnaf2.apk
   ```
   This creates `fnaf2_data/game/` (about 90 MB) and takes a minute or two. `ffmpeg` must be on your PATH.
3. **Copy the data to the Vita.** Copy the `game` folder to `ux0:data/fnaf2/`, so you end up with `ux0:data/fnaf2/game/application.ccn`.
4. **Install the VPK.** Install `FNaF2_Vita.vpk` from the [Releases](../../releases) page with VitaShell.

If the game says *"Game data not found"*, check the folder path from step 3.

**Optional: a single VPK.** If you'd rather install one file instead of copying a folder, run:
```
python tools/build_data.py fnaf2.apk --vpk FNaF2_Vita.vpk
```
This also writes `fnaf2_data/FNaF2_Vita_full.vpk` with the data inside. Keep it for yourself. Don't share it, because it contains the game's files.

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
| `tools/build_data.py` | APK → Vita data: 8-bit palettized LZ4 texture pack, ADPCM audio |
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

- Port by **Nox**, developed with the help of Claude (Anthropic).
- *Five Nights at Freddy's 2* © Scott Cawthon. This is a non-commercial fan project, not affiliated with or endorsed by Scott Cawthon or Clickteam. Please buy the game.

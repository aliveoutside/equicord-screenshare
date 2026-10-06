# Equicord Screenshare

Share application audio and switch windows in official Discord on Linux Wayland. An experimental Equicord user plugin, tested on KDE Wayland.

## Install

Open a terminal and paste:

```bash
curl -fsSL https://raw.githubusercontent.com/aliveoutside/equicord-screenshare/main/setup.sh | bash
```

The installer downloads Equicord and this plugin, installs missing build tools, builds everything, and connects it to Discord. You do not need to know or install Node.js or pnpm yourself. It may ask for your password to install system packages. Run it as your normal user, without `sudo`.

When it finishes, **fully quit Discord, including its tray icon, and reopen it**. WaylandScreenshare is enabled by default. Start a share with **Sound** enabled and choose the audio you want in the picker.

The first installation downloads and builds software, so it can take several minutes. This installs a custom Equicord build into Discord. If you already use another Discord modification, this installation replaces its Discord bootstrap. The managed build will use a separate source checkout, and updates are installed with the command below.

Supported automatic setup: **Arch/CachyOS, Debian/Ubuntu and Fedora on Linux x64**. You need official Discord, a Wayland session, PipeWire with PulseAudio compatibility, and a working ScreenCast desktop portal. Launch Discord at least once before installing. Flatpak and Snap Discord, Vesktop, Equibop, Windows, macOS and ARM are not supported by this installer.

## Update

Paste this in a terminal:

```bash
~/.local/bin/wayland-screenshare update
```

This downloads updates for both Equicord and the plugin, then rebuilds and installs them. Fully restart Discord afterward. If your terminal includes `~/.local/bin` in its command search path, you can also use:

```bash
wayland-screenshare update
```

You can rerun the original installation command if the update shortcut is missing or needs repair. Local changes to source files stop automatic updates rather than being overwritten.

## Use

- The audio picker opens when you start your own screenshare and after switching sources.
- **Automatic** shares a uniquely matching application when the compositor provides enough information. Missing or ambiguous metadata leaves the plugin capture silent.
- Choose an application to follow all its playback streams, or choose an individual stream to capture just that stream.
- On KDE, **Identify window audio** lets you click the shared window when automatic identification is unavailable.
- **Desktop audio** shares desktop playback, excluding Discord and the plugin's own capture paths. Recognized monitor names also select desktop audio in Automatic mode.
- **No audio** produces silence through the plugin recorder.
- Use Discord's **Change Windows** action to select another window or monitor while streaming. Cancellation keeps the previous capture.

Reopen the audio picker from plugin settings, Toolbox, or the stream context menu. Application selections follow recreated playback streams from the same running process. Restarted applications require selecting them again. Capturing audio adds a separate path and does not intentionally move playback away from your headphones or speakers.

## Troubleshooting

**Discord was not found:** launch official Discord once, then rerun setup. To choose a different Discord configuration directory:

```bash
curl -fsSL https://raw.githubusercontent.com/aliveoutside/equicord-screenshare/main/setup.sh | bash -s -- --discord /path/to/discord/config
```

The installer remembers this location for updates. Its default is `${XDG_CONFIG_HOME:-$HOME/.config}/discord`.

**No application audio:** check that Sound is enabled in Discord, then select the application manually. On KDE, try Identify window audio. Apps restarted since selection must be selected again.

**An incompatible interface error after a Discord update:** the audio hook uses internal Discord functions. There is no whole-file hash or version pin, but the native entry points are checked before being used. Removing the pin does not make every Discord voice module compatible. Run the update command to obtain fixes when available. If audio setup fails, do not assume Discord's normal soundshare recorder is isolated to your selection.

**PipeWire restarted:** stop sharing and start a new share.

**EasyEffects or GPU problems:** routing changes and GPU capture failures have occurred during testing. Compatibility with every audio graph and GPU configuration is not established. Setup does not change your hardware acceleration settings or move application playback.

## How it is installed

The plugin lives in its own repository and downloads the official Equicord source automatically. It is an Equicord user plugin, not a standalone Discord application. Vencord compatibility has not been verified.

Everything it builds is stored under `${XDG_DATA_HOME:-$HOME/.local/share}/equicord-screenshare`, with a small update launcher at `~/.local/bin/wayland-screenshare`. Node.js and pnpm are installed privately there. Only missing system build packages need administrator access. The Node download is checked against the checksums published by nodejs.org. Keep the installation directory in place: Discord's custom build points to it.

The terminal command downloads and runs this repository's setup script. To review it first, open [setup.sh](setup.sh). Automatic Equicord updates are disabled in favor of the update command above.

## Existing Equicord source checkout

Developers who already build Equicord can clone this repository into `src/userplugins/waylandScreenshare` and run:

```bash
bash src/userplugins/waylandScreenshare/install.sh
```

Use `--build-only` to compile without installing into Discord, or `--discord PATH` to select a different Discord configuration directory. Native source builds need a C++17 compiler, `pkg-config`, Node development headers and development libraries for GIO, PipeWire and PulseAudio. Headers are taken from the running Node installation; `NODE_INCLUDE_DIR` can override their location.

## Compatibility and license

Tested on KDE Wayland with official Discord 1.0.160. Other compositors can use manual audio selection, but have not been verified. Interactive window identification requires KDE. Native hooks remain loaded until Discord exits, so every upgrade needs a full restart.

GPL-3.0-or-later. See [LICENSE](LICENSE). Existing Vencord and contributor attribution is preserved. Generated native addons and Discord binaries are not included in this repository.

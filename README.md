# Equicord Screenshare

Share application audio and switch windows without restarting your Discord stream. For official Discord on Linux Wayland, tested on KDE with PipeWire.

## Install

Open a terminal and paste:

```bash
curl -fsSL https://raw.githubusercontent.com/aliveoutside/equicord-screenshare/main/setup.sh | bash
```

Everything is downloaded and built for you. It can take a few minutes and may ask for your password to install build tools. Run it without `sudo`.

When it finishes, **quit Discord completely, including the tray icon, and reopen it**.

The installer supports Arch/CachyOS, Debian/Ubuntu and Fedora on x64. Launch Discord at least once before installing. Flatpak and Snap installs aren't supported. This installs Equicord and replaces any existing Discord mod installation.

## Update

```bash
~/.local/bin/wayland-screenshare update
```

Restart Discord afterward.

## Use

Start a screenshare with **Sound** enabled. The audio picker opens automatically; choose an application, desktop audio, or no audio. If automatic matching misses your app, select it manually or use **Identify window audio** on KDE.

Use **Change Windows** in Discord's stream menu to switch the shared window or monitor. You can reopen the audio picker from plugin settings or Toolbox.

Still experimental. Discord updates can break the native hook, and EasyEffects routing and GPU capture have had problems during testing.

GPL-3.0-or-later. See [LICENSE](LICENSE).

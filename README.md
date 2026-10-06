# Equicord Screenshare

Share application audio and switch windows without restarting your Discord stream.

![Switch the shared window or monitor from Discord's stream menu](.github/1.png)

![Choose application audio in the screenshare audio picker](.github/2.png)

## Install

Open a terminal and paste:

```bash
curl -fsSL https://raw.githubusercontent.com/aliveoutside/equicord-screenshare/main/setup.sh | bash
```

**Already using Equicord?** This installer replaces the build Discord loads with Equicord plus this plugin. You no longer need to build or update your old copy separately. Use the update command below from now on.

When it finishes, **restart discord**.

The installer supports Arch/CachyOS, Debian/Ubuntu and Fedora on x64. Flatpak and Snap installs aren't supported. This installs Equicord and replaces any existing Discord mod installation.

## Update

```bash
equicord-screenshare update
```

Restart Discord afterward.

## Uninstall

```bash
equicord-screenshare uninstall
```

Removes Equicord from Discord. Downloaded files stay so you can reinstall later.

## OpenAsar

Optional, using the official Equicord installer:

```bash
equicord-screenshare install-openasar
equicord-screenshare uninstall-openasar
```

## Use

Start a screenshare with **Sound** enabled. Audio is selected automatically when your app or monitor is recognized. Otherwise, the picker opens so you can select an application or desktop audio


Highly experimental.

GPL-3.0-or-later. See [LICENSE](LICENSE).

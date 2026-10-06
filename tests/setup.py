#!/usr/bin/env python3
# Vencord, a Discord client mod
# Copyright (c) 2026 Vendicated and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import subprocess
import tempfile

setup = Path(__file__).resolve().parents[1] / "setup.sh"
stub = r"""#!/usr/bin/env python3
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tarfile

name = Path(sys.argv[0]).name
args = sys.argv[1:]
root = Path(os.environ['TEST_ROOT'])
with (root / 'calls').open('a') as log:
    log.write(json.dumps([name, *args]) + '\n')
if name == 'pkg-config':
    sys.exit(0 if (root / 'packages').exists() else 1)
if name == 'sudo':
    (root / 'packages').touch()
elif name == 'git':
    if args[0] == 'clone':
        target = Path(args[-1])
        (target / '.git').mkdir(parents=True)
        if 'Equicord.git' in args[-2]:
            (target / 'package.json').write_text('{"packageManager":"pnpm@12.8.1"}')
        else:
            (target / 'setup.sh').write_bytes(Path(os.environ['TEST_SETUP']).read_bytes())
            (target / 'install.sh').write_text('#!/bin/bash\nprintf "%s\\n" "$*" >> "$TEST_ROOT/install-calls"\nexit "${TEST_INSTALL_EXIT:-0}"\n')
    elif 'status' in args and (root / 'dirty').exists():
        print(' M source.cpp')
elif name == 'node':
    if args and args[0] == '-p':
        print('12.8.1')
elif name == 'pnpm':
    print('12.8.1')
elif name == 'npm':
    prefix = Path(args[args.index('--prefix') + 1])
    (prefix / 'bin').mkdir(exist_ok=True)
    target = prefix / 'bin/pnpm'
    target.write_bytes(Path(__file__).read_bytes())
    target.chmod(0o755)
elif name == 'curl':
    destination = Path(args[args.index('-o') + 1])
    archive = root / 'node-v24.0.0-linux-x64.tar.xz'
    if not archive.exists():
        with tarfile.open(archive, 'w:xz') as bundle:
            for path, data, mode in [('bin/node', Path(__file__).read_bytes(), 0o755), ('bin/npm', Path(__file__).read_bytes(), 0o755), ('include/node/node_api.h', b'', 0o644)]:
                info = tarfile.TarInfo('node-v24.0.0-linux-x64/' + path)
                info.size = len(data)
                info.mode = mode
                bundle.addfile(info, io.BytesIO(data))
    if destination.name == 'SHASUMS256.txt':
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        if os.environ.get('TEST_BAD_CHECKSUM'):
            digest = '0' * 64
        destination.write_text(digest + '  ' + archive.name + '\n')
    else:
        destination.write_bytes(archive.read_bytes())
"""


def fixture(directory):
    root = Path(directory)
    home = root / "home with spaces"
    discord = root / "Discord config"
    discord.mkdir()
    (discord / "Discord").touch()
    tools = root / "tools"
    tools.mkdir()
    for name in ["git", "curl", "g++", "pkg-config", "make", "sudo"]:
        path = tools / name
        path.write_text(stub)
        path.chmod(0o755)
    env = dict(
        os.environ,
        HOME=str(home),
        XDG_DATA_HOME=str(root / "data with spaces"),
        XDG_CONFIG_HOME=str(root / "config"),
        PATH=str(tools) + ":" + os.environ["PATH"],
        TEST_ROOT=str(root),
        TEST_SETUP=str(setup),
    )
    (root / "packages").touch()
    return root, home, discord, env


def run(script, args, env):
    return subprocess.run(
        ["bash", str(script), *args], env=env, capture_output=True, text=True, timeout=30
    )


with tempfile.TemporaryDirectory(prefix="screenshare-setup-test-") as directory:
    root, home, discord, env = fixture(directory)
    legacy = home / ".local/bin/wayland-screenshare"
    legacy.parent.mkdir(parents=True)
    legacy.write_bytes(setup.read_bytes())
    result = run(setup, ["--discord", str(discord)], env)
    assert result.returncode == 0, result.stdout + result.stderr
    managed = Path(env["XDG_DATA_HOME"]) / "equicord-screenshare"
    assert legacy.is_symlink()
    assert legacy.resolve() == managed / "Equicord/src/userplugins/waylandScreenshare/setup.sh"
    assert legacy.read_bytes() == setup.read_bytes()
    assert (managed / "tools/node/include/node/node_api.h").is_file()
    assert (managed / "tools/bin/pnpm").is_file()
    launcher = home / ".local/bin/equicord-screenshare"
    assert launcher.is_symlink() and os.access(launcher, os.X_OK)
    assert launcher.resolve() == legacy.resolve()
    assert (managed / "discord-path").read_text().strip() == str(discord)
    assert (root / "install-calls").read_text().count("--discord " + str(discord)) == 1
    (root / "calls").write_text("")
    result = run(launcher, ["update"], env)
    assert result.returncode == 0, result.stdout + result.stderr
    calls = [json.loads(line) for line in (root / "calls").read_text().splitlines()]
    assert all(call[0] not in ("curl", "npm") and "clone" not in call for call in calls)
    assert sum("pull" in call for call in calls) == 2
    assert (root / "install-calls").read_text().count("--discord " + str(discord)) == 2
    (root / "dirty").touch()
    result = run(launcher, ["update"], env)
    assert result.returncode != 0 and "Local source changes" in result.stderr
    assert (root / "install-calls").read_text().count("--discord") == 2
    for action in ("uninstall", "install-openasar", "uninstall-openasar"):
        (root / "calls").write_text("")
        result = run(launcher, [action], env)
        assert result.returncode == 0, result.stdout + result.stderr
        assert (root / "calls").read_text() == ""
        assert f"--{action} --discord {discord}" in (root / "install-calls").read_text()
    (root / "dirty").unlink()
    env["TEST_INSTALL_EXIT"] = "7"
    result = run(launcher, ["update"], env)
    assert result.returncode == 7 and "Setup stopped" in result.stderr
    assert "Installed." not in result.stdout
    for args in [["--discord"], ["--unknown"], ["--discord", str(root / "missing")]]:
        assert run(setup, args, env).returncode != 0

with tempfile.TemporaryDirectory(prefix="screenshare-packages-test-") as directory:
    root, home, discord, env = fixture(directory)
    (root / "packages").unlink()
    result = run(setup, ["--discord", str(discord)], env)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "sudo" in (root / "calls").read_text()

with tempfile.TemporaryDirectory(prefix="screenshare-checksum-test-") as directory:
    root, home, discord, env = fixture(directory)
    env["TEST_BAD_CHECKSUM"] = "1"
    result = run(setup, ["--discord", str(discord)], env)
    assert result.returncode != 0
    assert not (Path(env["XDG_DATA_HOME"]) / "equicord-screenshare/tools/node").exists()
    assert not (root / "install-calls").exists()

with tempfile.TemporaryDirectory(prefix="screenshare-symlink-test-") as directory:
    root, home, discord, env = fixture(directory)
    result = run(setup, ["--discord", str(discord)], env)
    assert result.returncode == 0, result.stdout + result.stderr
    launcher = home / ".local/bin/equicord-screenshare"
    target = launcher.resolve()
    target.write_text(setup.read_text() + "\n")
    assert launcher.read_bytes() == target.read_bytes()
    result = run(launcher, ["update"], env)
    assert result.returncode == 0, result.stdout + result.stderr
    assert launcher.is_symlink() and launcher.resolve() == target

print(
    "Setup checks passed: install, update, symlink shortcuts, management commands, private tools, paths with spaces and failure handling. No network or system changes were made."
)

#!/usr/bin/env python3
# Vencord, a Discord client mod
# Copyright (c) 2026 Vendicated and contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import json
import os
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).resolve().parents[1] / "install.sh"
stub = r"""#!/usr/bin/env python3
import json
import os
from pathlib import Path
import sys

name = Path(sys.argv[0]).name
args = sys.argv[1:]
root = Path(os.environ['TEST_ROOT'])
with (root / 'calls').open('a') as log:
    log.write(json.dumps([name, *args]) + '\n')
if name == 'node':
    if args and args[0] == '-p':
        print(os.environ['TEST_HEADERS'])
    elif args and args[0] == '--input-type=module':
        sys.exit(int(os.environ.get('TEST_VERIFY_EXIT', '0')))
    elif args and args[0] == 'scripts/runInstaller.mjs':
        if not os.environ.get('TEST_DOWNLOAD_FAIL'):
            installer = Path('dist/Installer/EquilotlCli-Linux')
            installer.parent.mkdir(parents=True, exist_ok=True)
            installer.write_bytes(Path(__file__).read_bytes())
            installer.chmod(0o755)
elif name == 'EquilotlCli-Linux':
    assert os.environ['EQUICORD_DEV_INSTALL'] == '1'
    assert os.environ['EQUICORD_DIRECTORY'].endswith('/dist/desktop')
    sys.exit(int(os.environ.get('TEST_INSTALLER_EXIT', '0')))
"""

with tempfile.TemporaryDirectory(prefix="screenshare-installer-test-") as directory:
    root = Path(directory)
    repo = root / "Equicord checkout"
    plugin = repo / "src/userplugins/waylandScreenshare"
    plugin.mkdir(parents=True)
    script = plugin / "install.sh"
    script.write_bytes(source.read_bytes())
    (repo / "package.json").write_text("{}")
    (repo / "scripts").mkdir()
    (repo / "scripts/runInstaller.mjs").touch()
    discord = root / "Discord config"
    discord.mkdir()
    (discord / "Discord").touch()
    headers = root / "headers"
    headers.mkdir()
    (headers / "node_api.h").touch()
    tools = root / "tools"
    tools.mkdir()
    for name in ("node", "pnpm", "pkg-config", "g++"):
        tool = tools / name
        tool.write_text(stub)
        tool.chmod(0o755)
    env = dict(
        os.environ,
        PATH=str(tools) + ":" + os.environ["PATH"],
        TEST_ROOT=str(root),
        TEST_HEADERS=str(headers),
    )
    installer = repo / "dist/Installer/EquilotlCli-Linux"

    def run(*args):
        (root / "calls").write_text("")
        result = subprocess.run(
            ["bash", str(script), *args, "--discord", str(discord)],
            env=env,
            capture_output=True,
            text=True,
            timeout=20,
        )
        calls = [json.loads(line) for line in (root / "calls").read_text().splitlines()]
        return result, calls

    result, calls = run("--build-only")
    assert result.returncode == 0, result.stderr
    assert ["pnpm", "install", "--frozen-lockfile"] in calls
    assert ["pnpm", "build", "--disable-updater"] in calls
    assert not installer.exists()
    env["TEST_DOWNLOAD_FAIL"] = "1"
    result, calls = run("--uninstall")
    assert result.returncode != 0 and "Could not download" in result.stderr
    assert not installer.exists()
    del env["TEST_DOWNLOAD_FAIL"]
    for action in ("uninstall", "install-openasar", "uninstall-openasar"):
        result, calls = run("--" + action)
        assert result.returncode == 0, result.stderr
        assert ["EquilotlCli-Linux", "--" + action, "--location", str(discord)] in calls
        assert not any(call[0] in ("pnpm", "pkg-config", "g++") for call in calls)
        checks = [call for call in calls if "--input-type=module" in call]
        assert len(checks) == (1 if action == "uninstall" else 0)
        if checks:
            assert checks[0][-1] == "uninstall"
    env["TEST_INSTALLER_EXIT"] = "7"
    result, calls = run("--uninstall")
    assert result.returncode == 7
    assert not any("--input-type=module" in call for call in calls)
    assert "Done." not in result.stdout
    env["TEST_INSTALLER_EXIT"] = "0"
    env["TEST_VERIFY_EXIT"] = "9"
    result, calls = run("--uninstall")
    assert result.returncode == 9 and "Done." not in result.stdout
    result, calls = run("--uninstall", "--build-only")
    assert result.returncode == 2

print(
    "Installer checks passed: build-only, official installer download, uninstall, OpenAsar actions, exit codes and verification failures. Discord was not modified."
)

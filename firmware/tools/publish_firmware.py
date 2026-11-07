#!/usr/bin/env python3
"""Publish the current build as the firmware bulbs download.

Commit, build, then run from an activated ESP-IDF shell:

    idf.py reconfigure build     # plain `build` can keep the old version number
    python tools/publish_firmware.py

It refuses a build that has uncommitted changes, doesn't match the current
commit or isn't signed with your key. The .bin and .elf are kept in
releases/<version>/, because reading a bulb's crash report later needs the
matching .elf.

Publishing doesn't update any bulb. Each one updates when told to:

    mosquitto_pub ... -t pico/bulb/<id>/cmd -m '{"cmd":"ota"}'
"""
import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parent.parent
PROJECT = "pico_bulb"
SIGNING_KEY = FIRMWARE / "keys" / "signing.pem"
PUBLISHED_NAME = "pico-bulb.bin"
MAX_VERSION_LENGTH = 31  # esp_app_desc_t::version holds this many characters


def git_version() -> str:
    """The version ESP-IDF gives a build of the current source."""
    result = subprocess.run(["git", "describe", "--always", "--tags", "--dirty"],
                            cwd=FIRMWARE, check=True, capture_output=True, text=True)
    return result.stdout.strip()


def problem_with(build: Path) -> str | None:
    """Why this build shouldn't be published, or None."""
    description = build / "project_description.json"
    binary = build / f"{PROJECT}.bin"
    elf = build / f"{PROJECT}.elf"
    if not (description.is_file() and binary.is_file() and elf.is_file()):
        return f"no complete build in {build}: run `idf.py reconfigure build`"

    built = json.loads(description.read_text())["project_version"]
    current = git_version()
    if current.endswith("-dirty"):
        return "there are uncommitted changes: commit first, so the version says what's in it"
    if built != current:
        return f"the build is version {built} but the source is {current}: run `idf.py reconfigure build`"
    if len(built) > MAX_VERSION_LENGTH:
        return f"version {built} is longer than {MAX_VERSION_LENGTH} characters, so bulbs would misread it"

    check = subprocess.run([sys.executable, "-m", "espsecure", "verify-signature", "--version", "2",
                            "--keyfile", str(SIGNING_KEY), str(binary)], capture_output=True, text=True)
    if check.returncode != 0:
        return f"{binary.name} isn't signed with {SIGNING_KEY}, so bulbs would reject it"
    return None


def archive(build: Path, version: str) -> Path:
    """Keep this release's files, and return the one to upload."""
    folder = FIRMWARE / "releases" / version
    folder.mkdir(parents=True, exist_ok=True)
    for name in (f"{PROJECT}.bin", f"{PROJECT}.elf"):
        shutil.copy2(build / name, folder / name)
    return folder / f"{PROJECT}.bin"


def upload(binary: Path, server: str, folder: str) -> None:
    """Rename into place at the end, so a bulb can never download a half-written file."""
    final = f"{folder}/{PUBLISHED_NAME}"
    partial = f"{folder}/.{PUBLISHED_NAME}.partial"
    subprocess.run(["scp", "-q", str(binary), f"{server}:{partial}"], check=True)
    remote = f"chmod 644 {shlex.quote(partial)} && mv -f {shlex.quote(partial)} {shlex.quote(final)}"
    subprocess.run(["ssh", server, remote], check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--server", default="tsp@mqtt.apicov.xyz", help="SSH login (default: %(default)s)")
    parser.add_argument("--folder", default="/var/www/pico-firmware", help="server folder (default: %(default)s)")
    parser.add_argument("--build-dir", type=Path, default=FIRMWARE / "build", help="default: build/")
    args = parser.parse_args()

    if "IDF_PATH" not in os.environ:
        print("Activate ESP-IDF first: IDF_PATH isn't set.", file=sys.stderr)
        return 1

    problem = problem_with(args.build_dir)
    if problem:
        print(f"Can't publish: {problem}.", file=sys.stderr)
        return 1

    version = git_version()
    binary = archive(args.build_dir, version)
    try:
        upload(binary, args.server, args.folder)
    except subprocess.CalledProcessError as error:
        print(f"Upload failed ({error.cmd[0]} exited with {error.returncode}). "
              "The published firmware hasn't changed.", file=sys.stderr)
        return 1
    print(f"Published {version}. No bulb changes until you send it {{\"cmd\":\"ota\"}}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

#!/usr/bin/env python3

import argparse
import json
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
GOOGLETEST = ROOT / "external" / "googletest"
PLATFORM_NATIVE = ROOT / "external" / "platform-native"
PORTDUINO_INI = ROOT / "external" / "meshtastic" / "variants" / "native" / "portduino.ini"

GOOGLETEST_VERSION = "1.17.0"
PLATFORM_ZIP = "https://github.com/meshtastic/platform-native/archive/86c62edfb7084d11669aa255411a70580e83823b.zip"

LIBRARY_JSON = {
    "name": "googletest",
    "version": GOOGLETEST_VERSION,
    "description": "Google Testing and Mocking Framework",
    "license": "BSD-3-Clause",
    "frameworks": "*",
    "platforms": "*",
    "build": {
        "flags": [
            "-I googletest/include",
            "-I googletest",
            "-I googlemock/include",
            "-I googlemock",
        ],
        "srcFilter": ["+<googletest/src/gtest-all.cc>", "+<googlemock/src/gmock-all.cc>"],
    },
}

PIOPM = {
    "type": "library",
    "name": "googletest",
    "version": GOOGLETEST_VERSION,
    "spec": {
        "owner": "google",
        "id": 12747,
        "name": "googletest",
        "requirements": "^" + GOOGLETEST_VERSION,
        "uri": None,
    },
}


def require(path: pathlib.Path) -> None:
    if not path.exists():
        sys.exit(f"{path} is missing, run: git submodule update --init --depth 1")


def vendor_googletest(env: str) -> pathlib.Path:
    require(GOOGLETEST / "googletest" / "src" / "gtest-all.cc")

    dest = ROOT / ".pio" / "libdeps" / env / "googletest"
    if dest.exists():
        shutil.rmtree(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)

    shutil.copytree(GOOGLETEST, dest, ignore=shutil.ignore_patterns(".git"))
    (dest / "library.json").write_text(json.dumps(LIBRARY_JSON, indent=2) + "\n")
    (dest / ".piopm").write_text(json.dumps(PIOPM) + "\n")

    return dest


def point_portduino_at_the_submodule() -> pathlib.Path:
    require(PLATFORM_NATIVE / "platform.json")
    require(PORTDUINO_INI)

    text = PORTDUINO_INI.read_text()
    local = "symlink://" + str(PLATFORM_NATIVE)

    if local in text:
        return PORTDUINO_INI
    if PLATFORM_ZIP not in text:
        sys.exit(f"{PORTDUINO_INI} no longer pins {PLATFORM_ZIP}, refusing to guess")

    PORTDUINO_INI.write_text(text.replace(PLATFORM_ZIP, local))
    return PORTDUINO_INI


def main() -> None:
    parser = argparse.ArgumentParser(description="Use the vendored submodules instead of downloading")
    parser.add_argument("--env", default="native")
    parser.add_argument("--portduino", action="store_true")
    args = parser.parse_args()

    print(f"vendored googletest into {vendor_googletest(args.env)}")

    if args.portduino:
        print(f"pointed {point_portduino_at_the_submodule()} at {PLATFORM_NATIVE}")


if __name__ == "__main__":
    main()

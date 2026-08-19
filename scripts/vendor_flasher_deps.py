#!/usr/bin/env python3

import argparse
import hashlib
import io
import pathlib
import sys
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
VENDOR = ROOT / "tools" / "flasher" / "vendor"

PACKAGE = "esptool-js"
VERSION = "0.6.1"
TARBALL = f"https://registry.npmjs.org/{PACKAGE}/-/{PACKAGE}-{VERSION}.tgz"

BUNDLE_SHA256 = "ef7d5a237d3f273ecf546bcee65dddad90bd82cf02f22a980d1537e0cd79a152"
BUNDLE_TARGET = VENDOR / "esptool-js.js"
LICENSE_TARGET = VENDOR / "LICENSE"

REQUIRED_EXPORTS = ["ESPLoader", "Transport"]


def fetch_members() -> dict:
    with urllib.request.urlopen(TARBALL) as response:
        payload = response.read()

    members = {}
    with tarfile.open(fileobj=io.BytesIO(payload), mode="r:gz") as archive:
        for name in ("package/bundle.js", "package/LICENSE"):
            handle = archive.extractfile(name)
            if handle is None:
                sys.exit(f"{name} missing from {PACKAGE}@{VERSION}")
            members[name] = handle.read()

        names = archive.getnames()

    if "package/lib/targets/esp32s3.js" not in names:
        sys.exit(f"{PACKAGE}@{VERSION} has no ESP32-S3 target; refusing to vendor it")

    return members


def check_exports(bundle: bytes) -> None:
    text = bundle.decode("utf-8", errors="replace")
    missing = [name for name in REQUIRED_EXPORTS if f" as {name}" not in text and f"{name} " not in text]
    if missing:
        sys.exit(f"vendored bundle does not export {', '.join(missing)}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Vendor the esptool-js bundle used by the browser flasher")
    parser.add_argument("--verify", action="store_true", help="check the committed bundle instead of downloading")
    args = parser.parse_args()

    if args.verify:
        if not BUNDLE_TARGET.exists():
            sys.exit(f"missing {BUNDLE_TARGET.relative_to(ROOT)}\nrun: python3 scripts/vendor_flasher_deps.py")
        payload = BUNDLE_TARGET.read_bytes()
        digest = hashlib.sha256(payload).hexdigest()
        check_exports(payload)
        if digest != BUNDLE_SHA256:
            sys.exit(
                f"{BUNDLE_TARGET.relative_to(ROOT)} does not match the pin\n"
                f"  expected {BUNDLE_SHA256}\n  actual   {digest}"
            )
        print(f"{PACKAGE}@{VERSION} bundle verified ({len(payload)} bytes)")
        return

    members = fetch_members()
    bundle = members["package/bundle.js"]
    check_exports(bundle)

    VENDOR.mkdir(parents=True, exist_ok=True)
    BUNDLE_TARGET.write_bytes(bundle)
    LICENSE_TARGET.write_bytes(members["package/LICENSE"])

    digest = hashlib.sha256(bundle).hexdigest()
    print(f"vendored {PACKAGE}@{VERSION} bundle ({len(bundle)} bytes)")
    print(f"sha256 {digest}")
    if digest != BUNDLE_SHA256:
        print(f"\nupdate BUNDLE_SHA256 in {pathlib.Path(__file__).name} to the value above")


if __name__ == "__main__":
    main()

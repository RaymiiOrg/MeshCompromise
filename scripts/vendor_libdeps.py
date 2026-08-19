#!/usr/bin/env python3

import argparse
import json
import pathlib
import re
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESHTASTIC = ROOT / "external" / "meshtastic"
LIBDEPS = ROOT / "external" / "libdeps"

ARCHIVE = re.compile(r"^https://github\.com/([^/]+)/([^/]+)/archive/(?:refs/tags/)?(.+)\.zip$")
REGISTRY = re.compile(r"^([^/]+)/(.+)@(.+)$")
COMMIT = re.compile(r"^[0-9a-f]{40}$")

REGISTRY_SOURCES = {
    "lovyan03/LovyanGFX": ("lovyan03", "LovyanGFX"),
    "adafruit/Adafruit seesaw Library": ("adafruit", "Adafruit_Seesaw"),
    "melopero/Melopero RV3028": ("melopero", "Melopero_RV-3028_Arduino_Library"),
}

TRANSITIVE = {
    "adafruit/Adafruit ST7735 and ST7789 Library": ("adafruit", "Adafruit-ST7735-Library", "1.9.3", "1.9.3"),
    "arduino-libraries/SD": ("arduino-libraries", "SD", "1.3.0", "1.3.0"),
    "throwtheswitch/Unity": ("ThrowTheSwitch", "Unity", "v2.6.1", "2.6.1"),
}

ALREADY_VENDORED = {
    "RadioLib": ROOT / "external" / "radiolib",
    "Crypto": ROOT / "external" / "crypto",
    "nanopb": ROOT / "external" / "nanopb",
}


class Dep:
    def __init__(self, url: str, owner: str, repo: str, ref: str, registry: str = "", version: str = ""):
        self.url = url
        self.owner = owner
        self.repo = repo
        self.ref = ref
        self.registry = registry
        self.version = version

    @property
    def path(self) -> pathlib.Path:
        return ALREADY_VENDORED.get(self.repo, LIBDEPS / self.repo)

    @property
    def remote(self) -> str:
        return f"https://github.com/{self.owner}/{self.repo}.git"

    @property
    def shared(self) -> bool:
        return self.repo in ALREADY_VENDORED


def run(args, cwd=None, check=True) -> subprocess.CompletedProcess:
    return subprocess.run(args, cwd=cwd, check=check, capture_output=True, text=True)


def resolve_deps(env: str) -> list:
    raw = run(["pio", "project", "config", "--json-output"], cwd=MESHTASTIC).stdout
    sections = dict((name, dict(options)) for name, options in json.loads(raw))

    if f"env:{env}" not in sections:
        sys.exit(f"{MESHTASTIC} has no [env:{env}]")

    deps = []
    for entry in sections[f"env:{env}"].get("lib_deps", []):
        entry = entry.strip()

        archive = ARCHIVE.match(entry)
        if archive:
            deps.append(Dep(entry, *archive.groups()))
            continue

        listing = REGISTRY.match(entry)
        if not listing:
            continue

        spec = f"{listing.group(1)}/{listing.group(2)}"
        source = REGISTRY_SOURCES.get(spec)
        if source is None:
            sys.exit(f"{entry} is a registry package with no GitHub source in REGISTRY_SOURCES")
        deps.append(Dep(entry, source[0], source[1], listing.group(3), registry=spec))

    for spec, (owner, repo, ref, version) in TRANSITIVE.items():
        deps.append(Dep(f"{spec}@{version}", owner, repo, ref, registry=spec, version=version))

    return deps


def manifest_json(path: pathlib.Path) -> dict:
    manifest = path / "library.json"
    if not manifest.exists():
        return {}
    try:
        parsed = json.loads(manifest.read_text())
    except (json.JSONDecodeError, UnicodeDecodeError):
        return {}
    return parsed if isinstance(parsed, dict) else {}


def manifest_property(path: pathlib.Path, key: str) -> str:
    properties = path / "library.properties"
    if not properties.exists():
        return ""
    for line in properties.read_text(errors="replace").splitlines():
        if line.startswith(f"{key}="):
            return line.split("=", 1)[1].strip()
    return ""


def package_name(dep: "Dep") -> str:
    if dep.registry:
        return dep.registry.split("/", 1)[1]
    return manifest_json(dep.path).get("name") or manifest_property(dep.path, "name") or dep.repo


def package_version(dep: "Dep") -> str:
    if dep.version:
        return dep.version
    return str(manifest_json(dep.path).get("version") or manifest_property(dep.path, "version") or "0.0.0")


def usable_manifest(path: pathlib.Path) -> bool:
    if (path / "library.json").exists():
        return bool(manifest_json(path))
    return (path / "library.properties").exists() or (path / "module.json").exists()


def head(path: pathlib.Path) -> str:
    return run(["git", "rev-parse", "HEAD"], cwd=path).stdout.strip()


def pinned_commit(dep: Dep) -> str:
    if COMMIT.match(dep.ref):
        return dep.ref
    out = run(["git", "rev-parse", f"{dep.ref}^{{commit}}"], cwd=dep.path, check=False)
    return out.stdout.strip() if out.returncode == 0 else ""


def add(deps: list) -> None:
    LIBDEPS.mkdir(parents=True, exist_ok=True)

    for dep in deps:
        if dep.shared:
            print(f"skipping {dep.repo}, already vendored at {dep.path.relative_to(ROOT)}")
            continue

        if not (dep.path / ".git").exists():
            run(["git", "submodule", "add", "--force", "--depth", "1", dep.remote,
                 str(dep.path.relative_to(ROOT))], cwd=ROOT)

        run(["git", "fetch", "--depth", "1", "origin", dep.ref], cwd=dep.path)
        run(["git", "checkout", "--detach", "FETCH_HEAD"], cwd=dep.path)
        run(["git", "add", str(dep.path.relative_to(ROOT))], cwd=ROOT)
        print(f"{dep.repo} pinned at {dep.ref} ({head(dep.path)[:12]})")


def check(deps: list) -> int:
    drifted = 0
    for dep in deps:
        if not (dep.path / ".git").exists():
            print(f"missing: {dep.path.relative_to(ROOT)} for {dep.url}")
            drifted += 1
            continue

        want = pinned_commit(dep)
        if want and head(dep.path) != want:
            print(f"drifted: {dep.repo} is at {head(dep.path)[:12]}, {dep.ref} wants {want[:12]}")
            drifted += 1

    if drifted:
        print(f"{drifted} dependency submodule(s) do not match the pins, run: "
              f"scripts/vendor_libdeps.py --add")
    else:
        print(f"all {len(deps)} dependency submodules match their pins")
    return drifted


def stage(deps: list, env: str) -> None:
    target = MESHTASTIC / ".pio" / "libdeps" / env
    target.mkdir(parents=True, exist_ok=True)

    for dep in deps:
        if not (dep.path / ".git").exists():
            sys.exit(f"{dep.path} is missing, run: git submodule update --init --depth 1")

        name = package_name(dep)
        dest = target / name
        if dest.exists():
            shutil.rmtree(dest)

        shutil.copytree(dep.path, dest, ignore=shutil.ignore_patterns(".git"))

        version = package_version(dep)
        if not usable_manifest(dest):
            (dest / "library.json").write_text(
                json.dumps({"name": name, "version": version, "frameworks": "*", "platforms": "*"}, indent=2) + "\n")

        if dep.registry:
            spec = {"owner": dep.registry.split("/", 1)[0], "id": None, "name": name,
                    "requirements": version, "uri": None}
        else:
            spec = {"owner": None, "id": None, "name": name, "requirements": None, "uri": dep.url}

        piopm = {"type": "library", "name": name, "version": version, "spec": spec}
        (dest / ".piopm").write_text(json.dumps(piopm) + "\n")

    print(f"staged {len(deps)} libraries into {target}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Vendor the Meshtastic library pins as submodules")
    parser.add_argument("--env", default="coverage")
    parser.add_argument("--add", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--stage", action="store_true")
    args = parser.parse_args()

    deps = resolve_deps(args.env)

    if args.add:
        add(deps)
    if args.check:
        sys.exit(1 if check(deps) else 0)
    if args.stage or not (args.add or args.check):
        stage(deps, args.env)


if __name__ == "__main__":
    main()

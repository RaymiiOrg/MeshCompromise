# MeshCompromise

> **This is vibeslopped up. No human has read a line of this code.**
>
> Every file here was written by an LLM. It compiles and the tests pass. No warranty of any kind — if you transmit with it, that is entirely on you.

This is firmware for the M5Stack Cardputer ADV that speaks both Meshtastic and MeshCore on one device. It is Meshtastic underneath and looks and behaves like Meshtastic, but it also listens to MeshCore and passes messages between the two networks in both directions, so one Cardputer covers both meshes instead of two devices or two firmwares.

This project is not affiliated with Meshtastic or MeshCore.

There is a special settings page on the Cardputer ADV for MeshCore settings like SF/BW. Make sure it matches, the defaults are for EU/NL. The radio is shared so you might miss some messages. #LongFast to #public and back works, as do direct messages. Double press ENTER to edit settings on the special settings page.

**See the Github Releases page for a downloadable FW.**

The repo has a web flasher html page and a serial monitor html page.

## Build

```
git submodule update --init --depth 1
python3 scripts/prepare_firmware.py
pio run -d external/meshtastic -e m5stack-cardputer-adv-meshcore
```

## Flash

```
python3 -m http.server 8000
```

Then open `http://localhost:8000/tools/flasher/`.

## Tests

```
pio test -e native
```

```
python3 scripts/vendor_libdeps.py --stage --env coverage
python3 scripts/prepare_firmware.py --tests-only
pio test -d external/meshtastic -e coverage -f test_meshcompromise
```

## Licence

AGPL-3.0. The firmware combines this with Meshtastic (GPL-3.0) and MeshCore (MIT), and the flasher bundles esptool-js (Apache-2.0); those keep their own licences.

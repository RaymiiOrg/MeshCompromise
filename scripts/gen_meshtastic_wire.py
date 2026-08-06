#!/usr/bin/env python3

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
MESHTASTIC = ROOT / "external" / "meshtastic"

RADIO_INTERFACE = MESHTASTIC / "src" / "mesh" / "RadioInterface.h"
CHANNELS_H = MESHTASTIC / "src" / "mesh" / "Channels.h"
CHANNELS_CPP = MESHTASTIC / "src" / "mesh" / "Channels.cpp"
ROUTER_CPP = MESHTASTIC / "src" / "mesh" / "Router.cpp"
DISPLAY_FORMATTERS = MESHTASTIC / "src" / "DisplayFormatters.cpp"

OUT = ROOT / "test" / "support" / "generated" / "meshtastic_wire_constants.h"

DEFINES = [
    ("MESHTASTIC_HEADER_LENGTH", "kMeshtasticHeaderLength", "size_t"),
    ("MAX_LORA_PAYLOAD_LEN", "kMeshtasticMaxLoraPayload", "size_t"),
    ("PACKET_FLAGS_HOP_LIMIT_MASK", "kFlagsHopLimitMask", "uint8_t"),
    ("PACKET_FLAGS_WANT_ACK_MASK", "kFlagsWantAckMask", "uint8_t"),
    ("PACKET_FLAGS_VIA_MQTT_MASK", "kFlagsViaMqttMask", "uint8_t"),
    ("PACKET_FLAGS_HOP_START_MASK", "kFlagsHopStartMask", "uint8_t"),
    ("PACKET_FLAGS_HOP_START_SHIFT", "kFlagsHopStartShift", "uint8_t"),
]

EXPECTED_FIELD_ORDER = ["to", "from", "id", "flags", "channel", "next_hop", "relay_node"]

HASH_RECIPE = "h ^= xorHash(k.bytes, k.length)"
ENCODE_STEP = "&meshtastic_Data_msg, &p->decoded"
ENCRYPT_STEP = "crypto->encryptPacket(getFrom(p), p->id, numbytes, bytes)"


def die(message):
    sys.exit(
        "%s\n"
        "The Meshtastic wire format our host tests encode is derived from upstream. Re-derive\n"
        "test/test_bridge/meshtastic_wire.h and this script against the current submodule." % message
    )


def read(path):
    if not path.exists():
        sys.exit("missing %s\nrun: git submodule update --init --depth 1" % path)
    return path.read_text()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def field_order(radio):
    match = re.search(r"typedef struct \{(.*?)\} PacketHeader;", radio, re.DOTALL)
    if match is None:
        die("could not find the PacketHeader struct in RadioInterface.h")

    order = []
    for declaration in strip_comments(match.group(1)).split(";"):
        declaration = declaration.strip()
        if not declaration:
            continue
        parts = declaration.split(None, 1)
        if len(parts) < 2:
            continue
        for field in parts[1].split(","):
            field = field.strip().strip("*")
            if field:
                order.append(field)
    return order


def defines(radio):
    found = {}
    for upstream_name, _, _ in DEFINES:
        match = re.search(r"^#define\s+%s\s+(\S+)" % re.escape(upstream_name), radio, re.MULTILINE)
        if match is None:
            die("upstream no longer defines %s" % upstream_name)
        found[upstream_name] = match.group(1)
    return found


def default_psk(channels):
    match = re.search(r"defaultpsk\[\]\s*=\s*\{(.*?)\}", channels, re.DOTALL)
    if match is None:
        die("could not find defaultpsk in Channels.h")

    values = [value.strip() for value in match.group(1).split(",") if value.strip()]
    if not all(re.fullmatch(r"0x[0-9a-fA-F]{2}", value) for value in values):
        die("defaultpsk is no longer a plain list of byte literals")
    if len(values) != 16:
        die("defaultpsk is %d bytes, expected 16" % len(values))
    return values


def primary_channel_name(display_formatters, channels_cpp):
    if 'channelName = DisplayFormatters::getModemPresetDisplayName' not in channels_cpp:
        die("Channels::getName no longer substitutes the modem preset name for an unnamed channel")

    match = re.search(r"case PRESET\(LONG_FAST\):\s*\n\s*return useShortName \? \"[^\"]*\" : \"([^\"]+)\"",
                      display_formatters)
    if match is None:
        die("could not find the LONG_FAST preset display name in DisplayFormatters.cpp")
    return match.group(1)


def check_pipeline(channels_cpp, router_cpp):
    if HASH_RECIPE not in channels_cpp:
        die("Channels::generateHash no longer xors the channel name hash with the key hash")
    if ENCODE_STEP not in router_cpp:
        die("Router no longer encodes meshtastic_Data before encrypting")
    if ENCRYPT_STEP not in router_cpp:
        die("Router no longer encrypts with encryptPacket(from, id, len, bytes)")


def render():
    radio = read(RADIO_INTERFACE)
    channels = read(CHANNELS_H)
    preset_name = primary_channel_name(read(DISPLAY_FORMATTERS), read(CHANNELS_CPP))

    order = field_order(radio)
    if order != EXPECTED_FIELD_ORDER:
        die("PacketHeader field order changed upstream: %s (we encode %s)" % (order, EXPECTED_FIELD_ORDER))

    check_pipeline(read(CHANNELS_CPP), read(ROUTER_CPP))

    values = defines(radio)
    psk = default_psk(channels)

    lines = [
        "#pragma once",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace meshcompromise",
        "{",
        "",
    ]

    for upstream_name, our_name, kind in DEFINES:
        lines.append("constexpr %s %s = %s;" % (kind, our_name, values[upstream_name]))

    lines += [
        "",
        'constexpr char kMeshtasticPrimaryChannelName[] = "%s";' % preset_name,
        "",
        "constexpr uint8_t kMeshtasticDefaultPsk[%d] = {" % len(psk),
        "    " + ", ".join(psk[:8]) + ",",
        "    " + ", ".join(psk[8:]) + "};",
        "",
        "} // namespace meshcompromise",
        "",
    ]

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Derive the Meshtastic wire constants from the submodule")
    parser.add_argument("--check", action="store_true", help="fail if the committed header is stale")
    args = parser.parse_args()

    content = render()

    if args.check:
        if not OUT.exists() or OUT.read_text() != content:
            sys.exit("stale %s\nrun: python3 scripts/gen_meshtastic_wire.py" % OUT.relative_to(ROOT))
        print("Meshtastic wire constants match upstream")
        return

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(content)
    print("wrote %s" % OUT.relative_to(ROOT))


if __name__ == "__main__":
    main()

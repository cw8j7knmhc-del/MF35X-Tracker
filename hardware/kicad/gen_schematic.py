#!/usr/bin/env python3
"""Generate the MF35X tracker KiCad schematic from hardware/wiring.csv.

The schematic deliberately models the tracker at module/connector level.
Confirmed signal assignments come from wiring.csv; unresolved physical details
remain TODO nets instead of being guessed.
"""
from __future__ import annotations

import csv
import uuid
from pathlib import Path
from typing import NamedTuple

ROOT = Path(__file__).resolve().parents[2]
WIRING = ROOT / "hardware" / "wiring.csv"
OUT = ROOT / "hardware" / "kicad" / "MF35X_Tracker.kicad_sch"
PROJECT = "MF35X_Tracker"
ROOT_UUID = "7c44dcb5-b2e5-41b8-9fe6-5d37a9a1d001"
NS = uuid.UUID(ROOT_UUID)


def uid(key: str) -> str:
    return str(uuid.uuid5(NS, key))


class Pin(NamedTuple):
    number: str
    name: str
    direction: str
    x: float
    y: float
    angle: float


class Symbol(NamedTuple):
    lib_id: str
    ref: str
    value: str
    x: float
    y: float
    pins: list[Pin]


def vertical_pins(items, left=True, spacing=2.54):
    n = len(items)
    ys = [(n - 1) * spacing / 2 - i * spacing for i in range(n)]
    x = -8.0 if left else 8.0
    angle = 180.0 if left else 0.0
    return [Pin(num, name, direction, x, y, angle) for (num, name, direction), y in zip(items, ys)]


def two_side(left_items, right_items):
    return vertical_pins(left_items, True) + vertical_pins(right_items, False)


def resistor_pins():
    return [
        Pin("1", "1", "passive", 0.0, -3.81, 90.0),
        Pin("2", "2", "passive", 0.0, 3.81, 270.0),
    ]


ESP = two_side(
    [
        ("1", "5V_IN", "passive"),
        ("2", "3V3", "power_out"),
        ("3", "GND", "passive"),
        ("4", "GPIO8", "bidirectional"),
        ("5", "GPIO9", "bidirectional"),
        ("6", "GPIO10", "input"),
        ("7", "GPIO0", "passive"),
    ],
    [
        ("8", "GPIO11", "output"),
        ("9", "GPIO12", "output"),
        ("10", "GPIO13", "output"),
        ("11", "GPIO14", "input"),
        ("12", "GPIO16", "input"),
        ("13", "GPIO17", "output"),
    ],
)

ADS = two_side(
    [
        ("1", "VDD", "power_in"),
        ("2", "GND", "passive"),
        ("3", "SCL", "input"),
        ("4", "SDA", "bidirectional"),
        ("5", "ADDR", "input"),
    ],
    [
        ("6", "AIN0", "input"),
        ("7", "AIN1", "input"),
        ("8", "AIN2", "input"),
        ("9", "AIN3", "input"),
    ],
)

GPS = vertical_pins(
    [("1", "VCC", "passive"), ("2", "GND", "passive"), ("3", "TX", "output"), ("4", "RX", "input")],
    True,
)
MAX31855 = vertical_pins(
    [
        ("1", "VCC", "passive"), ("2", "GND", "passive"), ("3", "CLK", "input"),
        ("4", "CS", "input"), ("5", "DO", "output"), ("6", "T+", "passive"), ("7", "T-", "passive"),
    ],
    True,
)
HY_M154 = vertical_pins(
    [("1", "W_INPUT", "passive"), ("2", "INPUT_GND", "passive"), ("3", "OUTPUT_SIGNAL", "output"), ("4", "OUTPUT_GND", "passive")],
    True,
)
CONN2 = vertical_pins([("1", "PIN1", "passive"), ("2", "PIN2", "passive")], True)
SENSOR2 = vertical_pins([("1", "SENSE", "passive"), ("2", "RETURN_GND", "passive")], True)

COMPONENTS = [
    Symbol("MF35X:ESP32_S3_USED", "U1", "Freenove ESP32-S3 WROOM Lite FNK0099A", 50, 55, ESP),
    Symbol("MF35X:ADS1115", "U2", "ADS1115 @ 0x48", 112, 55, ADS),
    Symbol("MF35X:GPS", "U3", "GPS module - VCC TBD", 170, 55, GPS),
    Symbol("MF35X:MAX31855", "U4", "MAX31855 K-type - VCC TBD", 170, 100, MAX31855),
    Symbol("MF35X:HY_M154", "U5", "HY-M154 / PC817 RPM interface", 112, 100, HY_M154),
    Symbol("MF35X:CONN2", "J1", "External GPIO11 logic output", 50, 100, CONN2),
    Symbol("MF35X:CONN2", "J2", "5V power input from DC/DC", 50, 130, CONN2),
    Symbol("MF35X:R", "R1", "980R oil-temp fixed", 92, 145, resistor_pins()),
    Symbol("MF35X:SENSOR2", "J3", "VDO oil-temp NTC - return/case TBD", 92, 175, SENSOR2),
    Symbol("MF35X:R", "R2", "216R measured / 220R firmware - Issue #16", 135, 145, resistor_pins()),
    Symbol("MF35X:SENSOR2", "J4", "Oil-pressure sender - return/case TBD", 135, 175, SENSOR2),
    Symbol("MF35X:CONN2", "J5", "Vehicle battery sense input", 50, 175, CONN2),
    Symbol("MF35X:R", "R3", "100k battery divider upper", 70, 200, resistor_pins()),
    Symbol("MF35X:R", "R4", "10k battery divider lower", 100, 220, resistor_pins()),
]


def esc(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def lib_symbol_block(lib_id: str, pins: list[Pin]) -> str:
    short = lib_id.split(":", 1)[1]
    max_y = max(abs(p.y) for p in pins) if pins else 2.54
    if short == "R":
        x1, y1, x2, y2 = -1.4, -2.3, 1.4, 2.3
    else:
        x1, y1, x2, y2 = -5.5, -max_y - 1.27, 5.5, max_y + 1.27
    pin_defs = "".join(
        f'      (pin {p.direction} line (at {p.x:.2f} {p.y:.2f} {p.angle:.0f}) (length 2.54)\n'
        f'        (name "{esc(p.name)}" (effects (font (size 1.0 1.0))))\n'
        f'        (number "{esc(p.number)}" (effects (font (size 1.0 1.0))))\n'
        f'      )\n'
        for p in pins
    )
    return f'''    (symbol "{esc(lib_id)}" (pin_names (offset 0.5)) (in_bom yes) (on_board yes)
      (property "Reference" "X" (at 0 {y2 + 2:.2f} 0) (effects (font (size 1.27 1.27))))
      (property "Value" "{esc(short)}" (at 0 {y1 - 2:.2f} 0) (effects (font (size 1.27 1.27))))
      (property "Footprint" "" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
      (property "Datasheet" "~" (at 0 0 0) (effects (font (size 1.27 1.27)) hide))
      (symbol "{esc(short)}_0_1"
        (rectangle (start {x1:.2f} {y1:.2f}) (end {x2:.2f} {y2:.2f})
          (stroke (width 0.254) (type default))
          (fill (type background))
        )
{pin_defs}      )
    )'''


def instance_block(sym: Symbol) -> str:
    sid = uid(f"symbol:{sym.ref}")
    pin_blocks = "".join(
        f'    (pin "{esc(p.number)}" (uuid {uid(f"pin:{sym.ref}:{p.number}")}))\n' for p in sym.pins
    )
    return f'''  (symbol (lib_id "{esc(sym.lib_id)}") (at {sym.x:.2f} {sym.y:.2f} 0) (unit 1)
    (in_bom yes) (on_board yes)
    (uuid {sid})
    (property "Reference" "{esc(sym.ref)}" (at {sym.x + 7:.2f} {sym.y - 3:.2f} 0) (effects (font (size 1.27 1.27))))
    (property "Value" "{esc(sym.value)}" (at {sym.x + 7:.2f} {sym.y:.2f} 0) (effects (font (size 1.05 1.05))))
    (property "Footprint" "" (at {sym.x:.2f} {sym.y:.2f} 0) (effects (font (size 1.27 1.27)) hide))
    (property "Datasheet" "~" (at {sym.x:.2f} {sym.y:.2f} 0) (effects (font (size 1.27 1.27)) hide))
{pin_blocks}    (instances
      (project "{PROJECT}"
        (path "/{ROOT_UUID}" (reference "{esc(sym.ref)}") (unit 1))
      )
    )
  )'''


def global_label(net: str, x: float, y: float, key: str) -> str:
    return f'''  (global_label "{esc(net)}" (shape bidirectional) (at {x:.2f} {y:.2f} 0)
    (effects (font (size 0.85 0.85)) (justify left))
    (uuid {uid(f"label:{key}:{net}")})
  )'''


def no_connect(x: float, y: float, key: str) -> str:
    return f'  (no_connect (at {x:.2f} {y:.2f}) (uuid {uid(f"nc:{key}")}))'


def text_block(text: str, x: float, y: float, size: float = 1.0, bold: bool = False) -> str:
    weight = " bold" if bold else ""
    return f'''  (text "{esc(text)}" (at {x:.2f} {y:.2f} 0)
    (effects (font (size {size:.2f} {size:.2f}){weight}) (justify left bottom))
    (uuid {uid(f"text:{x}:{y}:{text}")})
  )'''


def pin_lookup():
    lookup = {}
    for sym in COMPONENTS:
        for pin in sym.pins:
            lookup[f"{sym.ref}.{pin.name}"] = (sym, pin)
            lookup[f"{sym.ref}.{pin.number}"] = (sym, pin)
    return lookup


def load_wiring():
    with WIRING.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def build() -> str:
    rows = load_wiring()
    lookup = pin_lookup()
    lib_seen = {}
    for sym in COMPONENTS:
        lib_seen.setdefault(sym.lib_id, sym.pins)

    labels = set()
    ncs = set()
    todos = []
    for row in rows:
        net = row["net"].strip()
        status = row["status"].strip()
        if status == "TODO":
            todos.append(f'{net}: {row["notes"].strip()}')
        for endpoint in (row["endpoint_a"].strip(), row["endpoint_b"].strip()):
            if endpoint not in lookup:
                continue
            sym, pin = lookup[endpoint]
            key = f"{sym.ref}.{pin.name}"
            if status in {"NC_RESERVED", "NC_UNUSED"}:
                ncs.add((key, sym.x + pin.x, sym.y + pin.y))
            else:
                labels.add((key, net, sym.x + pin.x, sym.y + pin.y))

    out = [
        "(kicad_sch",
        "  (version 20231120)",
        '  (generator "chatgpt_mf35x_hw")',
        f"  (uuid {ROOT_UUID})",
        '  (paper "A4")',
        "  (title_block",
        '    (title "MF35X Livetracker - Trackerbox Wiring")',
        '    (date "2026-09-10")',
        '    (rev "DRAFT-1")',
        '    (company "MF35X Tracker")',
        '    (comment 1 "Generated from hardware/wiring.csv; unresolved details are TODO")',
        "  )",
        "  (lib_symbols",
    ]
    out.extend(lib_symbol_block(lib_id, pins) for lib_id, pins in lib_seen.items())
    out.append("  )")
    out.extend(instance_block(sym) for sym in COMPONENTS)
    for key, net, x, y in sorted(labels):
        out.append(global_label(net, x, y, key))
    for key, x, y in sorted(ncs):
        out.append(no_connect(x, y, key))

    out.append(text_block("MF35X LIVETRACKER - HARDWARE DRAFT 1", 20, 17, 1.7, True))
    out.append(text_block("GPIO0 is intentionally NC/reserved. Never connect the raw alternator W signal directly to ESP32 GPIO10.", 20, 23, 0.95, True))
    out.append(text_block("TODO items are not guessed; confirm on the physical trackerbox before marking VERIFIED.", 20, 245, 0.9, True))
    y = 250.0
    for item in todos[:6]:
        out.append(text_block(item, 20, y, 0.72, False))
        y += 3.0
    out.append(text_block("Oil-pressure divider: 216 ohm measured/diagnostic vs 220 ohm firmware main calculation remains open as Issue #16.", 20, 269, 0.8, False))
    out.extend(["  (sheet_instances", '    (path "/" (page "1"))', "  )", ")"])
    return "\n".join(out) + "\n"


def main():
    OUT.parent.mkdir(parents=True, exist_ok=True)
    content = build()
    OUT.write_text(content, encoding="utf-8")
    print(f"Generated {OUT} ({len(content)} bytes)")


if __name__ == "__main__":
    main()

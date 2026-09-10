# MF35X Trackerbox – KiCad hardware model

This directory contains the machine-checkable hardware representation of the MF35X Livetracker.

## Source of truth

`hardware/wiring.csv` is the canonical wiring matrix. `gen_schematic.py` renders that matrix into `MF35X_Tracker.kicad_sch` using deterministic UUIDs so changes remain reviewable in Git.

Regenerate after changing the matrix or generator:

```bash
python hardware/kicad/gen_schematic.py
```

`tests/check_kicad_wiring.py` requires the committed schematic to match the generator byte-for-byte and cross-checks the firmware-facing assignments against `hardware/pinout.csv`.

## Confidence states

- `VERIFIED` – connection is backed by the current firmware/pinout and known project wiring.
- `DERIVED` – topology follows the actual firmware conversion equation/component values, but should still be checked against the physical box.
- `PARTIAL` – the core connection is known, but at least one terminal/module detail remains to be physically confirmed.
- `TODO` – intentionally unresolved. The schematic must not silently guess the connection.
- `NC_RESERVED` / `NC_UNUSED` – explicit KiCad no-connect. GPIO0 is reserved and must remain unused; ADS1115 AIN3 is currently unused.

## Current DRAFT-1 scope

The schematic is deliberately a **module-level electrical wiring plan**, not a PCB layout and not yet a complete automotive protection design. It represents the ESP32-S3 board, ADS1115, GPS UART, HY-M154 RPM interface, MAX31855, GPIO11 output, oil-temperature divider, oil-pressure divider and battery-voltage divider.

Still intentionally unresolved in DRAFT-1:

- exact GPS module supply pin/voltage used in the installed box;
- exact MAX31855 breakout supply voltage;
- HY-M154 selected channel, terminal labels, jumper state, output pull-up and ground/isolation arrangement;
- external GPIO11 load/driver stage and its reference/return;
- whether the oil-temperature and oil-pressure senders return through a dedicated conductor or engine/case ground;
- exact final DC/DC module and automotive input protection (fuse, reverse-polarity protection, transient/TVS/filtering);
- oil-pressure fixed resistor discrepancy: 216 ohm measured/diagnostic vs 220 ohm in the main firmware calculation (tracked separately as Issue #16).

## ERC

CI runs KiCad CLI ERC on the generated file. The goal is that **electrical errors fail the pull request** while documented DRAFT/TODO conditions remain explicit in `wiring.csv` and are never promoted to `VERIFIED` automatically.

Equivalent local command:

```bash
kicad-cli sch erc --severity-error --exit-code-violations \
  -o erc.rpt hardware/kicad/MF35X_Tracker.kicad_sch
```

Do not use a rendered picture as the wiring authority. The review chain is:

`firmware -> hardware/pinout.csv -> hardware/wiring.csv -> generated KiCad schematic -> ERC`

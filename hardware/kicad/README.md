# MF35X Trackerbox – KiCad hardware model

This directory contains the machine-checkable electrical representation of the MF35X Livetracker trackerbox.

## Source of truth

`hardware/wiring.csv` is the canonical wiring matrix. `gen_schematic.py` renders that matrix into `MF35X_Tracker.kicad_sch` using deterministic UUIDs so changes remain reviewable in Git.

Regenerate after changing the matrix or generator:

```bash
python hardware/kicad/gen_schematic.py
```

`tests/check_kicad_wiring.py` requires the committed schematic to match the generator byte-for-byte and cross-checks the firmware-facing assignments against `hardware/pinout.csv`. `tests/check_kicad_pin_anchors.py` independently verifies critical pin positions and no-connect markers.

## Confidence states

- `VERIFIED` – connection is backed by current firmware/pinout or explicitly confirmed project wiring.
- `DERIVED` – topology follows the firmware conversion equation/component values.
- `PARTIAL` – the core connection is known, but a physical detail remains to be confirmed.
- `TODO` – intentionally unresolved; the schematic must not silently guess it.
- `NC_RESERVED` / `NC_UNUSED` – explicit KiCad no-connect. GPIO0 is reserved and ADS1115 AIN3 is unused.

## HW-REV1 scope

HW-REV1 contains the real trackerbox functional modules and connector-level wiring for:

- Freenove ESP32-S3 WROOM Lite FNK0099A;
- ADS1115 at address `0x48`, VDD 3.3 V, ADDR tied to GND;
- ATGM336H GPS: 3.3 V, GND, TX -> GPIO16, ESP GPIO17 -> GPS RX;
- MAX31855: 3.3 V, GND, CLK GPIO12, CS GPIO13, DO GPIO14 and K-type T+/T- connector;
- HY-M154 RPM interface: alternator W -> J2-OUT, J2-GND -> GND, J1-VCC -> +5 V, J1-GND -> GND, J1-OUT -> GPIO10;
- GPIO11 as the 3.3-V control signal for the external solenoid-valve switching function;
- VDO 801/1/6 oil-temperature sender on ADS1115 AIN0, including its case/thread return to engine ground;
- oil-pressure sender on ADS1115 AIN1;
- battery-voltage divider 100 kOhm / 10 kOhm on ADS1115 AIN2;
- external 5-V DC/DC input connector;
- explicit no-connects for ESP32 GPIO0 and ADS1115 AIN3.

The schematic uses two logical `PWR_FLAG` symbols on `+5V_IN` and `GND`. These are **ERC metadata only**: they tell KiCad that the two rails are powered by the external DC/DC source. They are not physical components and are excluded from BOM/board placement.

## Intentionally open physical details

Only details not yet recovered or explicitly confirmed remain open:

- exact external power-driver topology between GPIO11 and the solenoid valve (relay/MOSFET/module, valve supply/current and flyback protection);
- signal-return terminal used at the external solenoid driver;
- exact installed oil-pressure sender return topology (dedicated ground conductor vs. case ground);
- final vehicle-side DC/DC return terminal and the complete automotive input-protection implementation outside the trackerbox schematic boundary;
- oil-pressure fixed-resistor discrepancy: about 216 ohm measured versus 220 ohm in the main firmware calculation (tracked separately as Issue #16).

GPIO11 must **not** directly power the solenoid-valve coil.

## ERC

CI runs KiCad CLI ERC on the generated file. Electrical errors fail the pull request, while documented TODO/PARTIAL conditions remain explicit rather than being silently promoted to VERIFIED.

Equivalent local command:

```bash
kicad-cli sch erc --severity-error --exit-code-violations \
  -o erc.rpt hardware/kicad/MF35X_Tracker.kicad_sch
```

Do not use a rendered picture as the wiring authority. The review chain is:

`firmware -> hardware/pinout.csv -> hardware/wiring.csv -> generated KiCad schematic -> KiCad ERC`

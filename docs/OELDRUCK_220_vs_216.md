# Oeldruck: 220 Ohm Hauptberechnung vs. 216 Ohm gemessener Widerstand

Automatischer Exact-Source-Test vom 2026-09-10:

- Hauptberechnung `DRUCK_R_FIXED`: 220.0 Ohm
- Diagnose `MF35X_DIAG_PRESSURE_FIXED_OHM`: 216.0 Ohm (gemessener Festwiderstand)
- maximal berechnete Abweichung an den hinterlegten Kennlinienpunkten: ca. +0.198 bar
- groesste Abweichung: nominell 8.0 bar / 155 Ohm

Die massive Anzahl `SHORT`/`OUT_OF_RANGE` in den bisherigen Renn-CSV-Dateien wird durch diese 4-Ohm-Differenz nicht erklaert. Die Differenz erzeugt aber zwei unterschiedliche berechnete Druckwerte aus derselben Messspannung und muss deshalb vor der finalen Oeldruckkalibrierung auf einen gemeinsamen, am realen Aufbau bestaetigten Wert vereinheitlicht werden.

Bis dahin bleibt die Firmware unveraendert; PR #15 dient als Guard und macht die Abweichung reproduzierbar sichtbar.

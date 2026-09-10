#!/usr/bin/env python3
"""Host-side unit test for the active V5.9.19 GPIO11 control function.

Extracts mf35xStabilenSchaltausgangAktualisieren() verbatim from
rpm_stable_override.hpp, compiles it with minimal stubs, and tests the exact
logic used by the robust RPM control task in normal tracker operation.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "esp32" / "MF35X_Livetracker" / "rpm_stable_override.hpp"
SIGNATURE = "void mf35xStabilenSchaltausgangAktualisieren()"


def extract_function(source: str, signature: str) -> str:
    definition = signature + " {"
    start = source.find(definition)
    if start < 0:
        raise RuntimeError(f"Funktionsdefinition nicht gefunden: {definition}")
    brace = source.find("{", start + len(signature))
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    raise RuntimeError("Funktionsende nicht gefunden")


def build_test_source(function_source: str) -> str:
    return r'''
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
using std::isfinite;

struct GpsSnapshot {
  bool speedValid = false;
  double speedKmh = 0.0;
};

struct OutputConfig {
  float speedEnableKmh;
  float rpmOn;
  float rpmOff;
};

GpsSnapshot testGps;
bool testGpsFixCurrent = false;
OutputConfig outputConfig = {60.0f, 3200.0f, 3150.0f};
int outputConfigMux = 0;
int mf35xRpmMux = 0;
volatile bool mf35xRpmNeuerfassungAktiv = false;
volatile bool rpmSignalOk = false;
volatile float mf35xRpmSchnell = 0.0f;
volatile bool schaltausgangAktiv = false;
constexpr int SCHALTAUSGANG_PIN = 11;
constexpr int HIGH = 1;
constexpr int LOW = 0;
int lastDigitalWritePin = -1;
int lastDigitalWriteValue = -1;

#define portENTER_CRITICAL(x) do { (void)(x); } while (0)
#define portEXIT_CRITICAL(x) do { (void)(x); } while (0)

GpsSnapshot gpsSnapshotLesen() { return testGps; }
bool gpsFixAktuell(const GpsSnapshot&) { return testGpsFixCurrent; }
void digitalWrite(int pin, int value) {
  lastDigitalWritePin = pin;
  lastDigitalWriteValue = value;
}

''' + function_source + r'''

int failures = 0;
int checks = 0;

void setInputs(
    bool previousOn,
    bool gpsFix,
    bool speedValid,
    double speedKmh,
    bool rpmOk,
    float rpmValue,
    bool reacquire = false,
    float speedEnable = 60.0f,
    float rpmOn = 3200.0f,
    float rpmOff = 3150.0f) {
  schaltausgangAktiv = previousOn;
  testGpsFixCurrent = gpsFix;
  testGps.speedValid = speedValid;
  testGps.speedKmh = speedKmh;
  rpmSignalOk = rpmOk;
  mf35xRpmSchnell = rpmValue;
  mf35xRpmNeuerfassungAktiv = reacquire;
  outputConfig = {speedEnable, rpmOn, rpmOff};
  lastDigitalWritePin = -1;
  lastDigitalWriteValue = -1;
}

void expectState(const std::string& name, bool expected) {
  ++checks;
  mf35xStabilenSchaltausgangAktualisieren();
  const bool actual = schaltausgangAktiv;
  const int expectedWrite = expected ? HIGH : LOW;
  if (actual != expected ||
      lastDigitalWritePin != SCHALTAUSGANG_PIN ||
      lastDigitalWriteValue != expectedWrite) {
    ++failures;
    std::cerr << "FAIL: " << name
              << " state=" << actual << " expected=" << expected
              << " pin=" << lastDigitalWritePin
              << " write=" << lastDigitalWriteValue
              << " expected_write=" << expectedWrite << "\n";
  } else {
    std::cout << "PASS: " << name << "\n";
  }
}

int main() {
  setInputs(true, false, true, 80.0, true, 4000.0f);
  expectState("GPS fix lost forces LOW", false);

  setInputs(true, true, false, 80.0, true, 4000.0f);
  expectState("invalid speed forces LOW", false);

  setInputs(true, true, true, 80.0, false, 4000.0f);
  expectState("RPM signal lost forces LOW", false);

  setInputs(true, true, true, 80.0, true, 4000.0f, true);
  expectState("RPM reacquisition forces LOW", false);

  setInputs(true, true, true, 80.0, true, std::numeric_limits<float>::quiet_NaN());
  expectState("non-finite fast RPM forces LOW", false);

  setInputs(false, true, true, 59.9, true, 4000.0f, false, 60.0f);
  expectState("59.9 below 60 speed threshold", false);

  setInputs(false, true, true, 60.0, true, 3200.0f, false, 60.0f);
  expectState("60.0 equals speed threshold", true);

  setInputs(false, true, true, 60.9, true, 4000.0f, false, 61.0f);
  expectState("60.9 below configured 61 speed threshold", false);

  setInputs(false, true, true, 61.0, true, 3200.0f, false, 61.0f);
  expectState("61.0 equals configured speed threshold", true);

  setInputs(false, true, true, 70.0, true, 3199.0f);
  expectState("3199 does not switch ON", false);

  setInputs(false, true, true, 70.0, true, 3200.0f);
  expectState("3200 switches ON", true);

  setInputs(false, true, true, 70.0, true, 3201.0f);
  expectState("3201 switches ON", true);

  setInputs(true, true, true, 70.0, true, 3150.0f);
  expectState("3150 remains ON", true);

  setInputs(true, true, true, 70.0, true, 3149.0f);
  expectState("3149 switches OFF", false);

  setInputs(false, true, true, 70.0, true, 3175.0f);
  expectState("3175 retains previous OFF", false);

  setInputs(true, true, true, 70.0, true, 3175.0f);
  expectState("3175 retains previous ON", true);

  // Stateful hysteresis sequence.
  setInputs(false, true, true, 70.0, true, 3200.0f);
  mf35xStabilenSchaltausgangAktualisieren();
  mf35xRpmSchnell = 3175.0f;
  mf35xStabilenSchaltausgangAktualisieren();
  mf35xRpmSchnell = 3149.0f;
  mf35xStabilenSchaltausgangAktualisieren();
  ++checks;
  if (schaltausgangAktiv) {
    ++failures;
    std::cerr << "FAIL: ON-hold-OFF sequence\n";
  } else {
    std::cout << "PASS: ON-hold-OFF sequence\n";
  }

  mf35xRpmSchnell = 3199.0f;
  mf35xStabilenSchaltausgangAktualisieren();
  ++checks;
  if (schaltausgangAktiv) {
    ++failures;
    std::cerr << "FAIL: re-enabled below 3200\n";
  } else {
    std::cout << "PASS: no re-enable below 3200\n";
  }

  // Runtime fail-safe transitions after an already active output.
  setInputs(true, true, true, 70.0, true, 3300.0f);
  mf35xStabilenSchaltausgangAktualisieren();
  testGps.speedKmh = 59.9;
  ++checks;
  mf35xStabilenSchaltausgangAktualisieren();
  if (schaltausgangAktiv) {
    ++failures;
    std::cerr << "FAIL: speed loss after ON did not force LOW\n";
  } else {
    std::cout << "PASS: speed loss after ON forces LOW\n";
  }

  setInputs(true, true, true, 70.0, true, 3300.0f);
  mf35xStabilenSchaltausgangAktualisieren();
  mf35xRpmNeuerfassungAktiv = true;
  ++checks;
  mf35xStabilenSchaltausgangAktualisieren();
  if (schaltausgangAktiv) {
    ++failures;
    std::cerr << "FAIL: reacquisition after ON did not force LOW\n";
  } else {
    std::cout << "PASS: reacquisition after ON forces LOW\n";
  }

  std::cout << "Active V5.9.19 GPIO11 tests: " << (checks - failures)
            << "/" << checks << " passed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
'''


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    function_source = extract_function(source, SIGNATURE)
    cpp = build_test_source(function_source)

    with tempfile.TemporaryDirectory(prefix="mf35x-stable-output-test-") as temp:
        temp_path = Path(temp)
        source_path = temp_path / "test_stable_output.cpp"
        binary_path = temp_path / "test_stable_output"
        source_path.write_text(cpp, encoding="utf-8")

        compile_result = subprocess.run(
            ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source_path), "-o", str(binary_path)],
            text=True,
            capture_output=True,
        )
        if compile_result.returncode != 0:
            print("Host-Kompilierung der aktiven GPIO11-Funktion fehlgeschlagen.", file=sys.stderr)
            print(compile_result.stdout, file=sys.stderr)
            print(compile_result.stderr, file=sys.stderr)
            return compile_result.returncode

        run_result = subprocess.run([str(binary_path)], text=True)
        return run_result.returncode


if __name__ == "__main__":
    sys.exit(main())

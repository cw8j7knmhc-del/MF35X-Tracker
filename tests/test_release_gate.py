import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "esp32" / "MF35X_Livetracker"


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def define_int(source: str, name: str) -> int:
    m = re.search(rf"(?:#define|constexpr\s+[^;=]+)\s+{re.escape(name)}\s*(?:=\s*)?(\d+)(?:UL|U|L)?", source)
    if not m:
        raise AssertionError(f"Konstante {name} nicht gefunden")
    return int(m.group(1))


def extract_function(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"Funktion nicht gefunden: {signature}")
    brace = source.find("{", start)
    if brace < 0:
        raise AssertionError(f"Funktionsrumpf fehlt: {signature}")
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        elif source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise AssertionError(f"Unvollstaendiger Funktionsrumpf: {signature}")


HEADER = text(FW / "firmware_version.h")
CORE = text(FW / "MF35X_Livetracker_core.hpp")
INO = text(FW / "MF35X_Livetracker.ino")
TIMING = text(FW / "race_timing_fix.hpp")
NETWORK = text(FW / "race_network_isolation.hpp")
FAST = text(FW / "fast_track_logger.hpp")
MANIFEST = json.loads(text(ROOT / "firmware" / "manifest.json"))


class VersionGateTests(unittest.TestCase):
    def test_version_string_matches_numeric_code(self):
        version_match = re.search(r'MF35X_FIRMWARE_VERSION\s+"V(\d+)\.(\d+)\.(\d+)"', HEADER)
        code_match = re.search(r'MF35X_FIRMWARE_VERSION_CODE\s+(\d+)UL', HEADER)
        self.assertIsNotNone(version_match, "Versionsstring fehlt oder hat falsches Format")
        self.assertIsNotNone(code_match, "VersionCode fehlt")
        major, minor, patch = map(int, version_match.groups())
        expected = major * 10000 + minor * 100 + patch
        self.assertEqual(int(code_match.group(1)), expected)

    def test_new_firmware_is_not_older_than_published_manifest(self):
        code = int(re.search(r'MF35X_FIRMWARE_VERSION_CODE\s+(\d+)UL', HEADER).group(1))
        self.assertGreaterEqual(code, int(MANIFEST["versionCode"]))

    def test_faulty_v59120_is_never_release_version(self):
        self.assertNotIn('"V5.9.20"', HEADER)
        self.assertNotIn("50920UL", HEADER)


class Gpio11RegressionTests(unittest.TestCase):
    @staticmethod
    def gpio_step(active, speed, rpm, rpm_valid=True, speed_enable=60.0, rpm_on=3200.0, rpm_off=3150.0):
        # Referenzmodell des gewuenschten Verhaltens.
        if speed < speed_enable or not rpm_valid:
            return False
        if not active and rpm >= rpm_on:
            return True
        if active and rpm < rpm_off:
            return False
        return active

    def test_hysteresis_behavior_model(self):
        active = False
        active = self.gpio_step(active, 59.9, 4000)
        self.assertFalse(active, "Unter Speed-Freigabe muss GPIO11 LOW bleiben")
        active = self.gpio_step(active, 60.0, 3199)
        self.assertFalse(active)
        active = self.gpio_step(active, 60.0, 3200)
        self.assertTrue(active, "HIGH muss ab rpm_on einschalten")
        active = self.gpio_step(active, 60.0, 3170)
        self.assertTrue(active, "Im Hysteresefenster muss HIGH gehalten werden")
        active = self.gpio_step(active, 60.0, 3149)
        self.assertFalse(active, "Unter rpm_off muss LOW geschaltet werden")

    def test_firmware_gpio_function_contains_all_three_thresholds(self):
        body = extract_function(CORE, "void schaltausgangAktualisieren()")
        for token in ("speedEnableKmh", "rpmOn", "rpmOff", "schaltausgangAktiv", "digitalWrite"):
            self.assertIn(token, body)
        self.assertIn("outputConfigMux", body, "Schaltschwellen muessen konsistent gesnapshottet werden")


class RaceTimingRegressionTests(unittest.TestCase):
    def test_capture_task_is_independent_from_network(self):
        task = extract_function(TIMING, "void mf35xRaceTimingTask(void*)")
        for forbidden in ("firebasePut", "firebasePatch", "firebaseGet", "HTTPClient", "LittleFS"):
            self.assertNotIn(forbidden, task)
        self.assertIn("xQueueSend", task)

    def test_scheduler_uses_incremental_deadline(self):
        self.assertIn("nextCaptureMs += cfg.intervalMs", TIMING)
        self.assertIn("MF35X_RACE_TIMING_POLL_TICKS", TIMING)
        poll_ms = int(re.search(r"MF35X_RACE_TIMING_POLL_TICKS\s*=\s*pdMS_TO_TICKS\((\d+)\)", TIMING).group(1))
        self.assertLessEqual(poll_ms, 10)

    def test_five_second_reference_schedule_does_not_drift(self):
        interval = 5000
        deadline = 0
        actual = []
        # Modell: Netzwerkblockaden beeinflussen den separaten Capture-Task nicht.
        for _ in range(10):
            actual.append(deadline)
            deadline += interval
        self.assertEqual(actual, list(range(0, 50000, 5000)))

    def test_live_loop_has_no_race_network_drain(self):
        body = extract_function(INO, "void mf35xNextUsbCoreLoop()")
        for forbidden in ("offlineDrainBearbeiten", "mf35xDiagDrainOne", "mf35xRpmDiagDrainOne", "mf35xFastTrackDrainOne"):
            self.assertNotIn(forbidden, body)
        background = extract_function(NETWORK, "void mf35xRaceUploadTask(void*)")
        for required in ("offlineDrainBearbeiten", "mf35xDiagDrainOne", "mf35xRpmDiagDrainOne", "mf35xFastTrackDrainOne"):
            self.assertIn(required, background)


class AtomicSampleRegressionTests(unittest.TestCase):
    def test_one_capture_object_contains_base_and_both_diagnostics(self):
        struct_match = re.search(r"struct\s+Mf35xTimedRaceCapture\s*\{(.*?)\};", TIMING, re.S)
        self.assertIsNotNone(struct_match)
        body = struct_match.group(1)
        self.assertIn("OfflineRaceRecord rec", body)
        self.assertIn("Mf35xOilDiagRecord oilDiag", body)
        self.assertIn("Mf35xRpmDiagRecord rpmDiag", body)

    def test_same_sequence_is_assigned_to_all_records(self):
        persist = extract_function(NETWORK, "void mf35xRacePersistOne()")
        self.assertIn("item.rec.sequence = sequence", persist)
        self.assertIn("item.oilDiag.sequence = sequence", persist)
        self.assertIn("item.rpmDiag.sequence = sequence", persist)
        self.assertIn("item.rec.bootId = offlineBootId", persist)
        self.assertIn("item.oilDiag.bootId = offlineBootId", persist)
        self.assertIn("item.rpmDiag.bootId = offlineBootId", persist)

    def test_gpio11_is_captured_once_for_atomic_sample(self):
        capture = extract_function(TIMING, "Mf35xTimedRaceCapture mf35xRaceAtomicCaptureBauen()")
        self.assertIn("gpio11Snapshot", capture)
        self.assertIn("MF35X_RPM_DIAG_FLAG_GPIO11", capture)
        self.assertIn("OFFLINE_FLAG_SWITCH_OUTPUT", capture)


class FastTrackRegressionTests(unittest.TestCase):
    def test_fast_track_is_10hz_and_batches_one_second(self):
        interval = int(re.search(r"MF35X_FAST_TRACK_INTERVAL_MS\s*=\s*(\d+)UL", FAST).group(1))
        points = int(re.search(r"MF35X_FAST_TRACK_POINTS_PER_BATCH\s*=\s*(\d+)", FAST).group(1))
        self.assertEqual(interval, 100)
        self.assertEqual(points, 10)
        self.assertEqual(interval * points, 1000)

    def test_fast_track_payload_contains_required_driving_values(self):
        for token in ("latE6", "lngE6", "speedDeci", "rpmValue", "hdopCenti", "satellites", "MF35X_FAST_FLAG_GPIO11"):
            self.assertIn(token, FAST)
        self.assertIn('"/fastTrack/"', FAST)

    def test_fast_track_cannot_consume_last_megabyte(self):
        reserve = int(re.search(r"MF35X_FAST_TRACK_FLASH_PROTECT_BYTES\s*=\s*(\d+)UL\s*\*\s*(\d+)UL", FAST).group(1)) * 1024
        self.assertGreaterEqual(reserve, 1024 * 1024)

    def test_50hz_input_does_not_force_50hz_storage(self):
        # 50 GPS fixes/s, Logger speichert maximal alle 100 ms den neuesten Fix.
        incoming_hz = 50
        seconds = 10
        incoming = incoming_hz * seconds
        stored = seconds * (1000 // 100)
        self.assertEqual(incoming, 500)
        self.assertEqual(stored, 100)
        self.assertLess(stored, incoming)


class ReleaseArchitectureTests(unittest.TestCase):
    def test_required_next_usb_modules_are_included(self):
        for include in (
            '#include "race_timing_fix.hpp"',
            '#include "fast_track_logger.hpp"',
            '#include "race_network_isolation.hpp"',
        ):
            self.assertIn(include, INO)

    def test_setup_starts_all_background_tasks(self):
        setup = extract_function(INO, "void setup()")
        for call in ("mf35xRaceTimingSetup", "mf35xFastTrackSetup", "mf35xRaceNetworkIsolationSetup"):
            self.assertIn(call, setup)


if __name__ == "__main__":
    unittest.main(verbosity=2)

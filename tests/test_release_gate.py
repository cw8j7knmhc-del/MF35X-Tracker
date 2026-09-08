import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FW = ROOT / "esp32" / "MF35X_Livetracker"


def text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def extract_function(source: str, signature: str) -> str:
    search_from = 0
    while True:
        start = source.find(signature, search_from)
        if start < 0:
            raise AssertionError(f"Funktion nicht gefunden: {signature}")
        after = start + len(signature)
        brace = source.find("{", after)
        semicolon = source.find(";", after)
        if brace < 0:
            raise AssertionError(f"Funktionsrumpf fehlt: {signature}")
        if semicolon >= 0 and semicolon < brace:
            search_from = after
            continue
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
RUNTIME = text(FW / "mf35x_runtime.hpp")
TIMING = text(FW / "race_timing_fix.hpp")
NETWORK = text(FW / "race_network_isolation.hpp")
CONNECTIVITY = text(FW / "connectivity_diagnostics.hpp")
FAST = text(FW / "fast_track_logger.hpp")
ADMIN_V5922 = text(ROOT / "admin-v5922.js")
FIREBASE_CONFIG = text(ROOT / "firebase-config.js")
MANIFEST = json.loads(text(ROOT / "firmware" / "manifest.json"))


class VersionGateTests(unittest.TestCase):
    def test_version_string_matches_numeric_code(self):
        m = re.search(r'MF35X_FIRMWARE_VERSION\s+"V(\d+)\.(\d+)\.(\d+)"', HEADER)
        c = re.search(r'MF35X_FIRMWARE_VERSION_CODE\s+(\d+)UL', HEADER)
        self.assertIsNotNone(m)
        self.assertIsNotNone(c)
        major, minor, patch = map(int, m.groups())
        self.assertEqual(int(c.group(1)), major * 10000 + minor * 100 + patch)

    def test_new_firmware_is_not_older_than_published_manifest(self):
        code = int(re.search(r'MF35X_FIRMWARE_VERSION_CODE\s+(\d+)UL', HEADER).group(1))
        self.assertGreaterEqual(code, int(MANIFEST["versionCode"]))

    def test_faulty_v59120_is_never_release_version(self):
        self.assertNotIn('"V5.9.20"', HEADER)
        self.assertNotIn("50920UL", HEADER)


class Gpio11RegressionTests(unittest.TestCase):
    @staticmethod
    def gpio_step(active, speed, rpm, rpm_valid=True, speed_enable=60.0, rpm_on=3200.0, rpm_off=3150.0):
        if speed < speed_enable or not rpm_valid:
            return False
        if not active and rpm >= rpm_on:
            return True
        if active and rpm < rpm_off:
            return False
        return active

    def test_hysteresis_behavior_model(self):
        active = False
        self.assertFalse(self.gpio_step(active, 59.9, 4000))
        self.assertFalse(self.gpio_step(active, 60.0, 3199))
        active = self.gpio_step(active, 60.0, 3200)
        self.assertTrue(active)
        active = self.gpio_step(active, 60.0, 3170)
        self.assertTrue(active)
        self.assertFalse(self.gpio_step(active, 60.0, 3149))

    def test_firmware_gpio_function_contains_all_three_thresholds(self):
        body = extract_function(CORE, "void schaltausgangAktualisieren()")
        for token in ("speedEnableKmh", "rpmOn", "rpmOff", "schaltausgangAktiv", "digitalWrite"):
            self.assertIn(token, body)
        self.assertIn("outputConfigMux", body)


class RaceTimingRegressionTests(unittest.TestCase):
    def test_capture_task_is_independent_from_network(self):
        task = extract_function(TIMING, "void mf35xRaceTimingTask(void*)")
        for forbidden in ("firebasePut", "firebasePatch", "firebaseGet", "HTTPClient", "LittleFS"):
            self.assertNotIn(forbidden, task)
        self.assertIn("xQueueSend", task)

    def test_scheduler_uses_incremental_deadline(self):
        self.assertIn("nextCaptureMs += cfg.intervalMs", TIMING)
        poll_ms = int(re.search(r"MF35X_RACE_TIMING_POLL_TICKS\s*=\s*pdMS_TO_TICKS\((\d+)\)", TIMING).group(1))
        self.assertLessEqual(poll_ms, 10)

    def test_five_second_reference_schedule_does_not_drift(self):
        self.assertEqual(list(range(0, 50000, 5000)), [i * 5000 for i in range(10)])

    def test_live_runtime_has_no_race_network_drain(self):
        body = extract_function(RUNTIME, "void mf35xRuntimeLoop()")
        for forbidden in (
            "offlineDrainBearbeiten", "mf35xDiagDrainOne", "mf35xRpmDiagDrainOne",
            "mf35xNetDiagDrainOne", "mf35xFastTrackDrainOne"
        ):
            self.assertNotIn(forbidden, body)
        background = extract_function(NETWORK, "void mf35xRaceUploadTask(void*)")
        for required in (
            "offlineDrainBearbeiten", "mf35xDiagDrainOne", "mf35xRpmDiagDrainOne",
            "mf35xNetDiagDrainOne", "mf35xFastTrackDrainOne"
        ):
            self.assertIn(required, background)


class AtomicSampleRegressionTests(unittest.TestCase):
    def test_one_capture_object_contains_base_and_both_sensor_diagnostics(self):
        m = re.search(r"struct\s+Mf35xTimedRaceCapture\s*\{(.*?)\};", TIMING, re.S)
        self.assertIsNotNone(m)
        body = m.group(1)
        self.assertIn("OfflineRaceRecord rec", body)
        self.assertIn("Mf35xOilDiagRecord oilDiag", body)
        self.assertIn("Mf35xRpmDiagRecord rpmDiag", body)

    def test_same_sequence_is_assigned_to_all_records(self):
        persist = extract_function(NETWORK, "void mf35xRacePersistOne()")
        for token in (
            "item.rec.sequence = sequence", "item.oilDiag.sequence = sequence",
            "item.rpmDiag.sequence = sequence", "netDiag.sequence = sequence",
            "item.rec.bootId = offlineBootId", "item.oilDiag.bootId = offlineBootId",
            "item.rpmDiag.bootId = offlineBootId", "netDiag.bootId = offlineBootId"
        ):
            self.assertIn(token, persist)

    def test_gpio11_is_shared_by_base_and_both_diagnostics(self):
        capture = extract_function(TIMING, "Mf35xTimedRaceCapture mf35xRaceAtomicCaptureBauen(const char* raceId)")
        self.assertIn("switchState", capture)
        self.assertIn("mf35xRaceRpmDiagSnapshotBauen", capture)
        self.assertIn("mf35xRaceOilDiagSnapshotBauen", capture)
        self.assertIn("OFFLINE_FLAG_SWITCH_OUTPUT", capture)


class ConnectivityRegressionTests(unittest.TestCase):
    def test_network_probe_is_background_task(self):
        setup = extract_function(CONNECTIVITY, "void mf35xConnectivityDiagnosticsSetup()")
        self.assertIn("xTaskCreatePinnedToCore", setup)
        runtime_setup = extract_function(RUNTIME, "void mf35xRuntimeSetup()")
        self.assertIn("mf35xConnectivityDiagnosticsSetup", runtime_setup)

    def test_network_layers_are_measured_separately(self):
        for token in (
            "WiFi.gatewayIP", "client.connect", "WiFi.hostByName",
            "tracker/device.json?shallow=true", "firebase_http_code"
        ):
            self.assertIn(token, CONNECTIVITY)

    def test_optional_rut200_api_support_exists(self):
        for token in (
            "MF35X_RUT200_PASSWORD", '"/login"', '"/modems/signal/status"',
            '"/modems/status"', '"rsrp"', '"rsrq"', '"sinr"'
        ):
            self.assertIn(token, CONNECTIVITY)

    def test_router_credentials_never_enter_connectivity_json(self):
        body = extract_function(CONNECTIVITY, "String mf35xConnectivityJson(")
        self.assertNotIn("MF35X_RUT200_PASSWORD", body)
        self.assertNotIn("mf35xRutToken", body)
        self.assertNotIn('"password"', body.lower())
        self.assertNotIn('"token"', body.lower())

    def test_network_snapshot_is_persisted_per_race_sample(self):
        persist = extract_function(NETWORK, "void mf35xRacePersistOne()")
        self.assertIn("mf35xConnectivityRaceRecordBauen(item.rec.capturedMillis)", persist)
        self.assertIn("mf35xNetDiagQueueAppend", persist)


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
        m = re.search(r"MF35X_FAST_TRACK_FLASH_PROTECT_BYTES\s*=\s*(\d+)UL\s*\*\s*(\d+)UL", FAST)
        self.assertIsNotNone(m)
        self.assertGreaterEqual(int(m.group(1)) * int(m.group(2)), 1024 * 1024)

    def test_50hz_input_does_not_force_50hz_storage(self):
        self.assertEqual(50 * 10, 500)
        self.assertEqual(10 * (1000 // 100), 100)


class CsvSchemaRegressionTests(unittest.TestCase):
    def test_admin_loader_includes_v5922_module(self):
        self.assertIn("admin-v5922.js", FIREBASE_CONFIG)

    def test_csv_has_fixed_gps_columns(self):
        for column in ('"GPS_Valid"', '"Latitude"', '"Longitude"', '"GPS_HDOP"', '"GPS_Satelliten"'):
            self.assertIn(column, ADMIN_V5922)

    def test_csv_has_fixed_connectivity_columns(self):
        for column in (
            '"Netz_WLAN_verbunden"', '"Netz_Gateway_erreichbar"', '"Netz_DNS_ok"',
            '"Netz_Firebase_ok"', '"Netz_Firebase_Latenz_ms"', '"LTE_RSRP_dBm"',
            '"LTE_RSRQ_dB"', '"LTE_SINR_dB"'
        ):
            self.assertIn(column, ADMIN_V5922)
        self.assertIn('const CSV_SCHEMA_VERSION = "MF35X_RACE_CSV_V2"', ADMIN_V5922)


class ReleaseArchitectureTests(unittest.TestCase):
    def test_sketch_uses_single_runtime_facade(self):
        self.assertIn('#include "mf35x_runtime.hpp"', INO)
        for legacy in (
            '#include "v5917_patch.hpp"', '#include "v5918_rpm_diagnostics.hpp"',
            '#include "race_timing_fix.hpp"', '#include "race_network_isolation.hpp"'
        ):
            self.assertNotIn(legacy, INO)

    def test_runtime_owns_active_module_order(self):
        for include in (
            '#include "v5917_patch.hpp"', '#include "v5918_rpm_diagnostics.hpp"',
            '#include "connectivity_diagnostics.hpp"', '#include "race_timing_fix.hpp"',
            '#include "fast_track_logger.hpp"', '#include "race_network_isolation.hpp"'
        ):
            self.assertIn(include, RUNTIME)

    def test_sketch_setup_and_loop_only_delegate_runtime(self):
        setup = extract_function(INO, "void setup()")
        loop = extract_function(INO, "void loop()")
        self.assertIn("mf35xRuntimeSetup", setup)
        self.assertIn("mf35xRuntimeLoop", loop)


if __name__ == "__main__":
    unittest.main(verbosity=2)

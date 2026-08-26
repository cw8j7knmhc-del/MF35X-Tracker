#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <Adafruit_MAX31855.h>
#include <Preferences.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <limits.h>
#include "firmware_version.h"
#include "ota_public_key.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ==================================================
// MF35X LIVETRACKER V5.9.13 OTA SIGNED - ESP32-MAXWERTE/ALARME + OFFLINE-RENNSPEICHER + NETZUNABHAENGIGE GPIO11-STEUERUNG
// GPS + OELTEMP + OELDRUCK + BATTERIE + ZYLINDERTEMP
// + DREHZAHL + SCHALTAUSGANG + FIREBASE-KONFIGURATION
// + DYNAMISCHE UPDATEINTERVALLE + RENNHISTORIE
// + ADMIN-SYSTEMBEFEHLE: ESP32 / WLAN / GPS NEUSTART
// + ROBUSTER GPS-EMPFANG: 4-KB-UART-PUFFER + EIGENER FREERTOS-TASK
// + GPS 10 HZ FIX: KEINE BAUD-UMSCHALTUNG, RMC 10 HZ + GGA ~1,1 HZ + RATEN-DIAGNOSE
// + SIGNIERTES HTTPS-OTA + AUTOMATISCHER ROLLBACK-SCHUTZ
// ==================================================

// ==================================================
// WLAN
// ==================================================

// WLAN-Zugangsdaten werden fuer den oeffentlichen GitHub-Quellcode NICHT
// mehr fest im Sketch gespeichert. Normalfall nach dem einmaligen USB-Update:
// WiFi.begin() verwendet die bereits im ESP32-NVS gespeicherte AP-Konfiguration.
// Falls NVS geloescht wurde, kann lokal eine secrets.h angelegt werden.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef MF35X_WIFI_SSID
#define MF35X_WIFI_SSID ""
#endif

#ifndef MF35X_WIFI_PASSWORD
#define MF35X_WIFI_PASSWORD ""
#endif

const char* WLAN_NAME = MF35X_WIFI_SSID;
const char* WLAN_PASSWORT = MF35X_WIFI_PASSWORD;

// ==================================================
// FIREBASE
// ==================================================

const char* FIREBASE_ROOT =
  "https://tracker-989a9-default-rtdb.europe-west1.firebasedatabase.app";

constexpr unsigned long FIREBASE_CONFIG_CHECK_MS = 5000UL;
constexpr unsigned long DEVICE_STATUS_INTERVAL_MS = 10000UL;
constexpr unsigned long STATUS_INTERVAL_MS = 3000UL;
constexpr unsigned long WIFI_CHECK_INTERVAL_MS = 5000UL;

// ==================================================
// SIGNIERTES ONLINE-FIRMWAREUPDATE (OTA)
// ==================================================

// Feste Updatequelle. Ein Admin-Befehl darf diese URL NICHT ueberschreiben.
// Dadurch kann Firebase niemals eine beliebige Fremd-Firmware vorgeben.
const char* OTA_MANIFEST_URL =
  "https://raw.githubusercontent.com/cw8j7knmhc-del/MF35X-Tracker/main/firmware/manifest.json";
const char* OTA_FIRMWARE_URL =
  "https://raw.githubusercontent.com/cw8j7knmhc-del/MF35X-Tracker/main/firmware/MF35X_Livetracker_signed.bin";

constexpr unsigned long OTA_HTTP_TIMEOUT_MS = 15000UL;
constexpr unsigned long OTA_VALIDIERUNG_NACH_MS = 30000UL;

struct OtaManifest {
  String version;
  long versionCode;
  unsigned long sizeBytes;
  String md5;
};

// Signaturpruefer fuer alle OTA-Images. Der private Schluessel liegt NICHT
// im ESP32 und NICHT im GitHub-Repository.
UpdaterRSAVerifier otaSignaturPruefer(
  OTA_PUBLIC_KEY,
  OTA_PUBLIC_KEY_LEN,
  HASH_SHA256
);

bool otaValidierungErledigt = false;

// ==================================================
// HARDWARE
// ==================================================

constexpr int I2C_SDA = 8;
constexpr int I2C_SCL = 9;

constexpr int GPS_RX = 16;  // ESP32 RX <- GPS TX
constexpr int GPS_TX = 17;  // ESP32 TX -> GPS RX

// V5.9.7: GPS-Empfang bewusst vom HTTPS/Firebase-Upload entkoppelt.
// Der groessere UART-Puffer faengt kurze Blockierzeiten ab; der eigene
// FreeRTOS-Task liest die GPS-Daten parallel zur normalen loop()-Logik ein.
// V5.9.7: Das ATGM336H kann nach einem Reset / Verlust der gespeicherten
// Konfiguration wieder mit einer anderen Baudrate senden. Deshalb wird die
// Baudrate beim Start anhand GUELTIGER NMEA-Checksummen erkannt, nicht anhand
// der blossen Anzahl empfangener Bytes.
constexpr uint32_t GPS_BAUD_BEVORZUGT = 115200UL;
constexpr uint32_t GPS_BAUD_KANDIDATEN[] = {
  115200UL, 9600UL, 57600UL, 38400UL, 19200UL, 4800UL
};
constexpr size_t GPS_BAUD_KANDIDATEN_ANZAHL =
  sizeof(GPS_BAUD_KANDIDATEN) / sizeof(GPS_BAUD_KANDIDATEN[0]);
constexpr unsigned long GPS_BAUD_TEST_MS = 2200UL;
constexpr uint32_t GPS_BAUD_MIN_GUELTIGE_SAETZE = 2UL;

uint32_t gpsBaudAktiv = GPS_BAUD_BEVORZUGT;
bool gpsBaudErkannt = false;

// V5.9.10 GPS-Fix: 10-Hz-Konfiguration OHNE Baudratenwechsel.
// Der automatisch erkannte UART-Takt bleibt unangetastet.
// Die NMEA-Last wird bewusst reduziert:
// RMC bei jedem Fix (10 Hz), GGA bei jedem 9. Fix (~1,1 Hz).
//
// ATGM336H/CASIC-Firmware existiert mit unterschiedlichen PCAS03-Formaten.
// Deshalb werden die bekannten Formate von alt nach neu gesendet.
// Ein Empfaenger uebernimmt die von seiner Firmware verstandene Variante;
// die Baudrate wird dabei zu keinem Zeitpunkt veraendert.
constexpr uint32_t GPS_ZIELRATE_HZ = 10UL;
const char* GPS_CMD_NMEA_LEGACY_8 =
  "$PCAS03,9,0,0,0,1,0,0,0*0A\r\n";
const char* GPS_CMD_NMEA_EXTENDED_14 =
  "$PCAS03,9,0,0,0,1,0,0,0,0,0,,,0,0*0A\r\n";
const char* GPS_CMD_NMEA_FULL_19 =
  "$PCAS03,9,0,0,0,1,0,0,0,0,0,,,0,0,,,,0*3A\r\n";
const char* GPS_CMD_RATE_10HZ =
  "$PCAS02,100*1E\r\n";

bool gps10HzAngefordert = false;
volatile uint32_t gpsRmcSaetzeGesamt = 0;
volatile uint32_t gpsGgaSaetzeGesamt = 0;
float gpsRmcRateHz = NAN;
float gpsGgaRateHz = NAN;
bool gps10HzBestaetigt = false;
unsigned long gpsRmcMessungLetzteMs = 0;
uint32_t gpsRmcMessungLetzterZaehler = 0;
uint32_t gpsGgaMessungLetzterZaehler = 0;


constexpr size_t GPS_UART_RX_BUFFER_SIZE = 4096;
constexpr unsigned long GPS_FIX_TIMEOUT_MS = 5000UL;
constexpr uint32_t GPS_TASK_STACK_SIZE = 4096;
constexpr UBaseType_t GPS_TASK_PRIORITY = 3;

// V5.9.11: Zeitkritische W-Signal-/GPIO11-Steuerung laeuft in einem
// eigenen FreeRTOS-Task und ist damit unabhaengig von WLAN, LTE, Firebase,
// Website, HTTPS-Timeouts und Rennhistorie.
constexpr uint32_t CONTROL_TASK_STACK_SIZE = 3072;
constexpr UBaseType_t CONTROL_TASK_PRIORITY = 4;
constexpr TickType_t CONTROL_TASK_PERIOD_TICKS = pdMS_TO_TICKS(10);
constexpr BaseType_t CONTROL_TASK_CORE = 1;

// Drehzahl vom W-Anschluss ueber HY-M154 / Optokoppler
constexpr int RPM_PIN = 10;

// Geschaltener 3,3-V-Logikausgang
constexpr int SCHALTAUSGANG_PIN = 11;

// MAX31855 fuer Zylinderkopftemperatur
constexpr int MAX31855_CLK = 12;
constexpr int MAX31855_CS  = 13;
constexpr int MAX31855_DO  = 14;

constexpr int ADS_KANAL_OELTEMP = 0;   // AIN0
constexpr int ADS_KANAL_OELDRUCK = 1;  // AIN1
constexpr int ADS_KANAL_BATTERIE = 2;  // AIN2

// GPIO0 muss dauerhaft frei bleiben!

Adafruit_ADS1115 ads;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);
Preferences preferences;

// ==================================================
// GPS-SNAPSHOT / PARALLELER EMPFANG V5.9.7
// Nur der GPS-Task greift auf TinyGPSPlus direkt zu.
// Die Hauptschleife arbeitet ausschliesslich mit diesem geschuetzten Snapshot.
// ==================================================

struct GpsSnapshot {
  bool positionVorhanden;
  double lat;
  double lng;
  unsigned long letzterFixMillis;

  bool speedValid;
  double speedKmh;

  bool hdopValid;
  double hdop;

  bool satellitesValid;
  uint32_t satellites;

  // V5.9.12: GPS-UTC wird fuer netzunabhaengige Renn-Zeitstempel genutzt.
  bool utcValid;
  uint64_t utcEpochMs;
  unsigned long utcUpdateMillis;

  uint32_t charsProcessed;
  uint32_t passedChecksum;
  uint32_t failedChecksum;
  uint32_t sentencesWithFix;
};

GpsSnapshot gpsSnapshot = {};
portMUX_TYPE gpsSnapshotMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t gpsTaskHandle = nullptr;
SemaphoreHandle_t gpsUartMutex = nullptr;

Adafruit_MAX31855 thermocouple(
  MAX31855_CLK,
  MAX31855_CS,
  MAX31855_DO
);

// ==================================================
// WEBSITE/FIREBASE - STANDARDWERTE
// Diese Werte werden nur verwendet, wenn weder NVS noch
// Firebase bereits gueltige Werte liefern.
// ==================================================

struct OutputConfig {
  float speedEnableKmh;
  float rpmOn;
  float rpmOff;
};

struct IntervalConfig {
  unsigned long rpmFirebaseUpdateMs;
  unsigned long oilPressureUpdateMs;
  unsigned long temperatureUpdateMs;
  unsigned long gpsUpdateMs;
  unsigned long historyUpdateMs;
};

struct AlarmConfig {
  float batteryWarn;
  float batteryAlarm;
  float oilPressureWarn;
  float oilPressureAlarm;
  float oilTempWarn;
  float oilTempAlarm;
  float cylTempWarn;
  float cylTempAlarm;
};

struct RecordingConfig {
  bool enabled;
  String raceId;
  String raceName;
  unsigned long historyUpdateMs;
};

OutputConfig outputConfig = {
  60.0f,
  3200.0f,
  3150.0f
};

// Schuetzt die drei zusammengehoerigen Ausgangsschwellen beim Wechsel
// zwischen Firebase-/loop-Kontext und dem unabhaengigen Steuerungs-Task.
portMUX_TYPE outputConfigMux = portMUX_INITIALIZER_UNLOCKED;

IntervalConfig intervalConfig = {
  250UL,   // Drehzahl -> Firebase/Website
  100UL,   // Oeldruck -> Firebase/Website
  1000UL,  // Temperaturen
  1000UL,  // GPS Position + Geschwindigkeit
  5000UL   // Rennhistorie
};

AlarmConfig alarmConfig = {
  12.2f,
  11.8f,
  2.0f,
  1.2f,
  110.0f,
  125.0f,
  180.0f,
  220.0f
};

RecordingConfig recordingConfig = {
  false,
  "",
  "",
  5000UL
};

// ==================================================
// DREHZAHL
// ==================================================

// Muss einmal am Fahrzeug kalibriert werden.
// Formel: echte RPM / angezeigte Roh-RPM
constexpr float RPM_KALIBRIERFAKTOR = 1.0000f;

// Interne Drehzahlberechnung. Unabhaengig vom Firebase-Upload.
constexpr unsigned long RPM_MESSINTERVALL_MS = 250UL;
constexpr unsigned long RPM_SIGNAL_TIMEOUT_US = 500000UL;
constexpr unsigned long RPM_MIN_PULSABSTAND_US = 100UL;

volatile uint32_t rpmImpulse = 0;
volatile uint32_t letzterRpmImpulsUs = 0;

volatile float rpmRoh = 0.0f;
volatile float rpm = 0.0f;
volatile bool rpmSignalOk = false;
volatile bool schaltausgangAktiv = false;
TaskHandle_t controlTaskHandle = nullptr;

unsigned long letzteRpmMessung = 0;

// ==================================================
// OELTEMPERATUR
// ==================================================

constexpr float OEL_VCC = 3.30f;
constexpr float OEL_R_FIXED = 980.0f;

constexpr float OEL_CAL_R1 = 580.0f;
constexpr float OEL_CAL_T1 = 26.0f;

constexpr float OEL_CAL_R2 = 503.0f;
constexpr float OEL_CAL_T2 = 35.0f;

// Einpunkt-Korrektur bei nebeneinanderliegenden Sensoren:
// Oeltemperatur zeigte 39 C, MAX31855 zeigte 29 C.
constexpr float OEL_TEMP_OFFSET = 4.0f;

float oilVoltage = NAN;
float oilSensorOhm = NAN;
float oilTemp = NAN;

// ==================================================
// OELDRUCK
// ==================================================

constexpr float DRUCK_VCC = 3.27f;
constexpr float DRUCK_R_FIXED = 220.0f;
constexpr float DRUCK_NULL_OHM = 13.1f;

const float DRUCK_BAR_PUNKTE[] = {
  0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f
};

const float DRUCK_OHM_PUNKTE[] = {
  DRUCK_NULL_OHM, 52.0f, 88.0f, 124.0f, 155.0f, 184.0f
};

constexpr int DRUCK_ANZAHL_PUNKTE = 6;

float oilPressureVoltage = NAN;
float oilPressureOhm = NAN;
float oilPressureBar = NAN;

// ==================================================
// BATTERIESPANNUNG
// ==================================================

constexpr float BAT_R_OBEN = 100000.0f;
constexpr float BAT_R_UNTEN = 10000.0f;

constexpr float BAT_TEILERFAKTOR =
  (BAT_R_OBEN + BAT_R_UNTEN) / BAT_R_UNTEN;

constexpr float BAT_KORREKTUR = 1.0057f;

float batteryAdcVoltage = NAN;
float batteryVoltage = NAN;

// ==================================================
// ZYLINDERKOPFTEMPERATUR
// ==================================================

double cylinderTemp = NAN;
double max31855InternalTemp = NAN;
uint8_t max31855Fault = 0;
bool max31855Ok = false;

// ==================================================
// SYSTEMSTATUS / ZEITSTEUERUNG
// ==================================================

bool adsOk = false;
bool preferencesOk = false;
bool firebaseConfigOk = false;
bool firebaseSettingsOk = false;
String configQuelle = "STANDARD";

unsigned long letzterWlanCheck = 0;
unsigned long letzteStatusausgabe = 0;
unsigned long letzterConfigCheck = 0;
unsigned long letzterDeviceStatus = 0;
volatile uint32_t gpsBytesGesamt = 0;

// Zuletzt ausgefuehrte Admin-Systembefehle.
// Die IDs werden auch in NVS gespeichert, damit ein Befehl nach einem
// ESP32-Neustart nicht versehentlich erneut ausgefuehrt wird.
String letzterRebootCommandId;
String letzterWifiCommandId;
String letzterGpsCommandId;
String letzterOtaCommandId;

unsigned long letzterRpmUpload = 0;
unsigned long letzterOilPressureUpload = 0;
unsigned long letzterTemperatureUpload = 0;
unsigned long letzterGpsUpload = 0;
unsigned long letzterHistoryUpload = 0;

unsigned long uploadOk = 0;
unsigned long uploadFehler = 0;
unsigned long configLeseOk = 0;
unsigned long configLeseFehler = 0;
unsigned long historyOk = 0;
unsigned long historyFehler = 0;

int letzterHttpCode = 0;

// ==================================================
// VORWAERTSDEKLARATIONEN - FIX2
// ==================================================
// Der komplette Tracker-Code wird aus einer normalen Header-Datei eingebunden.
// Dadurch verarbeitet der Arduino-.ino-Praeprozessor nur die minimale .ino-Datei.
// Alle Funktionen sind deshalb hier explizit deklariert.
bool zeitFaellig(unsigned long jetzt, unsigned long zuletzt, unsigned long intervall);
bool floatAnders(float a, float b);
unsigned long kleinstesLiveIntervall();
int jsonWertStart(const String& json, const char* key);
bool jsonZahl(const String& json, const char* key, double& wert);
bool jsonBool(const String& json, const char* key, bool& wert);
bool jsonString(const String& json, const char* key, String& wert);
bool jsonObjekt(const String& json, const char* key, String& objekt);
String jsonEscape(const String& text);
void jsonKomma(String& json, bool& erstesFeld);
void jsonRaw(String& json, bool& erstesFeld, const char* key, const String& raw);
void jsonText(String& json, bool& erstesFeld, const char* key, const String& text);
void jsonBoolFeld(String& json, bool& erstesFeld, const char* key, bool wert);
void jsonFloatFeld(String& json, bool& erstesFeld, const char* key, double wert, int stellen);
void jsonULongFeld(String& json, bool& erstesFeld, const char* key, unsigned long wert);
void lokaleKonfigurationLaden();
void lokaleKonfigurationSpeichern();
void commandIdSpeichern(const char* key, const String& id);
String firebaseUrl(const String& pfad);
bool firebaseGet(const String& pfad, String& antwort);
bool firebasePatch(const String& pfad, const String& json, bool alsLiveUpload);
bool firebasePost(const String& pfad, const String& json);
bool firebasePut(const String& pfad, const String& json);
bool outputConfigUebernehmen(const String& configJson);
bool intervalConfigUebernehmen(const String& configJson);
bool recordingConfigUebernehmen(const String& configJson);
bool alarmConfigUebernehmen(const String& settingsJson);
void firebaseKonfigurationLaden(bool statusMelden);
void IRAM_ATTR rpmImpulsISR();
void drehzahlAktualisieren();
void schaltausgangAktualisieren();
void steuerungTask(void* parameter);
void steuerungTaskStarten();
float widerstandZuBar(float widerstand);
GpsSnapshot gpsSnapshotLesen();
void gpsSnapshotZuruecksetzen();
bool gpsFixAktuell(const GpsSnapshot& daten);
bool gpsFixAktuell();
bool gpsBaudTesten( uint32_t baud, uint32_t& gueltigeSaetze, uint32_t& fehlerhafteSaetze );
void gpsUartAufAktiveBaudSetzen();
void gpsBaudAutomatischErkennen();
void gpsBefehlSenden(const char* befehl);
bool gps10HzSicherKonfigurieren();
void gpsNmeaSatzTypErfassen(char zeichen, bool& aktuellerSatzRmc, bool& aktuellerSatzGga);
void gpsRmcRateAktualisieren();
void gpsParserSnapshotAktualisieren();
void gpsZeichenEinlesen();
void gpsEmpfangTask(void* parameter);
void gpsEinlesen();
void gpsTaskStarten();
void gpsSoftwareNeustart();
void wlanVerbindungStarten();
void wlanVerbinden();
void wlanPruefen();
void wlanSoftwareNeustart();
void oeltemperaturLesen();
void oeldruckLesen();
void batteriespannungLesen();
void zylinderTemperaturLesen();
void temperaturenUndBatterieLesen();
void alleSensorenEinmalLesen();
String liveJsonBauen(bool gpsDue, bool rpmDue, bool oilDue, bool tempDue);
String historyJsonBauen();
void liveUpdatesBearbeiten();
void rennhistorieBearbeiten();
bool otaPartitionVorhanden();
bool otaRollbackAutomatischAktiv();
bool otaIstPendingVerify();
void otaFirmwareValidierenWennBereit();
bool otaManifestLaden(OtaManifest& manifest, String& fehler);
bool otaUpdateAusfuehren(const String& requestId);
bool commandAusObjektLesen( const String& commandsJson, const char* commandName, String& requestId, String& status );
void commandStatusSenden(const char* commandName, const String& requestId, const char* status, const String& message = "");
void systemBefehlePruefen(const String& configJson);
void deviceStatusSenden();
void statusAusgeben();

// ==================================================
// HILFSFUNKTIONEN - ZEIT / VERGLEICH
// ==================================================

bool zeitFaellig(unsigned long jetzt, unsigned long zuletzt, unsigned long intervall) {
  return (unsigned long)(jetzt - zuletzt) >= intervall;
}

bool floatAnders(float a, float b) {
  return fabsf(a - b) > 0.0001f;
}

unsigned long kleinstesLiveIntervall() {
  unsigned long m = intervalConfig.rpmFirebaseUpdateMs;
  if (intervalConfig.oilPressureUpdateMs < m) m = intervalConfig.oilPressureUpdateMs;
  if (intervalConfig.temperatureUpdateMs < m) m = intervalConfig.temperatureUpdateMs;
  if (intervalConfig.gpsUpdateMs < m) m = intervalConfig.gpsUpdateMs;
  return m;
}

// ==================================================
// JSON-HILFSFUNKTIONEN
// Keine zusaetzliche ArduinoJson-Bibliothek erforderlich.
// ==================================================

int jsonWertStart(const String& json, const char* key) {
  String marker = "\"";
  marker += key;
  marker += "\"";

  int pos = json.indexOf(marker);
  if (pos < 0) return -1;

  pos = json.indexOf(':', pos + marker.length());
  if (pos < 0) return -1;

  pos++;
  while (pos < (int)json.length() &&
         (json[pos] == ' ' || json[pos] == '\t' ||
          json[pos] == '\r' || json[pos] == '\n')) {
    pos++;
  }

  return pos;
}

bool jsonZahl(const String& json, const char* key, double& wert) {
  int pos = jsonWertStart(json, key);
  if (pos < 0 || pos >= (int)json.length()) return false;

  const char* start = json.c_str() + pos;
  char* ende = nullptr;
  double v = strtod(start, &ende);

  if (ende == start || !isfinite(v)) return false;

  wert = v;
  return true;
}

bool jsonBool(const String& json, const char* key, bool& wert) {
  int pos = jsonWertStart(json, key);
  if (pos < 0) return false;

  if (json.substring(pos, pos + 4) == "true") {
    wert = true;
    return true;
  }

  if (json.substring(pos, pos + 5) == "false") {
    wert = false;
    return true;
  }

  return false;
}

bool jsonString(const String& json, const char* key, String& wert) {
  int pos = jsonWertStart(json, key);
  if (pos < 0 || pos >= (int)json.length() || json[pos] != '"') {
    return false;
  }

  pos++;
  String out;
  bool escape = false;

  for (; pos < (int)json.length(); pos++) {
    char c = json[pos];

    if (escape) {
      switch (c) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        default: out += c; break;
      }
      escape = false;
      continue;
    }

    if (c == '\\') {
      escape = true;
      continue;
    }

    if (c == '"') {
      wert = out;
      return true;
    }

    out += c;
  }

  return false;
}

bool jsonObjekt(const String& json, const char* key, String& objekt) {
  int pos = jsonWertStart(json, key);
  if (pos < 0 || pos >= (int)json.length() || json[pos] != '{') {
    return false;
  }

  int start = pos;
  int tiefe = 0;
  bool inString = false;
  bool escape = false;

  for (; pos < (int)json.length(); pos++) {
    char c = json[pos];

    if (inString) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }

    if (c == '"') {
      inString = true;
      continue;
    }

    if (c == '{') tiefe++;
    if (c == '}') {
      tiefe--;
      if (tiefe == 0) {
        objekt = json.substring(start, pos + 1);
        return true;
      }
    }
  }

  return false;
}

String jsonEscape(const String& text) {
  String out;
  out.reserve(text.length() + 8);

  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += c; break;
    }
  }

  return out;
}

void jsonKomma(String& json, bool& erstesFeld) {
  if (!erstesFeld) json += ',';
  erstesFeld = false;
}

void jsonRaw(String& json, bool& erstesFeld, const char* key, const String& raw) {
  jsonKomma(json, erstesFeld);
  json += '"';
  json += key;
  json += "\":";
  json += raw;
}

void jsonText(String& json, bool& erstesFeld, const char* key, const String& text) {
  jsonKomma(json, erstesFeld);
  json += '"';
  json += key;
  json += "\":\"";
  json += jsonEscape(text);
  json += '"';
}

void jsonBoolFeld(String& json, bool& erstesFeld, const char* key, bool wert) {
  jsonRaw(json, erstesFeld, key, wert ? "true" : "false");
}

void jsonFloatFeld(String& json, bool& erstesFeld, const char* key, double wert, int stellen) {
  if (isnan(wert) || !isfinite(wert)) {
    jsonRaw(json, erstesFeld, key, "null");
  } else {
    jsonRaw(json, erstesFeld, key, String(wert, stellen));
  }
}

void jsonULongFeld(String& json, bool& erstesFeld, const char* key, unsigned long wert) {
  jsonRaw(json, erstesFeld, key, String(wert));
}

// ==================================================
// NVS / PREFERENCES
// ==================================================

void lokaleKonfigurationLaden() {
  preferencesOk = preferences.begin("mf35x", false);

  if (!preferencesOk) {
    Serial.println("WARNUNG: Preferences/NVS konnte nicht geoeffnet werden.");
    return;
  }

  outputConfig.speedEnableKmh = preferences.getFloat("spd", outputConfig.speedEnableKmh);
  outputConfig.rpmOn = preferences.getFloat("rpon", outputConfig.rpmOn);
  outputConfig.rpmOff = preferences.getFloat("rpoff", outputConfig.rpmOff);

  intervalConfig.rpmFirebaseUpdateMs = preferences.getULong("rint", intervalConfig.rpmFirebaseUpdateMs);
  intervalConfig.oilPressureUpdateMs = preferences.getULong("opint", intervalConfig.oilPressureUpdateMs);
  intervalConfig.temperatureUpdateMs = preferences.getULong("tint", intervalConfig.temperatureUpdateMs);
  intervalConfig.gpsUpdateMs = preferences.getULong("gint", intervalConfig.gpsUpdateMs);
  intervalConfig.historyUpdateMs = preferences.getULong("hint", intervalConfig.historyUpdateMs);

  // V5.9.12: Laufende Rennaufzeichnung ebenfalls lokal merken. Dadurch kann
  // nach einem Stromausfall auch ohne sofortige Firebase-Verbindung weiter
  // aufgezeichnet und in den Offline-Puffer geschrieben werden.
  recordingConfig.enabled = preferences.getBool("rec_en", false);
  recordingConfig.raceId = preferences.getString("rec_id", "");
  recordingConfig.raceName = preferences.getString("rec_name", "");
  recordingConfig.historyUpdateMs = preferences.getULong("rec_int", intervalConfig.historyUpdateMs);
  if (recordingConfig.raceId.length() == 0) recordingConfig.enabled = false;
  if (recordingConfig.historyUpdateMs < 1000UL || recordingConfig.historyUpdateMs > 60000UL) {
    recordingConfig.historyUpdateMs = intervalConfig.historyUpdateMs;
  }

  alarmConfig.batteryWarn = preferences.getFloat("bw", alarmConfig.batteryWarn);
  alarmConfig.batteryAlarm = preferences.getFloat("ba", alarmConfig.batteryAlarm);
  alarmConfig.oilPressureWarn = preferences.getFloat("opw", alarmConfig.oilPressureWarn);
  alarmConfig.oilPressureAlarm = preferences.getFloat("opa", alarmConfig.oilPressureAlarm);
  alarmConfig.oilTempWarn = preferences.getFloat("otw", alarmConfig.oilTempWarn);
  alarmConfig.oilTempAlarm = preferences.getFloat("ota", alarmConfig.oilTempAlarm);
  alarmConfig.cylTempWarn = preferences.getFloat("ctw", alarmConfig.cylTempWarn);
  alarmConfig.cylTempAlarm = preferences.getFloat("cta", alarmConfig.cylTempAlarm);

  letzterRebootCommandId = preferences.getString("cmd_rb", "");
  letzterWifiCommandId = preferences.getString("cmd_wifi", "");
  letzterGpsCommandId = preferences.getString("cmd_gps", "");
  letzterOtaCommandId = preferences.getString("cmd_ota", "");

  // Plausibilitaet der NVS-Werte. Bei ungueltigen Altwerten Standard setzen.
  if (outputConfig.speedEnableKmh < 0.0f || outputConfig.speedEnableKmh > 200.0f ||
      outputConfig.rpmOn < 0.0f || outputConfig.rpmOn > 10000.0f ||
      outputConfig.rpmOff < 0.0f || outputConfig.rpmOff > 10000.0f ||
      outputConfig.rpmOff >= outputConfig.rpmOn) {
    outputConfig = {60.0f, 3200.0f, 3150.0f};
  }

  if (intervalConfig.rpmFirebaseUpdateMs < 100UL || intervalConfig.rpmFirebaseUpdateMs > 5000UL) {
    intervalConfig.rpmFirebaseUpdateMs = 250UL;
  }
  if (intervalConfig.oilPressureUpdateMs < 50UL || intervalConfig.oilPressureUpdateMs > 5000UL) {
    intervalConfig.oilPressureUpdateMs = 100UL;
  }
  if (intervalConfig.temperatureUpdateMs < 250UL || intervalConfig.temperatureUpdateMs > 10000UL) {
    intervalConfig.temperatureUpdateMs = 1000UL;
  }
  if (intervalConfig.gpsUpdateMs < 100UL || intervalConfig.gpsUpdateMs > 3000UL) {
    intervalConfig.gpsUpdateMs = 1000UL;
  }
  if (intervalConfig.historyUpdateMs < 1000UL || intervalConfig.historyUpdateMs > 60000UL) {
    intervalConfig.historyUpdateMs = 5000UL;
  }

  configQuelle = "NVS";
  Serial.println("Letzte gueltige Website-Konfiguration aus NVS geladen.");
}

void lokaleKonfigurationSpeichern() {
  if (!preferencesOk) return;

  preferences.putFloat("spd", outputConfig.speedEnableKmh);
  preferences.putFloat("rpon", outputConfig.rpmOn);
  preferences.putFloat("rpoff", outputConfig.rpmOff);

  preferences.putULong("rint", intervalConfig.rpmFirebaseUpdateMs);
  preferences.putULong("opint", intervalConfig.oilPressureUpdateMs);
  preferences.putULong("tint", intervalConfig.temperatureUpdateMs);
  preferences.putULong("gint", intervalConfig.gpsUpdateMs);
  preferences.putULong("hint", intervalConfig.historyUpdateMs);

  preferences.putBool("rec_en", recordingConfig.enabled);
  preferences.putString("rec_id", recordingConfig.raceId);
  preferences.putString("rec_name", recordingConfig.raceName);
  preferences.putULong("rec_int", recordingConfig.historyUpdateMs);

  preferences.putFloat("bw", alarmConfig.batteryWarn);
  preferences.putFloat("ba", alarmConfig.batteryAlarm);
  preferences.putFloat("opw", alarmConfig.oilPressureWarn);
  preferences.putFloat("opa", alarmConfig.oilPressureAlarm);
  preferences.putFloat("otw", alarmConfig.oilTempWarn);
  preferences.putFloat("ota", alarmConfig.oilTempAlarm);
  preferences.putFloat("ctw", alarmConfig.cylTempWarn);
  preferences.putFloat("cta", alarmConfig.cylTempAlarm);
}

void commandIdSpeichern(const char* key, const String& id) {
  if (!preferencesOk) return;
  preferences.putString(key, id);
}

// ==================================================
// FIREBASE REST HILFSFUNKTIONEN
// ==================================================

String firebaseUrl(const String& pfad) {
  String url = FIREBASE_ROOT;
  url += '/';
  url += pfad;
  url += ".json";
  return url;
}

bool firebaseGet(const String& pfad, String& antwort) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(4000);

  String url = firebaseUrl(pfad);

  if (!http.begin(client, url)) {
    letzterHttpCode = -1;
    return false;
  }

  int code = http.GET();
  letzterHttpCode = code;

  if (code == 200) {
    antwort = http.getString();
    http.end();
    gpsEinlesen();
    return true;
  }

  http.end();
  gpsEinlesen();
  return false;
}

bool firebasePatch(const String& pfad, const String& json, bool alsLiveUpload) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(4000);

  String url = firebaseUrl(pfad);

  if (!http.begin(client, url)) {
    letzterHttpCode = -1;
    if (alsLiveUpload) uploadFehler++;
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  int code = http.sendRequest("PATCH", json);
  letzterHttpCode = code;

  bool ok = (code == 200);

  if (alsLiveUpload) {
    if (ok) uploadOk++;
    else uploadFehler++;
  }

  http.end();
  gpsEinlesen();
  return ok;
}

bool firebasePost(const String& pfad, const String& json) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(4000);

  String url = firebaseUrl(pfad);

  if (!http.begin(client, url)) {
    letzterHttpCode = -1;
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  letzterHttpCode = code;

  bool ok = (code == 200);
  http.end();
  gpsEinlesen();
  return ok;
}

// V5.9.12: Deterministischer PUT fuer Rennsamples. Derselbe Messpunkt kann
// nach einem Timeout gefahrlos erneut gesendet werden, ohne Duplikate zu erzeugen.
bool firebasePut(const String& pfad, const String& json) {
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(4000);

  String url = firebaseUrl(pfad);
  if (!http.begin(client, url)) {
    letzterHttpCode = -1;
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  int code = http.PUT(json);
  letzterHttpCode = code;
  bool ok = (code == 200);
  http.end();
  gpsEinlesen();
  return ok;
}

// ==================================================
// WEBSITE-KONFIGURATION AUS FIREBASE LESEN
// ==================================================

bool outputConfigUebernehmen(const String& configJson) {
  String section;
  if (!jsonObjekt(configJson, "external_output", section)) return false;

  double speed, rpmOn, rpmOff;
  if (!jsonZahl(section, "speed_enable_kmh", speed) ||
      !jsonZahl(section, "rpm_on", rpmOn) ||
      !jsonZahl(section, "rpm_off", rpmOff)) {
    return false;
  }

  if (speed < 0.0 || speed > 200.0 ||
      rpmOn < 0.0 || rpmOn > 10000.0 ||
      rpmOff < 0.0 || rpmOff > 10000.0 ||
      rpmOff >= rpmOn) {
    Serial.println("WARNUNG: Ungueltige Ausgangskonfiguration in Firebase ignoriert.");
    return false;
  }

  portENTER_CRITICAL(&outputConfigMux);
  outputConfig.speedEnableKmh = (float)speed;
  outputConfig.rpmOn = (float)rpmOn;
  outputConfig.rpmOff = (float)rpmOff;
  portEXIT_CRITICAL(&outputConfigMux);
  return true;
}

bool intervalConfigUebernehmen(const String& configJson) {
  String section;
  if (!jsonObjekt(configJson, "intervals", section)) return false;

  double rpmInt, oilInt, tempInt, gpsInt, historyInt;

  if (!jsonZahl(section, "rpm_firebase_update_ms", rpmInt) ||
      !jsonZahl(section, "oil_pressure_update_ms", oilInt) ||
      !jsonZahl(section, "temperature_update_ms", tempInt) ||
      !jsonZahl(section, "gps_update_ms", gpsInt) ||
      !jsonZahl(section, "history_update_ms", historyInt)) {
    return false;
  }

  if (rpmInt < 100.0 || rpmInt > 5000.0 ||
      oilInt < 50.0 || oilInt > 5000.0 ||
      tempInt < 250.0 || tempInt > 10000.0 ||
      gpsInt < 100.0 || gpsInt > 3000.0 ||
      historyInt < 1000.0 || historyInt > 60000.0) {
    Serial.println("WARNUNG: Ungueltige Updateintervalle in Firebase ignoriert.");
    return false;
  }

  intervalConfig.rpmFirebaseUpdateMs = (unsigned long)rpmInt;
  intervalConfig.oilPressureUpdateMs = (unsigned long)oilInt;
  intervalConfig.temperatureUpdateMs = (unsigned long)tempInt;
  intervalConfig.gpsUpdateMs = (unsigned long)gpsInt;
  intervalConfig.historyUpdateMs = (unsigned long)historyInt;

  return true;
}

bool recordingConfigUebernehmen(const String& configJson) {
  String section;
  if (!jsonObjekt(configJson, "recording", section)) {
    recordingConfig.enabled = false;
    recordingConfig.raceId = "";
    recordingConfig.raceName = "";
    recordingConfig.historyUpdateMs = intervalConfig.historyUpdateMs;
    return true;
  }

  bool enabled = false;
  jsonBool(section, "enabled", enabled);

  String raceId;
  String raceName;
  jsonString(section, "raceId", raceId);
  jsonString(section, "raceName", raceName);

  double historyInt = (double)intervalConfig.historyUpdateMs;
  jsonZahl(section, "history_update_ms", historyInt);

  if (historyInt < 1000.0 || historyInt > 60000.0) {
    historyInt = (double)intervalConfig.historyUpdateMs;
  }

  // Nur mit gueltiger Renn-ID wirklich aufzeichnen.
  if (enabled && raceId.length() == 0) {
    enabled = false;
  }

  recordingConfig.enabled = enabled;
  recordingConfig.raceId = raceId;
  recordingConfig.raceName = raceName;
  recordingConfig.historyUpdateMs = (unsigned long)historyInt;
  return true;
}

bool alarmConfigUebernehmen(const String& settingsJson) {
  double bw, ba, opw, opa, otw, ota, ctw, cta;

  if (!jsonZahl(settingsJson, "batteryWarn", bw) ||
      !jsonZahl(settingsJson, "batteryAlarm", ba) ||
      !jsonZahl(settingsJson, "oilPressureWarn", opw) ||
      !jsonZahl(settingsJson, "oilPressureAlarm", opa) ||
      !jsonZahl(settingsJson, "oilTempWarn", otw) ||
      !jsonZahl(settingsJson, "oilTempAlarm", ota) ||
      !jsonZahl(settingsJson, "cylTempWarn", ctw) ||
      !jsonZahl(settingsJson, "cylTempAlarm", cta)) {
    return false;
  }

  alarmConfig.batteryWarn = (float)bw;
  alarmConfig.batteryAlarm = (float)ba;
  alarmConfig.oilPressureWarn = (float)opw;
  alarmConfig.oilPressureAlarm = (float)opa;
  alarmConfig.oilTempWarn = (float)otw;
  alarmConfig.oilTempAlarm = (float)ota;
  alarmConfig.cylTempWarn = (float)ctw;
  alarmConfig.cylTempAlarm = (float)cta;
  return true;
}

void firebaseKonfigurationLaden(bool statusMelden) {
  if (WiFi.status() != WL_CONNECTED) return;

  OutputConfig outputVorher = outputConfig;
  IntervalConfig intervalVorher = intervalConfig;
  AlarmConfig alarmVorher = alarmConfig;
  RecordingConfig recordingVorher = recordingConfig;

  String configJson;
  String settingsJson;

  bool configGetOk = firebaseGet("tracker/config", configJson);
  bool settingsGetOk = firebaseGet("tracker/settings", settingsJson);

  firebaseConfigOk = false;
  firebaseSettingsOk = false;

  if (configGetOk) {
    bool outOk = outputConfigUebernehmen(configJson);
    bool intOk = intervalConfigUebernehmen(configJson);
    recordingConfigUebernehmen(configJson);
    firebaseConfigOk = outOk && intOk;
  }

  if (settingsGetOk) {
    firebaseSettingsOk = alarmConfigUebernehmen(settingsJson);
  }

  bool geaendert =
    floatAnders(outputVorher.speedEnableKmh, outputConfig.speedEnableKmh) ||
    floatAnders(outputVorher.rpmOn, outputConfig.rpmOn) ||
    floatAnders(outputVorher.rpmOff, outputConfig.rpmOff) ||
    intervalVorher.rpmFirebaseUpdateMs != intervalConfig.rpmFirebaseUpdateMs ||
    intervalVorher.oilPressureUpdateMs != intervalConfig.oilPressureUpdateMs ||
    intervalVorher.temperatureUpdateMs != intervalConfig.temperatureUpdateMs ||
    intervalVorher.gpsUpdateMs != intervalConfig.gpsUpdateMs ||
    intervalVorher.historyUpdateMs != intervalConfig.historyUpdateMs ||
    recordingVorher.enabled != recordingConfig.enabled ||
    recordingVorher.raceId != recordingConfig.raceId ||
    recordingVorher.raceName != recordingConfig.raceName ||
    recordingVorher.historyUpdateMs != recordingConfig.historyUpdateMs ||
    floatAnders(alarmVorher.batteryWarn, alarmConfig.batteryWarn) ||
    floatAnders(alarmVorher.batteryAlarm, alarmConfig.batteryAlarm) ||
    floatAnders(alarmVorher.oilPressureWarn, alarmConfig.oilPressureWarn) ||
    floatAnders(alarmVorher.oilPressureAlarm, alarmConfig.oilPressureAlarm) ||
    floatAnders(alarmVorher.oilTempWarn, alarmConfig.oilTempWarn) ||
    floatAnders(alarmVorher.oilTempAlarm, alarmConfig.oilTempAlarm) ||
    floatAnders(alarmVorher.cylTempWarn, alarmConfig.cylTempWarn) ||
    floatAnders(alarmVorher.cylTempAlarm, alarmConfig.cylTempAlarm);

  if (firebaseConfigOk || firebaseSettingsOk) {
    configQuelle = "FIREBASE";
    configLeseOk++;

    if (geaendert) {
      lokaleKonfigurationSpeichern();
      if (statusMelden) {
        Serial.println("Website-Konfiguration geaendert und dauerhaft gespeichert.");
      }
    }
  } else {
    configLeseFehler++;
    if (statusMelden) {
      Serial.println("WARNUNG: Firebase-Konfiguration nicht vollstaendig lesbar - letzte Werte bleiben aktiv.");
    }
  }

  // Systembefehle werden aus demselben bereits geladenen configJson gelesen.
  if (configGetOk) {
    systemBefehlePruefen(configJson);
  }
}

// ==================================================
// DREHZAHL - INTERRUPT UND AUSWERTUNG
// ==================================================

void IRAM_ATTR rpmImpulsISR() {
  uint32_t jetztUs = micros();

  if (jetztUs - letzterRpmImpulsUs >= RPM_MIN_PULSABSTAND_US) {
    rpmImpulse++;
    letzterRpmImpulsUs = jetztUs;
  }
}

void drehzahlAktualisieren() {
  unsigned long jetztMs = millis();

  if (!zeitFaellig(jetztMs, letzteRpmMessung, RPM_MESSINTERVALL_MS)) {
    return;
  }

  unsigned long messdauerMs = jetztMs - letzteRpmMessung;
  letzteRpmMessung = jetztMs;

  uint32_t impulse;
  uint32_t letzterImpulsUs;

  noInterrupts();
  impulse = rpmImpulse;
  rpmImpulse = 0;
  letzterImpulsUs = letzterRpmImpulsUs;
  interrupts();

  uint32_t jetztUs = micros();

  rpmSignalOk =
    letzterImpulsUs != 0 &&
    (uint32_t)(jetztUs - letzterImpulsUs) <= RPM_SIGNAL_TIMEOUT_US;

  if (!rpmSignalOk || messdauerMs == 0) {
    rpmRoh = 0.0f;
    rpm = 0.0f;
    return;
  }

  float frequenzHz =
    (float)impulse * 1000.0f / (float)messdauerMs;

  rpmRoh = frequenzHz * 60.0f;
  rpm = rpmRoh * RPM_KALIBRIERFAKTOR;
}

void schaltausgangAktualisieren() {
  // Die Geschwindigkeit ist NUR die Freigabe.
  // Die Admin-Seite beschreibt "erreicht oder ueberschritten".
  GpsSnapshot gpsDaten = gpsSnapshotLesen();

  // Alle drei Grenzwerte als einen konsistenten Satz uebernehmen.
  OutputConfig cfg;
  portENTER_CRITICAL(&outputConfigMux);
  cfg = outputConfig;
  portEXIT_CRITICAL(&outputConfigMux);

  bool speedFreigabe =
    gpsFixAktuell(gpsDaten) &&
    gpsDaten.speedValid &&
    gpsDaten.speedKmh >= cfg.speedEnableKmh;

  // Fail-safe: ohne GPS-Freigabe oder ohne RPM-Signal immer LOW.
  if (!speedFreigabe || !rpmSignalOk) {
    schaltausgangAktiv = false;
  } else {
    if (!schaltausgangAktiv && rpm >= cfg.rpmOn) {
      schaltausgangAktiv = true;
    }

    if (schaltausgangAktiv && rpm < cfg.rpmOff) {
      schaltausgangAktiv = false;
    }
  }

  digitalWrite(
    SCHALTAUSGANG_PIN,
    schaltausgangAktiv ? HIGH : LOW
  );
}

// V5.9.11: Dieser Task entkoppelt die komplette lokale Schaltentscheidung
// vom Netzwerk. Auch waehrend blockierender HTTPS-/Firebase-Aufrufe werden
// W-Signal, RPM, GPS-Freigabe, Hysterese und GPIO11 weiter bearbeitet.
void steuerungTask(void* parameter) {
  (void)parameter;
  TickType_t letzterStart = xTaskGetTickCount();

  for (;;) {
    drehzahlAktualisieren();
    schaltausgangAktualisieren();
    vTaskDelayUntil(&letzterStart, CONTROL_TASK_PERIOD_TICKS);
  }
}

void steuerungTaskStarten() {
  if (controlTaskHandle != nullptr) return;

  BaseType_t ergebnis = xTaskCreatePinnedToCore(
    steuerungTask,
    "MF35X_CTRL",
    CONTROL_TASK_STACK_SIZE,
    nullptr,
    CONTROL_TASK_PRIORITY,
    &controlTaskHandle,
    CONTROL_TASK_CORE
  );

  if (ergebnis == pdPASS) {
    Serial.println("Steuerungs-Task GPIO10/GPIO11: OK (netzunabhaengig)");
  } else {
    controlTaskHandle = nullptr;
    digitalWrite(SCHALTAUSGANG_PIN, LOW);
    schaltausgangAktiv = false;
    Serial.println("WARNUNG: Steuerungs-Task konnte nicht gestartet werden - loop-Fallback aktiv.");
  }
}

// ==================================================
// HILFSFUNKTION: OELDRUCK-KENNLINIE
// ==================================================

float widerstandZuBar(float widerstand) {
  if (widerstand <= DRUCK_OHM_PUNKTE[0]) {
    return 0.0f;
  }

  if (widerstand >= DRUCK_OHM_PUNKTE[DRUCK_ANZAHL_PUNKTE - 1]) {
    return 10.0f;
  }

  for (int i = 0; i < DRUCK_ANZAHL_PUNKTE - 1; i++) {
    float r1 = DRUCK_OHM_PUNKTE[i];
    float r2 = DRUCK_OHM_PUNKTE[i + 1];

    if (widerstand >= r1 && widerstand <= r2) {
      float p1 = DRUCK_BAR_PUNKTE[i];
      float p2 = DRUCK_BAR_PUNKTE[i + 1];

      return p1 +
             (widerstand - r1) *
             (p2 - p1) /
             (r2 - r1);
    }
  }

  return NAN;
}

// ==================================================
// GPS
// ==================================================

GpsSnapshot gpsSnapshotLesen() {
  GpsSnapshot kopie;

  portENTER_CRITICAL(&gpsSnapshotMux);
  kopie = gpsSnapshot;
  portEXIT_CRITICAL(&gpsSnapshotMux);

  return kopie;
}

void gpsSnapshotZuruecksetzen() {
  portENTER_CRITICAL(&gpsSnapshotMux);
  gpsSnapshot = {};
  portEXIT_CRITICAL(&gpsSnapshotMux);
}

bool gpsFixAktuell(const GpsSnapshot& daten) {
  if (!daten.positionVorhanden || daten.letzterFixMillis == 0) {
    return false;
  }

  return (unsigned long)(millis() - daten.letzterFixMillis) <
         GPS_FIX_TIMEOUT_MS;
}

bool gpsFixAktuell() {
  return gpsFixAktuell(gpsSnapshotLesen());
}

// Testet eine Baudrate ausschliesslich lesend. Es werden KEINE
// Konfigurationsbefehle an das GPS gesendet. Das ist absichtlich konservativ:
// Ein funktionierender GPS-Empfaenger wird dadurch nicht umkonfiguriert.
bool gpsBaudTesten(
  uint32_t baud,
  uint32_t& gueltigeSaetze,
  uint32_t& fehlerhafteSaetze
) {
  gpsSerial.end();
  delay(80);

  gpsSerial.setRxBufferSize(GPS_UART_RX_BUFFER_SIZE);
  gpsSerial.begin(baud, SERIAL_8N1, GPS_RX, GPS_TX);

  TinyGPSPlus testParser;
  unsigned long start = millis();

  while ((unsigned long)(millis() - start) < GPS_BAUD_TEST_MS) {
    while (gpsSerial.available()) {
      testParser.encode((char)gpsSerial.read());
    }
    delay(1);
  }

  gueltigeSaetze = testParser.passedChecksum();
  fehlerhafteSaetze = testParser.failedChecksum();

  return gueltigeSaetze >= GPS_BAUD_MIN_GUELTIGE_SAETZE;
}

void gpsUartAufAktiveBaudSetzen() {
  gpsSerial.end();
  delay(80);

  gps = TinyGPSPlus();
  gpsBytesGesamt = 0;
  gpsSnapshotZuruecksetzen();

  gpsSerial.setRxBufferSize(GPS_UART_RX_BUFFER_SIZE);
  gpsSerial.begin(gpsBaudAktiv, SERIAL_8N1, GPS_RX, GPS_TX);
}

void gpsBaudAutomatischErkennen() {
  Serial.println("GPS Baudraten-Erkennung: starte ...");

  gpsBaudErkannt = false;

  for (size_t i = 0; i < GPS_BAUD_KANDIDATEN_ANZAHL; i++) {
    uint32_t baud = GPS_BAUD_KANDIDATEN[i];
    uint32_t ok = 0;
    uint32_t fehler = 0;

    Serial.print("  Test ");
    Serial.print(baud);
    Serial.print(" Baud: ");

    bool erkannt = gpsBaudTesten(baud, ok, fehler);

    Serial.print("NMEA OK=");
    Serial.print(ok);
    Serial.print(" | Fehler=");
    Serial.print(fehler);

    if (erkannt) {
      gpsBaudAktiv = baud;
      gpsBaudErkannt = true;
      Serial.println(" -> ERKANNT");
      break;
    }

    Serial.println(" -> keine gueltigen NMEA-Saetze");
  }

  if (!gpsBaudErkannt) {
    // Kein blindes Umprogrammieren des GPS. Wir starten nur mit der bisher
    // verwendeten Baudrate weiter und melden den Zustand deutlich.
    gpsBaudAktiv = GPS_BAUD_BEVORZUGT;
    Serial.println(
      "WARNUNG: Keine GPS-Baudrate anhand gueltiger NMEA-Saetze erkannt."
    );
    Serial.println(
      "GPS wird vorerst mit 115200 Baud gestartet; Antenne/UART/GPS pruefen."
    );
  }

  gpsUartAufAktiveBaudSetzen();

  Serial.print("GPS aktive Baudrate: ");
  Serial.print(gpsBaudAktiv);
  Serial.print(" | automatisch erkannt: ");
  Serial.println(gpsBaudErkannt ? "JA" : "NEIN");
}

void gpsBefehlSenden(const char* befehl) {
  gpsSerial.print(befehl);
  gpsSerial.flush();
}

// V5.9.7: Sichere 10-Hz-Konfiguration.
// WICHTIG: Die erkannte Baudrate wird NICHT veraendert.
// Damit kann der in V5.9.5 bestaetigte 9600-Baud-Betrieb nicht wieder
// durch eine fehlgeschlagene Baud-Umschaltung verloren gehen.
bool gps10HzSicherKonfigurieren() {
  gps10HzAngefordert = false;
  gps10HzBestaetigt = false;
  gpsRmcRateHz = NAN;
  gpsGgaRateHz = NAN;
  gpsRmcSaetzeGesamt = 0;
  gpsGgaSaetzeGesamt = 0;
  gpsRmcMessungLetzterZaehler = 0;
  gpsGgaMessungLetzterZaehler = 0;
  gpsRmcMessungLetzteMs = millis();

  if (!gpsBaudErkannt) {
    Serial.println("GPS 10 Hz: NICHT gesetzt - keine sichere Baudrate erkannt.");
    return false;
  }

  Serial.print("GPS 10 Hz: sichere Konfiguration bei ");
  Serial.print(gpsBaudAktiv);
  Serial.println(" Baud ...");

  // Zuerst die NMEA-Ausgabe konfigurieren.
  // CASIC hat PCAS03 ueber die Jahre erweitert. Die Varianten werden
  // bewusst von alt nach neu gesendet, ohne irgendeinen Baud-Befehl.
  gpsBefehlSenden(GPS_CMD_NMEA_LEGACY_8);
  delay(120);
  gpsBefehlSenden(GPS_CMD_NMEA_EXTENDED_14);
  delay(120);
  gpsBefehlSenden(GPS_CMD_NMEA_FULL_19);
  delay(300);

  // Danach die eigentliche Positionierungsrate auf 100 ms = 10 Hz setzen.
  gpsBefehlSenden(GPS_CMD_RATE_10HZ);
  delay(500);

  // UART NICHT beenden / neu starten und Baudrate NICHT veraendern.
  // Der laufende Parser bekommt die folgenden NMEA-Daten direkt weiter.
  gps10HzAngefordert = true;

  Serial.println("GPS 10 Hz: Befehl gesendet.");
  Serial.println("GPS NMEA: RMC 10 Hz + GGA ca. 1,1 Hz (PCAS03 8/14/19 kompatibel).");
  Serial.println("GPS Baud bleibt unveraendert.");
  return true;
}

// Ermittelt den Satztyp der gerade eingelesenen NMEA-Zeile.
// Die Zaehler werden erst erhoeht, wenn TinyGPS++ die Checksumme
// des jeweiligen Satzes als gueltig akzeptiert hat.
// Die Talker-ID (z. B. GP/GN/BD) ist dabei egal.
void gpsNmeaSatzTypErfassen(
  char zeichen,
  bool& aktuellerSatzRmc,
  bool& aktuellerSatzGga
) {
  static char header[6] = {0};
  static uint8_t pos = 0;

  if (zeichen == '$') {
    pos = 0;
    aktuellerSatzRmc = false;
    aktuellerSatzGga = false;
    return;
  }

  if (pos < 5) {
    header[pos++] = zeichen;
    if (pos == 5) {
      header[5] = '\0';

      aktuellerSatzRmc =
        header[2] == 'R' &&
        header[3] == 'M' &&
        header[4] == 'C';

      aktuellerSatzGga =
        header[2] == 'G' &&
        header[3] == 'G' &&
        header[4] == 'A';
    }
  }
}

void gpsRmcRateAktualisieren() {
  unsigned long jetzt = millis();

  if (gpsRmcMessungLetzteMs == 0) {
    gpsRmcMessungLetzteMs = jetzt;
    gpsRmcMessungLetzterZaehler = gpsRmcSaetzeGesamt;
    gpsGgaMessungLetzterZaehler = gpsGgaSaetzeGesamt;
    return;
  }

  unsigned long dt = (unsigned long)(jetzt - gpsRmcMessungLetzteMs);
  if (dt < 2000UL) return;

  uint32_t rmcAktuell = gpsRmcSaetzeGesamt;
  uint32_t ggaAktuell = gpsGgaSaetzeGesamt;
  uint32_t rmcDelta = rmcAktuell - gpsRmcMessungLetzterZaehler;
  uint32_t ggaDelta = ggaAktuell - gpsGgaMessungLetzterZaehler;

  gpsRmcRateHz = (dt > 0)
    ? ((float)rmcDelta * 1000.0f / (float)dt)
    : NAN;

  gpsGgaRateHz = (dt > 0)
    ? ((float)ggaDelta * 1000.0f / (float)dt)
    : NAN;

  gps10HzBestaetigt =
    !isnan(gpsRmcRateHz) &&
    gpsRmcRateHz >= 8.5f &&
    gpsRmcRateHz <= 11.5f;

  gpsRmcMessungLetzterZaehler = rmcAktuell;
  gpsGgaMessungLetzterZaehler = ggaAktuell;
  gpsRmcMessungLetzteMs = jetzt;
}

// UTC-Konvertierung ohne lokale Zeitzone. Grundlage ist die GPS-Zeit aus RMC.
int64_t gpsTageSeitUnixEpoch(int jahr, unsigned monat, unsigned tag) {
  jahr -= monat <= 2;
  const int era = (jahr >= 0 ? jahr : jahr - 399) / 400;
  const unsigned yoe = (unsigned)(jahr - era * 400);
  const unsigned doy = (153 * (monat + (monat > 2 ? -3 : 9)) + 2) / 5 + tag - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

uint64_t gpsUtcEpochMsBerechnen() {
  const int jahr = gps.date.year();
  const unsigned monat = gps.date.month();
  const unsigned tag = gps.date.day();
  if (jahr < 2020 || monat < 1 || monat > 12 || tag < 1 || tag > 31) return 0;

  const int64_t tage = gpsTageSeitUnixEpoch(jahr, monat, tag);
  if (tage < 0) return 0;

  uint64_t sekunden = (uint64_t)tage * 86400ULL;
  sekunden += (uint64_t)gps.time.hour() * 3600ULL;
  sekunden += (uint64_t)gps.time.minute() * 60ULL;
  sekunden += (uint64_t)gps.time.second();
  return sekunden * 1000ULL + (uint64_t)gps.time.centisecond() * 10ULL;
}

void gpsParserSnapshotAktualisieren() {
  GpsSnapshot neu = gpsSnapshotLesen();

  // Die letzte gueltige Position wird im Snapshot behalten.
  // "aktueller Fix" wird separat ueber letzterFixMillis bewertet.
  if (gps.location.isValid()) {
    unsigned long alter = gps.location.age();

    // TinyGPS++ meldet bei nie aktualisierten Daten ULONG_MAX.
    if (alter != ULONG_MAX) {
      neu.positionVorhanden = true;
      neu.lat = gps.location.lat();
      neu.lng = gps.location.lng();
      neu.letzterFixMillis = millis() - alter;
    }
  }

  neu.speedValid = gps.speed.isValid();
  if (neu.speedValid) {
    neu.speedKmh = gps.speed.kmph();
  } else {
    neu.speedKmh = 0.0;
  }

  neu.hdopValid = gps.hdop.isValid();
  if (neu.hdopValid) {
    neu.hdop = gps.hdop.hdop();
  } else {
    neu.hdop = 0.0;
  }

  neu.satellitesValid = gps.satellites.isValid();
  if (neu.satellitesValid) {
    neu.satellites = gps.satellites.value();
  } else {
    neu.satellites = 0;
  }

  // RMC liefert Datum + UTC-Zeit unabhaengig von LTE/Firebase.
  if (gps.date.isValid() && gps.time.isValid()) {
    const unsigned long dateAge = gps.date.age();
    const unsigned long timeAge = gps.time.age();
    if (dateAge != ULONG_MAX && timeAge != ULONG_MAX &&
        dateAge <= 2000UL && timeAge <= 2000UL) {
      const uint64_t epoch = gpsUtcEpochMsBerechnen();
      if (epoch > 1700000000000ULL) {
        neu.utcValid = true;
        neu.utcEpochMs = epoch;
        const unsigned long age = dateAge > timeAge ? dateAge : timeAge;
        neu.utcUpdateMillis = millis() - age;
      }
    }
  }

  neu.charsProcessed = gps.charsProcessed();
  neu.passedChecksum = gps.passedChecksum();
  neu.failedChecksum = gps.failedChecksum();
  neu.sentencesWithFix = gps.sentencesWithFix();

  portENTER_CRITICAL(&gpsSnapshotMux);
  gpsSnapshot = neu;
  portEXIT_CRITICAL(&gpsSnapshotMux);
}

void gpsZeichenEinlesen() {
  bool datenEmpfangen = false;
  static bool aktuellerSatzRmc = false;
  static bool aktuellerSatzGga = false;

  while (gpsSerial.available()) {
    char zeichen = gpsSerial.read();

    gpsNmeaSatzTypErfassen(
      zeichen,
      aktuellerSatzRmc,
      aktuellerSatzGga
    );

    uint32_t checksumVorher = gps.passedChecksum();
    gps.encode(zeichen);
    uint32_t checksumNachher = gps.passedChecksum();

    if (checksumNachher > checksumVorher) {
      if (aktuellerSatzRmc) {
        gpsRmcSaetzeGesamt++;
      }
      if (aktuellerSatzGga) {
        gpsGgaSaetzeGesamt++;
      }

      aktuellerSatzRmc = false;
      aktuellerSatzGga = false;
    }

    gpsBytesGesamt++;
    datenEmpfangen = true;
  }

  if (datenEmpfangen) {
    gpsParserSnapshotAktualisieren();
  }
}

void gpsEmpfangTask(void* parameter) {
  (void)parameter;

  for (;;) {
    // Das Mutex verhindert, dass ein Admin-GPS-Neustart gleichzeitig
    // auf UART/TinyGPSPlus zugreift.
    if (gpsUartMutex != nullptr &&
        xSemaphoreTake(gpsUartMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      gpsZeichenEinlesen();
      xSemaphoreGive(gpsUartMutex);
    }

    // 1 ms reicht, um CPU-Zeit zu sparen und trotzdem den UART-Puffer
    // praktisch kontinuierlich zu leeren.
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// Bestehende Aufrufe bleiben als Fallback erhalten.
// Wenn der FreeRTOS-Task laeuft, darf loop() TinyGPSPlus NICHT parallel anfassen.
void gpsEinlesen() {
  if (gpsTaskHandle == nullptr) {
    gpsZeichenEinlesen();
  }
}

void gpsTaskStarten() {
  if (gpsTaskHandle != nullptr) return;

  if (gpsUartMutex == nullptr) {
    gpsUartMutex = xSemaphoreCreateMutex();
  }

  if (gpsUartMutex == nullptr) {
    Serial.println("WARNUNG: GPS Mutex konnte nicht erstellt werden - Fallback ueber loop().");
    return;
  }

  BaseType_t ergebnis = xTaskCreate(
    gpsEmpfangTask,
    "MF35X_GPS_RX",
    GPS_TASK_STACK_SIZE,
    nullptr,
    GPS_TASK_PRIORITY,
    &gpsTaskHandle
  );

  if (ergebnis == pdPASS) {
    Serial.println("GPS Empfangs-Task: OK");
  } else {
    gpsTaskHandle = nullptr;
    Serial.println("WARNUNG: GPS Empfangs-Task konnte nicht gestartet werden - Fallback ueber loop().");
  }
}

void gpsSoftwareNeustart() {
  Serial.println("GPS Software-Neustart wird ausgefuehrt ...");

  bool mutexGesperrt = false;

  // Der GPS-Task darf waehrend UART-End/Begin und Parser-Reset nicht lesen.
  if (gpsUartMutex != nullptr) {
    mutexGesperrt =
      xSemaphoreTake(gpsUartMutex, portMAX_DELAY) == pdTRUE;
  }

  // CASIC/ATGM336H: Hot-Start des GNSS-Empfaengers.
  gpsSerial.print("$PCAS10,0*1C\r\n");
  gpsSerial.flush();
  delay(150);

  gpsSerial.end();
  delay(500);

  gps = TinyGPSPlus();
  gpsBytesGesamt = 0;
  gpsSnapshotZuruecksetzen();

  // Der Puffer wird vor begin() gesetzt.
  gpsSerial.setRxBufferSize(GPS_UART_RX_BUFFER_SIZE);
  gpsSerial.begin(gpsBaudAktiv, SERIAL_8N1, GPS_RX, GPS_TX);
  delay(300);

  // Nach einem Admin-GPS-Neustart 10 Hz erneut setzen, ohne die
  // funktionierende Baudrate zu veraendern.
  gps10HzSicherKonfigurieren();

  if (mutexGesperrt) {
    xSemaphoreGive(gpsUartMutex);
  }

  Serial.print("GPS UART neu gestartet: RX16 / TX17 / ");
  Serial.print(gpsBaudAktiv);
  Serial.print(" Baud | RX-Puffer ");
  Serial.print(GPS_UART_RX_BUFFER_SIZE);
  Serial.println(" Byte");
}

// ==================================================
// WLAN
// ==================================================

void wlanVerbindungStarten() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  if (strlen(WLAN_NAME) > 0) {
    // Nur fuer den lokalen Notfall mit secrets.h. Die Zugangsdaten werden
    // dabei wieder im WiFi-NVS gespeichert.
    WiFi.begin(WLAN_NAME, WLAN_PASSWORT);
  } else {
    // Normalfall: bereits gespeicherte Zugangsdaten aus dem bisherigen
    // funktionierenden V5.9.7-Stand weiterverwenden.
    WiFi.begin();
  }
}

void wlanVerbinden() {
  wlanVerbindungStarten();

  Serial.print("Verbinde mit WLAN");

  unsigned long startzeit = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startzeit < 20000UL
  ) {
    gpsEinlesen();
    Serial.print('.');
    delay(250);
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WLAN verbunden!");
    Serial.print("IP-Adresse: ");
    Serial.println(WiFi.localIP());
    Serial.print("Signalstaerke: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("WLAN-Verbindung fehlgeschlagen.");
  }
}

void wlanPruefen() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.println("WLAN getrennt - neuer Verbindungsversuch ...");
  WiFi.disconnect(false, false);
  wlanVerbindungStarten();
}

void wlanSoftwareNeustart() {
  Serial.println("WLAN Software-Neustart wird ausgefuehrt ...");

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  delay(1000);

  wlanVerbinden();
}

// ==================================================
// SENSOREN
// ==================================================

void oeltemperaturLesen() {
  if (!adsOk) {
    oilVoltage = NAN;
    oilSensorOhm = NAN;
    oilTemp = NAN;
    return;
  }

  int16_t rohwert = ads.readADC_SingleEnded(ADS_KANAL_OELTEMP);
  float spannung = ads.computeVolts(rohwert);

  oilVoltage = spannung;

  if (spannung <= 0.05f || spannung >= 3.25f) {
    oilSensorOhm = NAN;
    oilTemp = NAN;
    return;
  }

  oilSensorOhm =
    OEL_R_FIXED * spannung / (OEL_VCC - spannung);

  oilTemp =
    OEL_CAL_T1 +
    (
      (OEL_CAL_R1 - oilSensorOhm) *
      (OEL_CAL_T2 - OEL_CAL_T1) /
      (OEL_CAL_R1 - OEL_CAL_R2)
    );

  oilTemp += OEL_TEMP_OFFSET;
}

void oeldruckLesen() {
  if (!adsOk) {
    oilPressureVoltage = NAN;
    oilPressureOhm = NAN;
    oilPressureBar = NAN;
    return;
  }

  float summe = 0.0f;

  for (int i = 0; i < 20; i++) {
    int16_t rohwert = ads.readADC_SingleEnded(ADS_KANAL_OELDRUCK);
    summe += ads.computeVolts(rohwert);
    delay(5);
    gpsEinlesen();
  }

  oilPressureVoltage = summe / 20.0f;

  if (oilPressureVoltage < 0.02f) {
    oilPressureOhm = NAN;
    oilPressureBar = NAN;
    return;
  }

  if (oilPressureVoltage > 3.10f ||
      oilPressureVoltage >= DRUCK_VCC - 0.02f) {
    oilPressureOhm = NAN;
    oilPressureBar = NAN;
    return;
  }

  oilPressureOhm =
    DRUCK_R_FIXED *
    oilPressureVoltage /
    (DRUCK_VCC - oilPressureVoltage);

  oilPressureBar = widerstandZuBar(oilPressureOhm);

  if (!isnan(oilPressureBar) && oilPressureBar < 0.55f) {
    oilPressureBar = 0.0f;
  }
}

void batteriespannungLesen() {
  if (!adsOk) {
    batteryAdcVoltage = NAN;
    batteryVoltage = NAN;
    return;
  }

  float summe = 0.0f;

  for (int i = 0; i < 20; i++) {
    int16_t rohwert = ads.readADC_SingleEnded(ADS_KANAL_BATTERIE);
    summe += ads.computeVolts(rohwert);
    delay(5);
    gpsEinlesen();
  }

  batteryAdcVoltage = summe / 20.0f;

  if (batteryAdcVoltage < 0.02f ||
      batteryAdcVoltage > 3.25f) {
    batteryVoltage = NAN;
    return;
  }

  batteryVoltage =
    batteryAdcVoltage *
    BAT_TEILERFAKTOR *
    BAT_KORREKTUR;
}

void zylinderTemperaturLesen() {
  max31855InternalTemp = thermocouple.readInternal();
  cylinderTemp = thermocouple.readCelsius();
  max31855Fault = thermocouple.readError();

  if (max31855Fault != 0 || isnan(cylinderTemp)) {
    max31855Ok = false;
    cylinderTemp = NAN;
  } else {
    max31855Ok = true;
  }
}

void temperaturenUndBatterieLesen() {
  oeltemperaturLesen();
  batteriespannungLesen();
  zylinderTemperaturLesen();
}

void alleSensorenEinmalLesen() {
  oeltemperaturLesen();
  oeldruckLesen();
  batteriespannungLesen();
  zylinderTemperaturLesen();
}

// ==================================================
// LIVE-JSON BAUEN
// ==================================================

String liveJsonBauen(bool gpsDue, bool rpmDue, bool oilDue, bool tempDue) {
  String json = "{";
  json.reserve(850);
  bool first = true;

  jsonBoolFeld(json, first, "online", true);
  jsonText(json, first, "firmware", "V5.9.7");
  jsonText(json, first, "mode", "LIVE");
  jsonRaw(json, first, "wifi_rssi", String(WiFi.RSSI()));
  jsonULongFeld(json, first, "uptime_seconds", millis() / 1000UL);
  jsonBoolFeld(json, first, "config_sync_ok", firebaseConfigOk);
  jsonText(json, first, "config_source", configQuelle);

  // Ausgangszustand ist sicherheitsrelevant und wird bei jedem Patch mitgesendet.
  jsonBoolFeld(json, first, "switch_output", schaltausgangAktiv);

  if (gpsDue) {
    GpsSnapshot gpsDaten = gpsSnapshotLesen();
    bool gpsGueltig = gpsFixAktuell(gpsDaten);

    jsonBoolFeld(json, first, "gps_valid", gpsGueltig);

    if (gpsGueltig) {
      jsonFloatFeld(json, first, "lat", gpsDaten.lat, 6);
      jsonFloatFeld(json, first, "lng", gpsDaten.lng, 6);

      if (gpsDaten.speedValid) {
        jsonFloatFeld(json, first, "speed_kmh", gpsDaten.speedKmh, 1);
      } else {
        jsonFloatFeld(json, first, "speed_kmh", 0.0, 1);
      }

      if (gpsDaten.hdopValid) {
        jsonFloatFeld(json, first, "hdop", gpsDaten.hdop, 1);
      } else {
        jsonRaw(json, first, "hdop", "null");
      }
    } else {
      // Wie V5.9.2: lat/lng bei verlorenem Fix NICHT patchen.
      // Dadurch bleibt die letzte gueltige Position in Firebase erhalten.
      jsonFloatFeld(json, first, "speed_kmh", 0.0, 1);
      jsonRaw(json, first, "hdop", "null");
    }

    if (gpsDaten.satellitesValid) {
      jsonULongFeld(json, first, "satellites", gpsDaten.satellites);
    } else {
      jsonULongFeld(json, first, "satellites", 0UL);
    }

    jsonULongFeld(json, first, "gps_bytes", gpsBytesGesamt);

    // Diagnosewerte fuer den seriellen Test/Firebase, ohne Einfluss auf die Website.
    jsonULongFeld(json, first, "gps_nmea_ok", gpsDaten.passedChecksum);
    jsonULongFeld(json, first, "gps_nmea_failed", gpsDaten.failedChecksum);
    jsonULongFeld(json, first, "gps_sentences_with_fix", gpsDaten.sentencesWithFix);
  }

  if (rpmDue) {
    if (rpmSignalOk) {
      jsonFloatFeld(json, first, "rpm", rpm, 0);
    } else {
      jsonRaw(json, first, "rpm", "null");
    }

    jsonBoolFeld(json, first, "rpm_signal_ok", rpmSignalOk);
    jsonFloatFeld(json, first, "switch_speed_enable_kmh", outputConfig.speedEnableKmh, 1);
    jsonFloatFeld(json, first, "switch_rpm_on", outputConfig.rpmOn, 0);
    jsonFloatFeld(json, first, "switch_rpm_off", outputConfig.rpmOff, 0);
  }

  if (oilDue) {
    jsonFloatFeld(json, first, "oil_pressure", oilPressureBar, 2);
    jsonFloatFeld(json, first, "oil_pressure_voltage", oilPressureVoltage, 4);
    jsonFloatFeld(json, first, "oil_pressure_ohm", oilPressureOhm, 1);
  }

  if (tempDue) {
    jsonFloatFeld(json, first, "oil_temp", oilTemp, 1);
    jsonFloatFeld(json, first, "oil_temp_voltage", oilVoltage, 4);
    jsonFloatFeld(json, first, "oil_temp_ohm", oilSensorOhm, 1);

    jsonFloatFeld(json, first, "battery_v", batteryVoltage, 2);
    jsonFloatFeld(json, first, "battery_adc_voltage", batteryAdcVoltage, 4);

    jsonFloatFeld(json, first, "cylinder_temp", cylinderTemp, 1);
    jsonFloatFeld(json, first, "cylinder_module_temp", max31855InternalTemp, 1);
    jsonBoolFeld(json, first, "cylinder_sensor_ok", max31855Ok);
    jsonRaw(json, first, "cylinder_fault", String(max31855Fault));
  }

  jsonRaw(json, first, "timestamp", "{\".sv\":\"timestamp\"}");
  json += '}';
  return json;
}

String historyJsonBauen() {
  String json = "{";
  json.reserve(800);
  bool first = true;

  GpsSnapshot gpsDaten = gpsSnapshotLesen();
  bool gpsGueltig = gpsFixAktuell(gpsDaten);

  jsonBoolFeld(json, first, "gps_valid", gpsGueltig);

  if (gpsGueltig) {
    jsonFloatFeld(json, first, "lat", gpsDaten.lat, 6);
    jsonFloatFeld(json, first, "lng", gpsDaten.lng, 6);

    if (gpsDaten.speedValid) {
      jsonFloatFeld(json, first, "speed_kmh", gpsDaten.speedKmh, 1);
    } else {
      jsonFloatFeld(json, first, "speed_kmh", 0.0, 1);
    }

    if (gpsDaten.hdopValid) {
      jsonFloatFeld(json, first, "hdop", gpsDaten.hdop, 1);
    } else {
      jsonRaw(json, first, "hdop", "null");
    }
  } else {
    // In der Rennhistorie wird ein fehlender Fix bewusst als null gespeichert,
    // damit keine alte Position als neue Rennposition interpretiert wird.
    jsonRaw(json, first, "lat", "null");
    jsonRaw(json, first, "lng", "null");
    jsonFloatFeld(json, first, "speed_kmh", 0.0, 1);
    jsonRaw(json, first, "hdop", "null");
  }

  if (gpsDaten.satellitesValid) {
    jsonULongFeld(json, first, "satellites", gpsDaten.satellites);
  } else {
    jsonULongFeld(json, first, "satellites", 0UL);
  }

  if (rpmSignalOk) {
    jsonFloatFeld(json, first, "rpm", rpm, 0);
  } else {
    jsonRaw(json, first, "rpm", "null");
  }

  jsonFloatFeld(json, first, "oil_pressure", oilPressureBar, 2);
  jsonFloatFeld(json, first, "oil_temp", oilTemp, 1);
  jsonFloatFeld(json, first, "battery_v", batteryVoltage, 2);
  jsonFloatFeld(json, first, "cylinder_temp", cylinderTemp, 1);
  jsonBoolFeld(json, first, "switch_output", schaltausgangAktiv);
  jsonRaw(json, first, "wifi_rssi", String(WiFi.RSSI()));
  jsonULongFeld(json, first, "uptime_seconds", millis() / 1000UL);
  jsonRaw(json, first, "timestamp", "{\".sv\":\"timestamp\"}");

  json += '}';
  return json;
}

// V5.9.12: Durable Offline-Warteschlange fuer Rennmesspunkte.
#include "offline_race_buffer.hpp"

// V5.9.13: Maximalwerte und Alarmhistorie werden zentral vom ESP32 erzeugt.
#include "device_metrics_alarm.hpp"

// ==================================================
// DYNAMISCHE LIVE-UPLOADS
// ==================================================

void liveUpdatesBearbeiten() {
  unsigned long jetzt = millis();

  bool rpmDue = zeitFaellig(jetzt, letzterRpmUpload, intervalConfig.rpmFirebaseUpdateMs);
  bool oilDue = zeitFaellig(jetzt, letzterOilPressureUpload, intervalConfig.oilPressureUpdateMs);
  bool tempDue = zeitFaellig(jetzt, letzterTemperatureUpload, intervalConfig.temperatureUpdateMs);
  bool gpsDue = zeitFaellig(jetzt, letzterGpsUpload, intervalConfig.gpsUpdateMs);

  if (!rpmDue && !oilDue && !tempDue && !gpsDue) return;

  // Sensoren werden auch ohne WLAN weiter lokal aktualisiert.
  if (oilDue) {
    oeldruckLesen();
    letzterOilPressureUpload = jetzt;
  }

  if (tempDue) {
    temperaturenUndBatterieLesen();
    letzterTemperatureUpload = jetzt;
  }

  if (rpmDue) letzterRpmUpload = jetzt;
  if (gpsDue) letzterGpsUpload = jetzt;

  // V5.9.13: Maximalwerte und Alarmzustand werden lokal und netzunabhaengig ausgewertet.
  deviceDerivedDataAktualisieren();

  if (WiFi.status() != WL_CONNECTED) return;

  String json = liveJsonBauen(gpsDue, rpmDue, oilDue, tempDue);
  firebasePatch("tracker/live", json, true);
}

// ==================================================
// RENNHISTORIE
// ==================================================

void rennhistorieBearbeiten() {
  if (!recordingConfig.enabled || recordingConfig.raceId.length() == 0) {
    return;
  }

  unsigned long jetzt = millis();
  unsigned long intervall = recordingConfig.historyUpdateMs;

  if (intervall < 1000UL || intervall > 60000UL) {
    intervall = intervalConfig.historyUpdateMs;
  }

  if (!zeitFaellig(jetzt, letzterHistoryUpload, intervall)) return;
  letzterHistoryUpload = jetzt;

  // V5.9.12: Immer einen Messpunkt erzeugen - auch ohne WLAN.
  // Versand bzw. dauerhafte Pufferung entscheidet die Offline-Queue.
  offlineRennMesspunktBearbeiten();
}

// ==================================================
// SIGNIERTES OTA-FIRMWAREUPDATE
// ==================================================

// Arduino-ESP32 kann neue OTA-Images nach dem Boot automatisch als gueltig
// markieren. Fuer den MF35X wird das bewusst verschoben: Erst wenn setup()
// und loop() 30 Sekunden stabil gelaufen sind, bestaetigen wir das Image.
// Bei einem Bootloop davor kann der ESP32 auf die vorige OTA-Partition
// zurueckfallen (bei OTA-faehiger Partitionstabelle).
// Der Core-Hook verifyRollbackLater() liegt absichtlich in rollback_hook.cpp.
// Grund: Arduino erzeugt fuer .ino-Dateien automatisch Funktionsprototypen;
// C-Linkage-Deklarationen innerhalb einer .ino koennen falsches Linkage fuer spaetere
// Funktionen wie setup()/loop() verursachen.

bool otaPartitionVorhanden() {
  return esp_ota_get_next_update_partition(nullptr) != nullptr;
}

bool otaRollbackAutomatischAktiv() {
#ifdef CONFIG_APP_ROLLBACK_ENABLE
  return true;
#else
  return false;
#endif
}

bool otaIstPendingVerify() {
#ifdef CONFIG_APP_ROLLBACK_ENABLE
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) return false;

  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) return false;
  return state == ESP_OTA_IMG_PENDING_VERIFY;
#else
  return false;
#endif
}

void otaFirmwareValidierenWennBereit() {
  if (otaValidierungErledigt) return;

#ifndef CONFIG_APP_ROLLBACK_ENABLE
  otaValidierungErledigt = true;
  return;
#else
  if ((unsigned long)millis() < OTA_VALIDIERUNG_NACH_MS) return;

  const esp_partition_t* running = esp_ota_get_running_partition();
  if (!running) {
    otaValidierungErledigt = true;
    return;
  }

  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    otaValidierungErledigt = true;
    return;
  }

  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      Serial.println("OTA: neue Firmware nach 30 s stabilem Lauf bestaetigt.");
    } else {
      Serial.println("WARNUNG: OTA-Firmware konnte nicht als gueltig markiert werden.");
    }
  }

  otaValidierungErledigt = true;
#endif
}

bool otaManifestLaden(OtaManifest& manifest, String& fehler) {
  if (WiFi.status() != WL_CONNECTED) {
    fehler = "Keine WLAN-Verbindung.";
    return false;
  }

  WiFiClientSecure client;
  // Die Firmware selbst wird kryptografisch mit RSA/SHA-256 signiert.
  // Dadurch kann ein TLS-/Server-Manipulationsversuch kein fremdes Image
  // installieren. HTTPS bleibt zusaetzlich fuer Transportverschluesselung.
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(OTA_HTTP_TIMEOUT_MS);

  const String manifestUrl =
    String(OTA_MANIFEST_URL) + "?check=" + String((unsigned long)millis());

  if (!http.begin(client, manifestUrl)) {
    fehler = "Manifest-Verbindung konnte nicht gestartet werden.";
    return false;
  }

  http.addHeader("Cache-Control", "no-cache");
  const int code = http.GET();
  if (code != 200) {
    fehler = "Manifest HTTP " + String(code);
    http.end();
    return false;
  }

  const String json = http.getString();
  http.end();

  double versionCode = 0.0;
  double sizeBytes = 0.0;

  if (!jsonString(json, "version", manifest.version) ||
      !jsonZahl(json, "versionCode", versionCode) ||
      !jsonZahl(json, "size", sizeBytes) ||
      !jsonString(json, "md5", manifest.md5)) {
    fehler = "Manifest unvollstaendig.";
    return false;
  }

  manifest.versionCode = (long)versionCode;
  manifest.sizeBytes = (unsigned long)sizeBytes;
  manifest.md5.toLowerCase();

  if (manifest.versionCode <= 0 ||
      manifest.sizeBytes < 1024UL ||
      manifest.md5.length() != 32) {
    fehler = "Manifest ungueltig.";
    return false;
  }

  return true;
}

bool otaUpdateAusfuehren(const String& requestId) {
  if (!otaPartitionVorhanden()) {
    commandStatusSenden(
      "ota_update",
      requestId,
      "error",
      "Keine zweite OTA-App-Partition gefunden. Einmal per USB mit OTA-Partitionstabelle flashen."
    );
    return false;
  }

  commandStatusSenden("ota_update", requestId, "checking");

  OtaManifest manifest;
  String fehler;
  if (!otaManifestLaden(manifest, fehler)) {
    commandStatusSenden("ota_update", requestId, "error", fehler);
    return false;
  }

  if (manifest.versionCode <= MF35X_FIRMWARE_VERSION_CODE) {
    commandStatusSenden(
      "ota_update",
      requestId,
      "no_update",
      "Bereits aktuell: " + String(MF35X_FIRMWARE_VERSION)
    );
    return false;
  }

  commandStatusSenden(
    "ota_update",
    requestId,
    "downloading",
    "Lade " + manifest.version + " (signiert)"
  );

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(OTA_HTTP_TIMEOUT_MS);

  // Die Binary-URL ist fest im Sketch hinterlegt und kann weder von der
  // Website noch von Firebase umgebogen werden.
  const String firmwareUrl =
    String(OTA_FIRMWARE_URL) + "?v=" + String(manifest.versionCode);

  if (!http.begin(client, firmwareUrl)) {
    commandStatusSenden(
      "ota_update", requestId, "error", "Firmware-Verbindung fehlgeschlagen."
    );
    return false;
  }

  http.addHeader("Cache-Control", "no-cache");
  const int code = http.GET();
  if (code != 200) {
    commandStatusSenden(
      "ota_update",
      requestId,
      "error",
      "Firmware HTTP " + String(code)
    );
    http.end();
    return false;
  }

  const int contentLength = http.getSize();
  if (contentLength <= 0 || (unsigned long)contentLength != manifest.sizeBytes) {
    commandStatusSenden(
      "ota_update",
      requestId,
      "error",
      "Firmwaregroesse passt nicht zum Manifest."
    );
    http.end();
    return false;
  }

  Update.clearError();

  if (!Update.installSignature(&otaSignaturPruefer)) {
    commandStatusSenden(
      "ota_update", requestId, "error", "OTA-Signaturpruefer konnte nicht aktiviert werden."
    );
    http.end();
    return false;
  }

  if (!Update.begin(manifest.sizeBytes, U_FLASH)) {
    commandStatusSenden(
      "ota_update",
      requestId,
      "error",
      "Update.begin Fehler " + String(Update.getError())
    );
    http.end();
    return false;
  }

  if (!Update.setMD5(manifest.md5.c_str())) {
    Update.abort();
    commandStatusSenden(
      "ota_update", requestId, "error", "MD5-Wert im Manifest ungueltig."
    );
    http.end();
    return false;
  }

  NetworkClient* stream = http.getStreamPtr();
  if (!stream) {
    Update.abort();
    commandStatusSenden(
      "ota_update", requestId, "error", "Firmware-Datenstrom fehlt."
    );
    http.end();
    return false;
  }

  const size_t written = Update.writeStream(*stream);
  if (written != manifest.sizeBytes) {
    Update.abort();
    commandStatusSenden(
      "ota_update",
      requestId,
      "error",
      "Download unvollstaendig: " + String((unsigned long)written) + "/" +
        String(manifest.sizeBytes) + " Byte"
    );
    http.end();
    return false;
  }

  commandStatusSenden("ota_update", requestId, "verifying", "Pruefe MD5 und RSA-Signatur.");

  if (!Update.end()) {
    commandStatusSenden(
      "ota_update",
      requestId,
      "error",
      "Firmware verworfen. Update-Fehler " + String(Update.getError())
    );
    http.end();
    return false;
  }

  http.end();

  if (!Update.isFinished()) {
    commandStatusSenden(
      "ota_update", requestId, "error", "OTA wurde nicht vollstaendig abgeschlossen."
    );
    return false;
  }

  commandStatusSenden(
    "ota_update",
    requestId,
    "restarting",
    "Signatur OK. Neustart auf " + manifest.version
  );

  Serial.print("OTA erfolgreich. Neustart auf ");
  Serial.println(manifest.version);
  delay(1000);
  ESP.restart();
  return true;
}

// ==================================================
// ADMIN-SYSTEMBEFEHLE AUS FIREBASE
// ==================================================

bool commandAusObjektLesen(
  const String& commandsJson,
  const char* commandName,
  String& requestId,
  String& status
) {
  String objekt;
  if (!jsonObjekt(commandsJson, commandName, objekt)) return false;

  status = "";
  jsonString(objekt, "status", status);

  return jsonString(objekt, "requestId", requestId) && requestId.length() > 0;
}

void commandStatusSenden(
  const char* commandName,
  const String& requestId,
  const char* status,
  const String& message
) {
  if (WiFi.status() != WL_CONNECTED) return;

  String json = "{";
  bool first = true;
  jsonText(json, first, "ackId", requestId);
  jsonText(json, first, "status", status);
  if (message.length() > 0) {
    jsonText(json, first, "message", message);
  }
  jsonRaw(json, first, "ackAt", "{\".sv\":\"timestamp\"}");
  json += '}';

  String pfad = "tracker/config/system_commands/";
  pfad += commandName;
  firebasePatch(pfad, json, false);
}

void systemBefehlePruefen(const String& configJson) {
  if (WiFi.status() != WL_CONNECTED) return;

  // Die Systembefehle liegen absichtlich unter tracker/config.
  // Dieser Pfad wird ohnehin alle 5 Sekunden gelesen. Dadurch entsteht
  // fuer die Fernsteuerung KEIN zusaetzlicher dauernder Firebase-GET.
  String commandsJson;
  if (!jsonObjekt(configJson, "system_commands", commandsJson)) return;

  String requestId;
  String status;

  // Firmwareupdate hat hoechste Prioritaet. Es wird ausschliesslich die
  // fest hinterlegte GitHub-Quelle verwendet und die RSA-Signatur geprueft.
  if (commandAusObjektLesen(commandsJson, "ota_update", requestId, status) &&
      requestId != letzterOtaCommandId) {
    letzterOtaCommandId = requestId;
    commandIdSpeichern("cmd_ota", requestId);

    Serial.println("Admin-Befehl: signiertes Firmwareupdate pruefen");
    otaUpdateAusfuehren(requestId);
    return;
  }

  // Nach einem erfolgreichen OTA-Neustart steht derselbe Befehl noch auf
  // "restarting". Die gespeicherte ID verhindert eine zweite Installation.
  if (commandAusObjektLesen(commandsJson, "ota_update", requestId, status) &&
      requestId == letzterOtaCommandId && status == "restarting") {
    commandStatusSenden(
      "ota_update",
      requestId,
      "completed",
      "Firmware aktiv: " + String(MF35X_FIRMWARE_VERSION)
    );
  }

  // ESP32-Reboot hat Prioritaet vor den uebrigen Wartungsbefehlen.
  if (commandAusObjektLesen(commandsJson, "esp32_reboot", requestId, status) &&
      requestId != letzterRebootCommandId) {
    letzterRebootCommandId = requestId;
    commandIdSpeichern("cmd_rb", requestId);

    Serial.println("Admin-Befehl: ESP32 neu starten");
    commandStatusSenden("esp32_reboot", requestId, "restarting");
    delay(700);
    ESP.restart();
    return;
  }

  // Nach dem ESP32-Neustart steht derselbe Befehl noch auf "restarting".
  // Weil die ID bereits in NVS gespeichert ist, wird nicht erneut gestartet,
  // sondern der erfolgreiche Neustart nur noch bestaetigt.
  if (requestId == letzterRebootCommandId && status == "restarting") {
    commandStatusSenden("esp32_reboot", requestId, "completed");
  }

  // V5.9.13: Admin-Reset der Maximalwerte wird vom ESP32 selbst ausgefuehrt.
  deviceDerivedDataCommandBearbeiten(commandsJson);

  if (commandAusObjektLesen(commandsJson, "gps_restart", requestId, status) &&
      requestId != letzterGpsCommandId) {
    letzterGpsCommandId = requestId;
    commandIdSpeichern("cmd_gps", requestId);

    Serial.println("Admin-Befehl: GPS neu starten");
    commandStatusSenden("gps_restart", requestId, "restarting");
    gpsSoftwareNeustart();
    commandStatusSenden("gps_restart", requestId, "completed");
  }

  if (commandAusObjektLesen(commandsJson, "wifi_restart", requestId, status) &&
      requestId != letzterWifiCommandId) {
    letzterWifiCommandId = requestId;
    commandIdSpeichern("cmd_wifi", requestId);

    Serial.println("Admin-Befehl: WLAN neu starten");
    commandStatusSenden("wifi_restart", requestId, "restarting");
    delay(300);
    wlanSoftwareNeustart();

    if (WiFi.status() == WL_CONNECTED) {
      commandStatusSenden("wifi_restart", requestId, "completed");
      deviceStatusSenden();
    }
  }
}

// ==================================================
// DEVICE-STATUS FUER WEBSITE
// ==================================================

void deviceStatusSenden() {
  if (WiFi.status() != WL_CONNECTED) return;

  String json = "{";
  bool first = true;

  jsonText(json, first, "firmware", MF35X_FIRMWARE_VERSION);
  jsonULongFeld(json, first, "firmwareVersionCode", MF35X_FIRMWARE_VERSION_CODE);
  jsonBoolFeld(json, first, "otaSupported", true);
  jsonBoolFeld(json, first, "otaSignedUpdates", true);
  jsonBoolFeld(json, first, "otaPartitionReady", otaPartitionVorhanden());
  jsonBoolFeld(json, first, "otaAutomaticRollback", otaRollbackAutomatischAktiv());
  jsonBoolFeld(json, first, "otaPendingValidation", otaIstPendingVerify());
  jsonText(json, first, "otaManifestUrl", OTA_MANIFEST_URL);
  jsonBoolFeld(json, first, "historySupported", true);
  jsonBoolFeld(json, first, "maxValuesDeviceOwned", true);
  jsonBoolFeld(json, first, "alarmHistoryDeviceOwned", true);
  jsonBoolFeld(json, first, "alarmHistoryOfflineBufferReady", deviceAlarmQueueReady);
  jsonULongFeld(json, first, "alarmHistoryOfflinePending", deviceAlarmPendingCount);
  jsonULongFeld(json, first, "alarmHistoryOfflineQueued", deviceAlarmQueuedCount);
  jsonULongFeld(json, first, "alarmHistoryOfflineReplayed", deviceAlarmReplayedCount);
  jsonULongFeld(json, first, "alarmHistoryOfflineDropped", deviceAlarmDroppedCount);
  jsonBoolFeld(json, first, "historyOfflineBufferSupported", true);
  jsonBoolFeld(json, first, "historyOfflineBufferReady", offlineBufferReady);
  jsonBoolFeld(json, first, "historyOfflineBufferFull", offlineBufferFull);
  jsonULongFeld(json, first, "historyOfflinePending", offlinePendingCount);
  jsonULongFeld(json, first, "historyOfflineQueued", offlineQueuedCount);
  jsonULongFeld(json, first, "historyOfflineReplayed", offlineReplayedCount);
  jsonULongFeld(json, first, "historyOfflineDropped", offlineDroppedCount);
  jsonULongFeld(json, first, "historyOfflineCorrupt", offlineCorruptCount);
  jsonULongFeld(json, first, "historyOfflineFsTotalBytes", (unsigned long)offlineFsTotalBytes);
  jsonULongFeld(json, first, "historyOfflineFsUsedBytes", (unsigned long)offlineFsUsedBytes);
  jsonBoolFeld(json, first, "historyOfflinePsramAvailable", offlinePsramAvailable);
  jsonULongFeld(json, first, "historyOfflinePsramBytes", (unsigned long)offlinePsramBytes);
  jsonULongFeld(json, first, "historyOfflinePsramCacheRecords", (unsigned long)offlinePsramCacheCount);
  if (offlineLastError.length() > 0) {
    jsonText(json, first, "historyOfflineLastError", offlineLastError);
  }
  jsonBoolFeld(json, first, "systemCommandsSupported", true);
  jsonBoolFeld(json, first, "gpsSoftwareRestartSupported", true);
  jsonBoolFeld(json, first, "gpsParallelRxSupported", true);
  jsonULongFeld(json, first, "gpsRxBufferBytes", GPS_UART_RX_BUFFER_SIZE);
  jsonULongFeld(json, first, "gpsBaud", gpsBaudAktiv);
  jsonBoolFeld(json, first, "gpsBaudDetected", gpsBaudErkannt);
  jsonULongFeld(json, first, "gpsTargetHz", GPS_ZIELRATE_HZ);
  jsonBoolFeld(json, first, "gps10HzRequested", gps10HzAngefordert);
  jsonBoolFeld(json, first, "gps10HzVerified", gps10HzBestaetigt);
  jsonULongFeld(json, first, "commandCheckIntervalMs", FIREBASE_CONFIG_CHECK_MS);
  jsonULongFeld(json, first, "uploadIntervalMs", kleinstesLiveIntervall());
  jsonBoolFeld(json, first, "configSyncOk", firebaseConfigOk);
  jsonBoolFeld(json, first, "settingsSyncOk", firebaseSettingsOk);
  jsonText(json, first, "configSource", configQuelle);
  jsonULongFeld(json, first, "uptimeSeconds", millis() / 1000UL);
  jsonRaw(json, first, "timestamp", "{\".sv\":\"timestamp\"}");

  json += '}';
  firebasePatch("tracker/device", json, false);
}

// ==================================================
// SETUP
// ==================================================

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("============================================");
  Serial.println("MF35X LIVETRACKER V5.9.13 OTA SIGNED");
  Serial.println("FIREBASE-KONFIG + DYNAMISCHE INTERVALLE");
  Serial.println("ADMIN: ESP32 / WLAN / GPS SOFTWARE-NEUSTART");
  Serial.println("OTA: RSA-SIGNIERT + ZWEITE APP-PARTITION + ROLLBACK-SCHUTZ");
  Serial.println("DREHZAHL GPIO10 + AUSGANG GPIO11");
  Serial.println("GPS ROBUST: AUTO-BAUD + SICHERE 10 HZ + 4-KB RX-PUFFER + TASK");
  Serial.println("============================================");

  // Erst letzte gueltige Konfiguration laden.
  lokaleKonfigurationLaden();

  // V5.9.12: Datenpartition + PSRAM pruefen und vorhandenen Rueckstau laden.
  // Die Funktion kennt noch kein WLAN und veraendert die OTA-Partitionen nicht.
  offlinePufferInitialisieren();

  // V5.9.13: lokale Maximalwerte + Alarmqueue laden.
  deviceDerivedDataInitialisieren();

  // Ausgang sofort sicher LOW.
  pinMode(SCHALTAUSGANG_PIN, OUTPUT);
  digitalWrite(SCHALTAUSGANG_PIN, LOW);

  // HY-M154 / PC817 mit internem Pull-up.
  pinMode(RPM_PIN, INPUT_PULLUP);
  attachInterrupt(
    digitalPinToInterrupt(RPM_PIN),
    rpmImpulsISR,
    FALLING
  );

  Serial.println("Drehzahl W-Signal: GPIO10");
  Serial.println("Schaltausgang: GPIO11 (HIGH = EIN)");

  // V5.9.7: zuerst die tatsaechliche GPS-Baudrate anhand gueltiger
  // NMEA-Checksummen erkennen. Diese Baudrate bleibt danach UNVERAENDERT.
  gpsBaudAutomatischErkennen();

  // Nur offizielle CASIC-Befehle fuer Satzrate + Fixrate senden.
  // Kein Wechsel auf 115200: dadurch bleibt der bestaetigte GPS-Link stabil.
  gps10HzSicherKonfigurieren();

  Serial.print("GPS gestartet: RX16 / TX17 / ");
  Serial.print(gpsBaudAktiv);
  Serial.print(" Baud | RX-Puffer ");
  Serial.print(GPS_UART_RX_BUFFER_SIZE);
  Serial.println(" Byte");

  // Ab jetzt wird GPS unabhaengig von HTTPS/Firebase permanent eingelesen.
  gpsTaskStarten();

  // V5.9.11: RPM/GPIO11 ebenfalls vor jedem WLAN-/Firebase-Zugriff starten.
  // Damit bleibt die Schaltfunktion selbst bei komplettem Netzausfall lokal aktiv.
  steuerungTaskStarten();

  Wire.begin(I2C_SDA, I2C_SCL);

  adsOk = ads.begin(0x48);

  if (adsOk) {
    ads.setGain(GAIN_ONE);
    Serial.println("ADS1115 gefunden: Adresse 0x48");
    Serial.println("Oeltemperatur: AIN0");
    Serial.println("Oeldruck: AIN1");
    Serial.println("Batteriespannung: AIN2");
  } else {
    Serial.println("FEHLER: ADS1115 nicht gefunden!");
  }

  Serial.println("MAX31855 Zylinderkopf:");
  Serial.println("CLK GPIO12 | CS GPIO13 | DO GPIO14");

  // Einmal lokale Werte erfassen.
  alleSensorenEinmalLesen();
  deviceDerivedDataAktualisieren();

  wlanVerbinden();

  if (WiFi.status() == WL_CONNECTED) {
    // Beim Start sofort die aktuellen Website-Werte uebernehmen.
    firebaseKonfigurationLaden(true);
    // Vor dem Device-Status alte browserbasierte Maxwerte einmalig einlesen und zusammenfuehren.
    deviceDerivedDataBearbeiten();
    deviceStatusSenden();

    // Ersten kompletten Datensatz unmittelbar senden.
    String initialJson = liveJsonBauen(true, true, true, true);
    firebasePatch("tracker/live", initialJson, true);

    unsigned long jetzt = millis();
    letzterRpmUpload = jetzt;
    letzterOilPressureUpload = jetzt;
    letzterTemperatureUpload = jetzt;
    letzterGpsUpload = jetzt;
    letzterConfigCheck = jetzt;
    letzterDeviceStatus = jetzt;
  }

  Serial.println();
  Serial.print("Livetracker ");
  Serial.print(MF35X_FIRMWARE_VERSION);
  Serial.println(" gestartet.");
}

// ==================================================
// HAUPTSCHLEIFE
// ==================================================

void loop() {
  otaFirmwareValidierenWennBereit();
  gpsEinlesen();

  // Normalfall: RPM/GPIO11 laufen ausschliesslich im eigenen Steuerungs-Task.
  // Nur falls dessen Start fehlgeschlagen ist, bleibt die alte loop-Logik als
  // sicherer Fallback erhalten.
  if (controlTaskHandle == nullptr) {
    drehzahlAktualisieren();
    schaltausgangAktualisieren();
  }

  unsigned long jetzt = millis();

  if (zeitFaellig(jetzt, letzterWlanCheck, WIFI_CHECK_INTERVAL_MS)) {
    letzterWlanCheck = jetzt;
    wlanPruefen();
  }

  if (WiFi.status() == WL_CONNECTED &&
      zeitFaellig(jetzt, letzterConfigCheck, FIREBASE_CONFIG_CHECK_MS)) {
    letzterConfigCheck = jetzt;
    firebaseKonfigurationLaden(false);
  }

  // Alle Website-Intervalle werden hier wirksam.
  liveUpdatesBearbeiten();

  // V5.9.13: NVS-Sicherung, Maxwert-Sync und Alarm-Nachsenden.
  deviceDerivedDataBearbeiten();

  // Rennaufzeichnung nutzt das auf der Admin-Seite eingestellte Intervall.
  rennhistorieBearbeiten();

  // Maximal einen gepufferten Datensatz pro Drain-Zyklus nachsenden.
  // GPIO10/GPIO11 laufen weiterhin unabhaengig im V5.9.11-Steuerungs-Task.
  offlineDrainBearbeiten();

  if (WiFi.status() == WL_CONNECTED &&
      zeitFaellig(jetzt, letzterDeviceStatus, DEVICE_STATUS_INTERVAL_MS)) {
    letzterDeviceStatus = jetzt;
    deviceStatusSenden();
  }

  if (zeitFaellig(jetzt, letzteStatusausgabe, STATUS_INTERVAL_MS)) {
    letzteStatusausgabe = jetzt;
    statusAusgeben();
  }
}

// ==================================================
// STATUSAUSGABE
// ==================================================

void statusAusgeben() {
  Serial.println();
  Serial.print("------------- STATUS ");
  Serial.print(MF35X_FIRMWARE_VERSION);
  Serial.println(" -------------");

  Serial.print("WLAN: ");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("OK | ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
  } else {
    Serial.println("getrennt");
  }

  Serial.print("Konfiguration: ");
  Serial.print(configQuelle);
  Serial.print(" | config=");
  Serial.print(firebaseConfigOk ? "OK" : "nicht aktuell");
  Serial.print(" | limits=");
  Serial.println(firebaseSettingsOk ? "OK" : "nicht aktuell");

  GpsSnapshot gpsDaten = gpsSnapshotLesen();
  gpsRmcRateAktualisieren();

  Serial.print("GPS: ");
  if (gpsFixAktuell(gpsDaten)) {
    Serial.print("FIX | ");
    if (gpsDaten.satellitesValid) {
      Serial.print(gpsDaten.satellites);
    } else {
      Serial.print('0');
    }
    Serial.println(" Satelliten");
  } else {
    Serial.print("kein aktueller Fix");
    if (gpsDaten.satellitesValid) {
      Serial.print(" | Satelliten gemeldet: ");
      Serial.print(gpsDaten.satellites);
    }
    Serial.println();
  }

  Serial.print("GPS Bytes gesamt: ");
  Serial.println(gpsBytesGesamt);

  Serial.print("GPS NMEA Checksummen OK/Fehler: ");
  Serial.print(gpsDaten.passedChecksum);
  Serial.print('/');
  Serial.println(gpsDaten.failedChecksum);

  Serial.print("GPS NMEA Saetze mit Fix: ");
  Serial.println(gpsDaten.sentencesWithFix);

  Serial.print("GPS Baud: ");
  Serial.print(gpsBaudAktiv);
  Serial.print(" | automatisch erkannt: ");
  Serial.println(gpsBaudErkannt ? "JA" : "NEIN");

  Serial.print("GPS Zielrate: 10 Hz | Befehl: ");
  Serial.println(gps10HzAngefordert ? "GESENDET" : "NICHT GESENDET");

  Serial.print("GPS RMC-Rate gemessen: ");
  if (isnan(gpsRmcRateHz)) {
    Serial.println("---");
  } else {
    Serial.print(gpsRmcRateHz, 1);
    Serial.print(" Hz | 10 Hz: ");
    Serial.println(gps10HzBestaetigt ? "BESTAETIGT" : "NOCH NICHT");
  }

  Serial.print("GPS GGA-Rate gemessen: ");
  if (isnan(gpsGgaRateHz)) {
    Serial.println("---");
  } else {
    Serial.print(gpsGgaRateHz, 1);
    Serial.println(" Hz | Ziel ca. 1,1 Hz");
  }

  Serial.print("GPS RMC/GGA Saetze gesamt: ");
  Serial.print(gpsRmcSaetzeGesamt);
  Serial.print(" / ");
  Serial.println(gpsGgaSaetzeGesamt);

  Serial.print("GPS Empfang: ");
  Serial.print(GPS_UART_RX_BUFFER_SIZE);
  Serial.print(" Byte UART-Puffer | Task=");
  Serial.println(gpsTaskHandle != nullptr ? "OK" : "FALLBACK");

  Serial.print("ADS1115: ");
  Serial.println(adsOk ? "OK" : "FEHLER");

  Serial.print("Oeltemperatur: ");
  if (isnan(oilTemp)) Serial.println("---");
  else {
    Serial.print(oilTemp, 1);
    Serial.println(" C");
  }

  Serial.print("Oeldruck: ");
  if (isnan(oilPressureBar)) Serial.println("---");
  else {
    Serial.print(oilPressureBar, 2);
    Serial.println(" bar");
  }

  Serial.print("Batteriespannung: ");
  if (isnan(batteryVoltage)) Serial.println("---");
  else {
    Serial.print(batteryVoltage, 2);
    Serial.println(" V");
  }

  Serial.print("Zylinderkopf: ");
  if (isnan(cylinderTemp)) Serial.println("---");
  else {
    Serial.print(cylinderTemp, 1);
    Serial.println(" C");
  }

  Serial.print("Drehzahl W-Signal: ");
  if (!rpmSignalOk) {
    Serial.println("kein gueltiges Signal");
  } else {
    Serial.print(rpm, 0);
    Serial.println(" U/min");
  }

  Serial.print("Drehzahl Rohwert: ");
  Serial.print(rpmRoh, 0);
  Serial.println(" U/min");
  Serial.print("Steuerungs-Task GPIO10/GPIO11: ");
  Serial.print(controlTaskHandle != nullptr ? "OK / NETZUNABHAENGIG" : "FALLBACK / LOOP");
  Serial.print(" | W-Timeout: ");
  Serial.print(RPM_SIGNAL_TIMEOUT_US / 1000UL);
  Serial.println(" ms");

  Serial.println("--- Externer High/Low-Ausgang ---");
  Serial.print("Speed-Freigabe >= ");
  Serial.print(outputConfig.speedEnableKmh, 1);
  Serial.println(" km/h");
  Serial.print("HIGH ab RPM: ");
  Serial.println(outputConfig.rpmOn, 0);
  Serial.print("LOW unter RPM: ");
  Serial.println(outputConfig.rpmOff, 0);
  Serial.print("GPIO11: ");
  Serial.println(schaltausgangAktiv ? "EIN / HIGH" : "AUS / LOW");

  Serial.println("--- Website-Updateintervalle ---");
  Serial.print("RPM: ");
  Serial.print(intervalConfig.rpmFirebaseUpdateMs);
  Serial.println(" ms");
  Serial.print("Oeldruck: ");
  Serial.print(intervalConfig.oilPressureUpdateMs);
  Serial.println(" ms");
  Serial.print("Temperaturen/Batterie: ");
  Serial.print(intervalConfig.temperatureUpdateMs);
  Serial.println(" ms");
  Serial.print("GPS: ");
  Serial.print(intervalConfig.gpsUpdateMs);
  Serial.println(" ms");
  Serial.print("Rennhistorie: ");
  Serial.print(intervalConfig.historyUpdateMs);
  Serial.println(" ms");

  Serial.println("--- Alarmgrenzen aus Website ---");
  Serial.print("Batterie Warn/Alarm: ");
  Serial.print(alarmConfig.batteryWarn, 1);
  Serial.print(" / ");
  Serial.print(alarmConfig.batteryAlarm, 1);
  Serial.println(" V");
  Serial.print("Oeldruck Warn/Alarm: ");
  Serial.print(alarmConfig.oilPressureWarn, 1);
  Serial.print(" / ");
  Serial.print(alarmConfig.oilPressureAlarm, 1);
  Serial.println(" bar");
  Serial.print("Oeltemp Warn/Alarm: ");
  Serial.print(alarmConfig.oilTempWarn, 0);
  Serial.print(" / ");
  Serial.print(alarmConfig.oilTempAlarm, 0);
  Serial.println(" C");
  Serial.print("Zylindertemp Warn/Alarm: ");
  Serial.print(alarmConfig.cylTempWarn, 0);
  Serial.print(" / ");
  Serial.print(alarmConfig.cylTempAlarm, 0);
  Serial.println(" C");

  Serial.print("Rennaufzeichnung: ");
  if (recordingConfig.enabled) {
    Serial.print("AKTIV | ");
    Serial.print(recordingConfig.raceId);
    Serial.print(" | ");
    Serial.print(recordingConfig.historyUpdateMs);
    Serial.println(" ms");
  } else {
    Serial.println("AUS");
  }

  Serial.println("--- ESP32-Maximalwerte / Alarmhistorie ---");
  Serial.print("Datenquelle: ESP32 | Max-NVS: " );
  Serial.print(deviceMaxNvsDirty ? "offen" : "gesichert");
  Serial.print(" | Alarm pending/queued/replayed/dropped: " );
  Serial.print(deviceAlarmPendingCount);
  Serial.print('/');
  Serial.print(deviceAlarmQueuedCount);
  Serial.print('/');
  Serial.print(deviceAlarmReplayedCount);
  Serial.print('/');
  Serial.println(deviceAlarmDroppedCount);
  if (deviceAlarmLastError.length() > 0) {
    Serial.print("Alarm-Puffer letzter Fehler: " );
    Serial.println(deviceAlarmLastError);
  }

  Serial.println("--- Offline-Rennpuffer ---");
  Serial.print("Puffer: ");
  Serial.print(offlineBufferReady ? "BEREIT" : "FEHLER");
  Serial.print(" | pending: ");
  Serial.print(offlinePendingCount);
  Serial.print(" | queued/replayed/dropped: ");
  Serial.print(offlineQueuedCount);
  Serial.print('/');
  Serial.print(offlineReplayedCount);
  Serial.print('/');
  Serial.println(offlineDroppedCount);
  Serial.print("Flash: ");
  Serial.print((unsigned long)offlineFsUsedBytes);
  Serial.print('/');
  Serial.print((unsigned long)offlineFsTotalBytes);
  Serial.print(" Byte | voll: ");
  Serial.println(offlineBufferFull ? "JA" : "NEIN");
  Serial.print("PSRAM: ");
  if (offlinePsramAvailable) {
    Serial.print((unsigned long)offlinePsramBytes);
    Serial.print(" Byte | Cache: ");
    Serial.println((unsigned long)offlinePsramCacheCount);
  } else {
    Serial.println("nicht verfuegbar");
  }
  if (offlineLastError.length() > 0) {
    Serial.print("Puffer letzter Fehler: ");
    Serial.println(offlineLastError);
  }

  Serial.println("--- Firmware / OTA ---");
  Serial.print("Firmware: ");
  Serial.println(MF35X_FIRMWARE_VERSION);
  Serial.print("OTA-Partition: ");
  Serial.println(otaPartitionVorhanden() ? "OK" : "FEHLT");
  Serial.print("OTA signiert: JA | Auto-Rollback: ");
  Serial.println(otaRollbackAutomatischAktiv() ? "JA" : "NEIN");
  Serial.print("OTA Pending-Validierung: ");
  Serial.println(otaIstPendingVerify() ? "JA" : "NEIN");

  Serial.print("Firebase HTTP zuletzt: ");
  Serial.println(letzterHttpCode);

  Serial.print("Live-Uploads OK/Fehler: ");
  Serial.print(uploadOk);
  Serial.print('/');
  Serial.println(uploadFehler);

  Serial.print("Config lesen OK/Fehler: ");
  Serial.print(configLeseOk);
  Serial.print('/');
  Serial.println(configLeseFehler);

  Serial.print("History OK/Fehler: ");
  Serial.print(historyOk);
  Serial.print('/');
  Serial.println(historyFehler);

  Serial.println("----------------------------------------");
}

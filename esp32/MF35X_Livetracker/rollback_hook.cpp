// MF35X V5.9.9 OTA Rollback-Hook
//
// Diese Funktion ueberschreibt den schwachen Arduino-ESP32-Core-Hook.
// Sie muss C-Linkage besitzen, liegt aber bewusst in einer separaten .cpp-Datei,
// damit der Arduino-.ino-Praeprozessor keine falschen Funktionsprototypen erzeugt.

extern "C" bool verifyRollbackLater(void)
{
  return true;
}

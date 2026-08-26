# Zephyr RTOS UDS Hardware Testsuite

Dieses Verzeichnis enthält die PyQt5-basierte grafische Testoberfläche (`uds_gui.py`) zur automatisierten und manuellen Validierung des ISO 14229-1 konformen Zephyr RTOS UDS-Servers. Die Suite kommuniziert über native Linux SocketCAN-Schnittstellen und nutzt das ISO-TP-Protokoll (ISO 15765-2) für die Multi-Frame-Übertragung.

## 📊 Abgedeckte Testmatrix & C-Module

Die GUI bietet eine vollständige Testabdeckung für die im C-Core implementierten Fahrzeugdiagnose-Dienste:

| UDS Dienst | Bezeichnung | Ziel-Modul im Core | Validierter Schutzmechanismus / Funktion |
| :--- | :--- | :--- | :--- |
| **0x10 0x03** | DiagnosticSessionControl | `uds_session.c` | Schaltet die ECU in die angehobene `EXTENDED_SESSION`. |
| **0x3E** | TesterPresent | `uds_session.c` | Setzt den S3-Verbindungstimer im Hintergrund zurück. |
| **-** | S3-Timer Fallback | `uds_session.c` | Prüft, ob der `k_timer` nach 5000 ms Inaktivität autark in die `DEFAULT_SESSION` zurückfällt. |
| **0x27** | Security Access | `uds_security.c` | Überprüft den NVS-Flash-basierten Anti-Brute-Force Lockout (Sperre nach 3 Fehlversuchen für 10s). |
| **0x19 0x02** | ReadDTCInformation | `uds_read_dtc.c` | Liest Fehlerspeichereinträge via ISO-TP Multi-Frame (Consecutive Frames) aus. |
| **0x14** | ClearDiagnosticInfo | `uds_clear_dtc.c` | Validiert den asynchronen `k_work`-Handshake (zyklische `NRC 0x78 Response Pending` Meldungen während der Flash-Löschung). |
| **0x2F** | InputOutputControl | `uds_iocontrol.c` | Steuert Aktuatoren (Status-LED) über die anwendungsspezifische DID `0x0123`. |
| **0x31** | RoutineControl | `uds_routine.c` | Startet remote Applikations-Routinen in der angehobenen Session. |
| **0x34** | RequestDownload | `uds_flash_pipeline.c`| Leitet die Flash-Pipeline für Firmware-Streaming-Updates ein. |

---

## 🔒 Krypto- & Adressierungsparameter

Die Suite ist fest auf die Adressarchitektur und den Sicherheitsalgorithmus des Zephyr-Moduls kalibriert:

*   **CAN-ID Client/Tester (TX):** `0x7E0` (Physikalische Anfrage)
*   **CAN-ID ECU/Server (RX):** `0x7E8` (Physikalische Antwort)
*   **Funktionale ID (Broadcast):** `0x7DF` (Wird zur Überprüfung der funktionalen NRC-Unterdrückung genutzt)
*   **Krypto-Algorithmus (Level 1 Standard):** `expected_key = seed_val ^ 0xABCDE123`
*   **Krypto-Algorithmus (Level 3 Extended):** `expected_key = seed_val ^ 0xDEADBEEF`

---

## 💡 Spezifische Testabläufe

### 1. 🛠️ Input/Output Control (Status-LED)
Der Test für den Dienst `0x2F` ist in drei anwendungsspezifische Interaktionen für die `DID 0x0123` unterteilt. Er durchläuft automatisiert die Session- und Krypto-Vorschaltkette, um `NRC 0x33` zu verhindern:
*   **LED Einschalten:** Sendet `2F 01 23 03 01` (*ShortTermAdjustment* mit Zustand Ein). Schaltet die physische LED auf dem Board ein.
*   **LED Ausschalten:** Sendet `2F 01 23 03 00` (*ShortTermAdjustment* mit Zustand Aus).
*   **Kontrolle zurückgeben:** Sendet `2F 01 23 00` (*ReturnControlToECU*). Übergibt das GPIO-Handling zurück an die autarke Steuerung des Steuergeräts.

### 2. ⏳ Asynchroner k_work-Handshake (Clear DTC)
Beim Klick auf das Löschen des Fehlerspeichers wird simuliert, dass die Hardware 1200 ms zum Leeren der Flash-Sektoren benötigt.
*   Die GUI fängt in einer adaptiven Empfangsschleife (bis zu 150 Durchläufe) die vom Zephyr-Modul periodisch gesendeten `7F 14 78` (*Response Pending*) Frames lückenlos ab, um Socket-Timeouts zu verhindern.
*   Nach Ablauf der Löschzeit bricht das C-Modul den One-Shot-Timer ab und die GUI quittiert den erfolgreichen Abschluss mit dem Erhalt des positiven `0x54`-Erfolgsbytes.

---

## 🚀 Inbetriebnahme & Ausführung

### 1. Virtuelles CAN-Interface einrichten (Falls keine echte Hardware verbunden ist)
```bash
sudo modprobe vcan
sudo ip link add dev can0 type vcan
sudo ip link set up can0
```

### 2. Abhängigkeiten installieren
Die Testsuite benötigt Python 3.10+ und die Bibliotheken für Qt5 sowie die SocketCAN-Protokollfamilie:
```bash
pip install PyQt5 python-can can-isotp
```

### 3. Testsuite starten
Die Ausführung erfolgt über das zentrale Hauptprogramm im Testsuite-Verzeichnis. Als Argument wird das SocketCAN-Interface übergeben:
```bash
python main.py can0
```

## 🧹 Komfortfunktionen
*   **Puffer-Auto-Flush:** Vor kritischen Diensten (wie dem Krypto-Handshake beim IO-Control) leert die Python-Suite das Socket vollautomatisch von alten "Geister-Bytes", um Race-Conditions im Linux-Kernel zu verhindern.
*   **🧹 Log-Fenster leeren:** Über die integrierte Schaltfläche direkt oberhalb des Terminals kann das Bus- und Eventprotokoll jederzeit mit einem Klick bereinigt werden, um bei aufeinanderfolgenden Testzyklen die Übersicht zu behalten.

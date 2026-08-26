# Zephyr UDS Application (zephyr-uds-app)

Dieses Repository enthält eine Referenzapplikation für das `zephyr-uds`-Modul, welche einen generischen UDS-Server (ISO 14229-1) auf Basis des nativen Zephyr ISO-TP-Stacks (ISO 15765-2) als Steuergerät (ECU) im CAN-Netzwerk implementiert.

## 📁 Repository-Struktur
```text
.
├── app/                        # Applikations-Logik, DeviceTree & Konfiguration
│   ├── src/main.c              # UDS-Thread-Management
│   ├── src/app_uds_impl.c      # Implementierung der UDS-Schnittstellen
│   ├── app.overlay             # Generisches Devicetree-Overlay
│   ├── nucleo_g474re.overlay   # STM32G4-spezifisches Overlay
│   ├── prj.conf                # Kconfig-Konfiguration
│   └── sysbuild/mcuboot.conf   # Bootloader-Konfiguration
├── Testsuite/                  # Python-basierte Testumgebung
├── west.yml                    # West-Manifest für das Core-Modul
└── README.md
```

## 🛠 Architektur & Datenfluss
Die Anwendung nutzt `__weak`-Funktionen im Core-Modul, um entkoppelt auf Events zu reagieren. Die `app/src/app_uds_impl.c` bildet die Brücke:
*   **DIDs (DID 0x1001, etc.):** Lesen/Schreiben von CAN-Daten, Persistenz über Zephyr NVS.
*   **Security Access:** Verwendung des Zephyr Entropy Generators für `seed-to-key`.
*   **I/O Control:** Ansteuerung von GPIO/PWM-Treibern.
*   **Firmware-Update (0x34/0x36/0x37):** Streaming-Architektur für Over-the-Air (OTA) Updates auf die `slot1_partition`.

## 📦 Bootloader & Sysbuild
Verwendung von Zephyr Sysbuild zur Integration von MCUboot (`SB_CONFIG_BOOTLOADER_MCUBOOT=y`). Erzeugt ein kombiniertes `zephyr.signed.confirmed.bin` Image.

## ⚡ Schnellstart
```bash
west init --local zephyr-uds-app/
west update
west build -b nucleo_g474re app/
west flash
```

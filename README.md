# HydroMate - ESP32 Automatisches Bewaesserungssystem

## Uebersicht

Ein ESP32-basiertes automatisches Bewaesserungssystem mit FreeRTOS und IoT-Anbindung.
Entwickelt als Transferarbeit im Modul Mikrocomputertechnik und Sensorik.

## Features

- **FreeRTOS** mit 6 parallelen Tasks auf Dual-Core ESP32
- **3 Sensoren**: DHT22 (Temperatur/Luftfeuchte), Kapazitiver Bodensensor, HC-SR04 (Wasserstand)
- **3 Benutzerschnittstellen**: OLED-Display (SH1106), Web-Dashboard, Blynk Cloud-App
- **Moore-Automat** fuer die Bewaesserungslogik (5 Zustaende)
- **Trockenlauf-Schutz** fuer die Pumpe
- **Multi-WiFi** mit automatischem Fallback
- **Thread-Safety** durch Mutex und Queue

## Hardware

| Komponente | Typ | Verbindung |
|---|---|---|
| Mikrocontroller | ESP32 DevKitC V4 (38 Pin) | - |
| Temperatur/Feuchte | DHT22 (AM2302) | GPIO 4 |
| Bodenfeuchtigkeit | Kapazitiver Sensor v1.2 | GPIO 34 (ADC) |
| Wasserstand | HC-SR04 Ultraschall | GPIO 5 (Trig), GPIO 18 (Echo) |
| Display | SH1106 OLED 1.3 Zoll | I2C (GPIO 21 SDA, GPIO 22 SCL) |
| Pumpe | Peristaltische Pumpe 5V | via D4184 MOSFET (GPIO 25) |
| Status-LED | RGB LED | GPIO 16 (Rot), GPIO 17 (Gruen) |

## Software-Architektur

```
Core 0:                  Core 1:
+----------------+       +-------------------+
| taskWeb        |       | taskSensorenLesen |
| (Webserver)    |       | taskDisplay       |
+----------------+       | taskLogik         |
                         | taskStatistik     |
                         +-------------------+
                         
Arduino loop():
+----------------+
| Blynk.run()    |
+----------------+
```

### FreeRTOS Tasks

| Task | Prioritaet | Core | Intervall |
|---|---|---|---|
| taskSensorenLesen | 2 | 1 | 2500 ms |
| taskDisplay | 1 | 1 | 1000 ms |
| taskLogik | 3 | 1 | 500 ms |
| taskWeb | 1 | 0 | Async |
| taskStatistik | 1 | 1 | 30000 ms |
| Blynk (loop) | - | 1 | 15000 ms |

### Synchronisation

- **Queue** (Mailbox-Prinzip): Uebertraegt Sensordaten zwischen Tasks
- **dataMutex**: Schuetzt globale Steuerungsvariablen
- **serialMutex**: Schuetzt die serielle Ausgabe

## Zustandsautomat (Moore-Automat)

```
            boden_trocken
    IDLE ──────────────────> GIESSEN
     ^                         |
     |    cooldown_ok          | 3 Sek.
     +──── COOLDOWN <──────────+
     
     tank_leer (von jedem Zustand)
     ────────> TANK_LEER
     
     manuell (von IDLE)
     ────────> MANUELL
```

5 Zustaende: IDLE, GIESSEN, COOLDOWN, TANK_LEER, MANUELL

## Ordnerstruktur

```
Abgabe_Finale_Version_TEKO/
  1_Dokumentation/         -> Transferarbeit (PDF + DOCX)
  2_Quellcode/             -> Arduino-Sketch (.ino)
  3_Schaltplan_und_Diagramme/ -> Diagramme und Schaltplaene
```

## Verwendete Bibliotheken

| Bibliothek | Zweck |
|---|---|
| WiFi.h | WiFi-Verbindung |
| BlynkSimpleEsp32.h | Blynk Cloud IoT |
| ESPAsyncWebServer.h | Async Web-Dashboard |
| DHT.h | DHT22 Sensor |
| Adafruit_SH110X.h | OLED Display |
| ArduinoJson.h | JSON fuer REST-API |
| SPIFFS.h | Dateisystem |

## Setup

1. Arduino IDE installieren
2. ESP32 Board in Arduino IDE hinzufuegen
3. Alle Bibliotheken installieren (siehe Tabelle oben)
4. Im Code anpassen:
   - `BLYNK_AUTH_TOKEN` -> Dein Blynk Token
   - `ssid_1` / `password_1` -> Dein WiFi
5. Hochladen auf ESP32

## Lizenz

Dieses Projekt wurde als Lernprojekt an der TEKO Bern erstellt.

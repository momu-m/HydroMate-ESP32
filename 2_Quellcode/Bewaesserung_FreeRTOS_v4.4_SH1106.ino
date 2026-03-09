/*
 * AUTOMATISCHES BEWAESSERUNGS-SYSTEM - VERSION 4.4
 * 
 * Projekt: Smartes Bewaesserungssystem (IoT + Web)
 * Autor: Anonym (TEKO Bern - Systemtechnik)
 * Modul: Mikrocomputertechnik und Sensorik
 * 
 * NEU in v4.4:
 * - OLED Display Upgrade: 1.3" SH1106 (statt 0.96" SSD1306)
 * - Bibliothek gewechselt auf Adafruit_SH110X
 * - Display ist groesser und besser lesbar!
 * 
 * FEATURES (seit v4.3):
 * - FreeRTOS mit 6 Tasks
 * - Task 1: Sensoren (DHT22 + Boden + HC-SR04)
 * - Task 2: Display (OLED 1.3" SH1106 + Status-LED)
 * - Task 3: Bewaesserung (Logik + Sicherheit)
 * - Task 4: Statistik
 * - Task 5: Webserver (AsyncWebServer)
 * - Loop:  Blynk IoT (Cloud Dashboard)
 * 
 * HARDWARE:
 * - ESP32 DevKitC V4 (38 Pin)
 * - DHT22 Temp+Feuchte (Pin 4)
 * - Kapazitiver Bodensensor (Pin 34)
 * - HC-SR04 Wasserstand (Pin 5 Trig, Pin 18 Echo)
 * - OLED 1.3" SH1106 128x64 I2C (Pin 21 SDA, Pin 22 SCL)  <-- NEU!
 * - MOSFET Modul (Pin 25)
 * - RGB LED Status (Pin 16 Rot, Pin 17 Gruen)
 */

// -------------------------------------------------------------------------
// 1. BLYNK KONFIGURATION (MUSS GANZ OBEN STEHEN!)
// -------------------------------------------------------------------------
#define BLYNK_PRINT Serial

// WICHTIG: Hier fehlt die Template ID! Bitte aus dem Blynk Dashboard kopieren.
// Sie beginnt meist mit "TMPL...". Ohne diese funktioniert es nicht.
#define BLYNK_TEMPLATE_ID "TMPL4iYQ5z9r0"
#define BLYNK_TEMPLATE_NAME "ESP32 Bewässerung"
#define BLYNK_AUTH_TOKEN "DEIN_BLYNK_TOKEN_HIER"
// -------------------------------------------------------------------------
// 2. BIBLIOTHEKEN
// -------------------------------------------------------------------------
#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
// =========================================================================
// NEU v4.4: SH1106 statt SSD1306!
// ALTE Zeile war:  #include <Adafruit_SSD1306.h>
// Neue Bibliothek: Adafruit SH110X (unterstuetzt SH1106 und SH1107)
// Installation: Arduino IDE -> Bibliotheksverwalter -> "Adafruit SH110X" suchen
// =========================================================================
#include <Adafruit_SH110X.h>

// -------------------------------------------------------------------------
// 3. WIFI & GLOBALE EINSTELLUNGEN
// -------------------------------------------------------------------------
const char* ssid_1 = "DEIN_WLAN_NAME_1";      // Z.B. "Swisscom_ABCD1234"
const char* password_1 = "DEIN_PASSWORT_1";

// WiFi Netzwerk 2: TEKO Schule
const char* ssid_2 = "DEIN_WLAN_NAME_2";      // Frage deinen Lehrer nach dem Namen
const char* password_2 = "DEIN_PASSWORT_2";   // Frage deinen Lehrer nach dem Passwort

// WiFi Netzwerk 3: Handy Hotspot (Backup für Präsentation)
const char* ssid_3 = "DEIN_WLAN_NAME_3";      // Z.B. "iPhone von Mo" oder "Samsung Galaxy"
const char* password_3 = "DEIN_PASSWORT_3";
// Pins - Sensoren
#define DHT_PIN 4
#define BODEN_PIN 34
#define RELAIS_PIN 25
#define OLED_SCK 22
#define OLED_SDA 21

// NEU v4.3: HC-SR04 Wasserstand
#define TRIG_PIN 5    // Trigger (Output) -> GPIO5
#define ECHO_PIN 18   // Echo (Input) -> GPIO18 (mit Spannungsteiler!)

// NEU v4.3: RGB Status-LED (Ampel)
#define LED_ROT_PIN 16   // Rote LED
#define LED_GRUEN_PIN 17 // Gruene LED
// Gelb = Rot + Gruen gleichzeitig AN

// -------------------------------------------------------------------------
// TANK KONFIGURATION - An deinen Wassertank anpassen!
// -------------------------------------------------------------------------
const float TANK_HOEHE_CM     = 8.5;   // Abstand Sensor bis Tankboden (cm)
const float TANK_MIN_DIST_CM  = 2.0;   // Mindestabstand Sensor (wenn voll)
const float TANK_MAX_DIST_CM  = 8.0;   // Maximalabstand (wenn leer)
const int   WASSER_MIN_PROZENT = 5;    // Unter 5% -> Pumpe GESPERRT!
const int   WASSER_WARN_PROZENT = 20;  // Unter 20% -> LED ROT
const int   WASSER_OK_PROZENT = 50;    // Ueber 50% -> LED GRUEN

// Objekte
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

// =========================================================================
// NEU v4.4: Display-Objekt fuer SH1106 1.3" OLED
// ALTE Zeilen waren:
//   #define SCREEN_WIDTH 128
//   #define SCREEN_HEIGHT 64
//   Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
//
// NEUE Zeile: Adafruit_SH1106G132 = SH1106, 1.3 Zoll, 128x64
// Die Aufloesung bleibt gleich (128x64), nur der Treiber-Chip ist anders!
// =========================================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SH1106G display = Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

AsyncWebServer server(80);
// JsonDocument lokal in taskWeb definiert für Thread-Safety

// -------------------------------------------------------------------------
// HTML CODE (DIRECT EMBEDDED)
// -------------------------------------------------------------------------
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="de">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>HydroMate Dashboard</title>
    <link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css">
    <style>
        :root { --primary-color: #00b894; --secondary-color: #0984e3; --bg-color: #2d3436; --card-bg: #353b48; --text-color: #dfe6e9; --danger-color: #d63031; }
        body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: var(--bg-color); color: var(--text-color); margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }
        h1 { color: var(--primary-color); margin-bottom: 30px; text-align: center; }
        .dashboard-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; width: 100%; max-width: 1200px; }
        .card { background-color: var(--card-bg); border-radius: 15px; padding: 25px; box-shadow: 0 10px 20px rgba(0,0,0,0.3); text-align: center; transition: transform 0.3s ease; }
        .card:hover { transform: translateY(-5px); }
        .card i { font-size: 2.5rem; margin-bottom: 15px; }
        .value { font-size: 2.5rem; font-weight: bold; margin: 10px 0; }
        .unit { font-size: 1.2rem; color: #b2bec3; }
        .label { font-size: 1.2rem; text-transform: uppercase; letter-spacing: 1px; color: #b2bec3; }
        .temp i { color: #ff7675; } .humid i { color: #74b9ff; } .soil i { color: #55efc4; }
        .controls { display: flex; flex-direction: column; gap: 15px; }
        .btn-toggle { background-color: #636e72; border: none; padding: 15px; border-radius: 10px; color: white; font-size: 1.2rem; cursor: pointer; transition: background 0.3s; display: flex; justify-content: space-between; align-items: center; }
        .btn-toggle.active { background-color: var(--primary-color); }
        .btn-toggle.pump-active { background-color: var(--secondary-color); }
        .status-dot { height: 15px; width: 15px; background-color: #d63031; border-radius: 50%; display: inline-block; }
        .status-dot.g { background-color: #00b894; }
        .water i { color: #0984e3; }
        .water-bar { width: 100%; height: 20px; background: #636e72; border-radius: 10px; overflow: hidden; margin-top: 10px; }
        .water-fill { height: 100%; background: linear-gradient(90deg, #d63031, #fdcb6e, #00b894); border-radius: 10px; transition: width 0.5s; }
        .tank-warn { color: #d63031; font-weight: bold; margin-top: 8px; }
        @media (max-width: 600px) { .dashboard-grid { grid-template-columns: 1fr; } .value { font-size: 2rem; } }
    </style>
</head>
<body>
    <h1><i class="fas fa-tint"></i> HydroMate</h1>
    <div class="dashboard-grid">
        <div class="card temp"><i class="fas fa-thermometer-half"></i><div class="label">Temperatur</div><div class="value" id="temp">--</div><div class="unit">°C</div></div>
        <div class="card humid"><i class="fas fa-tint"></i><div class="label">Luftfeuchte</div><div class="value" id="hum">--</div><div class="unit">%</div></div>
        <div class="card soil"><i class="fas fa-seedling"></i><div class="label">Bodenfeuchte</div><div class="value" id="soil">--</div><div class="unit">%</div></div>
        <div class="card"><i class="fas fa-server"></i><div class="label">System</div>
            <div style="margin-top: 15px; text-align: left;"><p>Uptime: <span id="uptime">--:--</span></p><p>Status: <span id="status-text">Bereit</span></p><p>Bewässerungen: <span id="count">0</span></p></div>
        </div>
        <div class="card"><i class="fas fa-gamepad"></i><div class="label">Steuerung</div>
            <div class="controls" style="margin-top: 20px;">
                <button id="btn-auto" class="btn-toggle" onclick="toggleAuto()"><span>Automatik-Modus</span><span class="status-dot" id="dot-auto"></span></button>
                <button id="btn-pump" class="btn-toggle" onclick="togglePump()"><span>Wasserpumpe</span><span class="status-dot" id="dot-pump"></span></button>
            </div>
        </div>
        <div class="card water"><i class="fas fa-water"></i><div class="label">Wasserstand</div><div class="value" id="water">--</div><div class="unit">%</div><div class="water-bar"><div class="water-fill" id="water-bar" style="width:0%"></div></div><div class="tank-warn" id="tank-warn" style="display:none">PUMPE GESPERRT!</div></div>
    </div>
    <script>
        function updateData() {
            fetch('/data').then(r => r.json()).then(d => {
                document.getElementById('temp').innerText = d.temperature.toFixed(1);
                document.getElementById('hum').innerText = d.humidity.toFixed(1);
                document.getElementById('soil').innerText = d.soil_moisture;
                document.getElementById('count').innerText = d.watering_count;
                // Uptime formatieren (Stunden:Minuten:Sekunden)
                // Die Uhrzeit war so vorher und soll auch so bleiben!
                let s = d.uptime || 0; 
                let m = Math.floor(s/60); s=s%60;
                let h = Math.floor(m/60); m=m%60;
                document.getElementById('uptime').innerText = h + ":" + (m<10?"0":"")+m + ":" + (s<10?"0":"")+s;

                updateButton('btn-auto', 'dot-auto', d.auto_mode);
                updateButton('btn-pump', 'dot-pump', d.pump_state, true);
                // NEU v4.3: Wasserstand anzeigen
                document.getElementById('water').innerText = d.water_level || 0;
                document.getElementById('water-bar').style.width = (d.water_level || 0) + '%';
                var tw = document.getElementById('tank-warn');
                tw.style.display = d.tank_locked ? 'block' : 'none';
                let st = "Standby";
                if (d.tank_locked) st = "TANK LEER!";
                else if (d.pump_state) st = "Giessen...";
                if (d.auto_mode) st += " (Auto)";
                document.getElementById('status-text').innerText = st;
            }).catch(e => console.error(e));
        }
        function updateButton(bid, did, active, isPump=false) {
            const b = document.getElementById(bid), d = document.getElementById(did);
            if (active) { b.classList.add(isPump?'pump-active':'active'); d.classList.add('g'); }
            else { b.classList.remove('active','pump-active'); d.classList.remove('g'); }
        }
        function toggleAuto() { fetch('/toggle?target=auto').then(() => updateData()); }
        function togglePump() { fetch('/toggle?target=pump').then(() => updateData()); }
        setInterval(updateData, 3000);
        window.onload = updateData;
    </script>
</body>
</html>
)rawliteral";

// -------------------------------------------------------------------------
// 4. LOGIK VARIABLEN (Thread-Safety beachten!)
// -------------------------------------------------------------------------

// Sensor Daten Struktur
struct SensorDaten {
  float temperatur;
  float luftfeuchtigkeit;
  int bodenwert;
  // NEU v4.3: Wasserstand
  float wasser_distanz_cm;    // Gemessene Distanz zur Oberflaeche
  int   wasser_prozent;       // Fuellstand 0-100%
  // Fehler-Flags
  bool dht_fehler;
  bool boden_fehler;
  bool wasser_fehler;         // NEU: HC-SR04 Fehler
  bool wasser_leer;           // NEU: Tank unter Minimum
  unsigned long zeitstempel;
};

// Kalibrierung
const int BODEN_TROCKEN = 2600;      
const int BODEN_FEUCHT = 1300;       
const int BODEN_SCHWELLWERT = 2210;  
const int BODEN_MAX_PLAUSIBEL = 3000;
const int BODEN_MIN_PLAUSIBEL = 1000;

// Status Variablen (fuer Web, Blynk & Logik)
bool automatik_aktiv = true;
bool pumpe_aktiv = false;
bool manueller_modus = false; // Wenn true -> Automatik ignoriert
int giess_zaehler = 0;
unsigned long letztes_giessen = 0;
bool warte_nach_giessen = false;
bool tank_sperre = false;            // NEU v4.3: Trockenlauf-Sperre
bool led_blink_state = false;        // NEU v4.3: Fuer LED-Blinken
bool blynk_update_requested = false; // Flag für Thread-Safety Update

// FreeRTOS Handles
QueueHandle_t sensorQueue;
SemaphoreHandle_t dataMutex;   // Schützt globale Variablen
SemaphoreHandle_t serialMutex; // Schützt Serial Ausgabe

// Task Handles - Werden gebraucht damit der Statistik-Task
// den Speicherverbrauch jedes einzelnen Tasks messen kann.
// Ohne Handle kann man einen Task nicht von aussen ueberwachen.
TaskHandle_t taskSensorenHandle;  // Handle fuer Sensor-Task
TaskHandle_t taskDisplayHandle;   // Handle fuer Display-Task
TaskHandle_t taskLogikHandle;     // Handle fuer Logik-Task
TaskHandle_t taskWebHandle;       // Handle fuer Web-Task
TaskHandle_t taskBlynkHandle;     // Handle fuer Blynk-Task
TaskHandle_t taskStatistikHandle; // Handle fuer Statistik-Task (misst sich selbst)

// -------------------------------------------------------------------------
// STATISTIK TASK - Misst den Speicherverbrauch aller Tasks
// -------------------------------------------------------------------------
// uxTaskGetStackHighWaterMark() gibt zurueck, wie viele Bytes
// ein Task MINDESTENS noch frei hatte seit dem Start.
// Je kleiner der Wert, desto naeher war der Task am Ueberlauf.
// Wenn der Wert 0 erreicht -> Stack Overflow -> System stuerzt ab!

void taskStatistik(void *pv) {
  // Warte 30 Sekunden nach Start, damit alle Tasks stabil laufen
  vTaskDelay(30000 / portTICK_PERIOD_MS);
  
  while(1) {
    // Sicherstellen dass Serial nicht von anderen Tasks gleichzeitig benutzt wird
    if (xSemaphoreTake(serialMutex, 1000 / portTICK_PERIOD_MS)) {
      
      Serial.println();
      Serial.println("=== STACK VERWENDUNG (Bytes frei) ===");
      
      // Jeder Task wird einzeln gemessen
      // uxTaskGetStackHighWaterMark() braucht den Task-Handle
      Serial.print("Sensoren:   ");
      Serial.println(uxTaskGetStackHighWaterMark(taskSensorenHandle));
      
      Serial.print("Display:    ");
      Serial.println(uxTaskGetStackHighWaterMark(taskDisplayHandle));
      
      Serial.print("Logik:      ");
      Serial.println(uxTaskGetStackHighWaterMark(taskLogikHandle));
      
      Serial.print("Web:        ");
      Serial.println(uxTaskGetStackHighWaterMark(taskWebHandle));
      
      Serial.print("Blynk:      ");
      Serial.println(uxTaskGetStackHighWaterMark(taskBlynkHandle));
      
      // WICHTIG: Der Statistik-Task misst sich SELBST
      // Das geht mit NULL als Parameter (= aktueller Task)
      Serial.print("Statistik:  ");
      Serial.println(uxTaskGetStackHighWaterMark(NULL));
      
      // Freier Heap-Speicher (dynamischer RAM)
      Serial.print("Heap frei:  ");
      Serial.println(esp_get_free_heap_size());
      
      Serial.println("=====================================");
      
      xSemaphoreGive(serialMutex); // Serial wieder freigeben
    }
    
    // Alle 30 Sekunden messen
    vTaskDelay(30000 / portTICK_PERIOD_MS);
  }
}

// -------------------------------------------------------------------------
// 5. BLYNK FUNKTIONEN (Callbacks)
// -------------------------------------------------------------------------

// V6: Automatik Schalter
BLYNK_WRITE(V6) {
  int pinValue = param.asInt(); // 0 oder 1
  
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    automatik_aktiv = (pinValue == 1);
    
    // Wenn Automatik an, manuellen Modus aus
    if (automatik_aktiv) manueller_modus = false;
    
    // Serial Ausgabe verschoben nach Mutex Release
    bool newStatus = automatik_aktiv;
    xSemaphoreGive(dataMutex);
    
    Serial.print("[BLYNK] Automatik: ");
    Serial.println(newStatus ? "AN" : "AUS");
  }
}

// V7: Manuelle Pumpe
BLYNK_WRITE(V7) {
  int pinValue = param.asInt(); // 0 oder 1
  
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    if (pinValue == 1) {
      manueller_modus = true;
      pumpe_aktiv = true;
      digitalWrite(RELAIS_PIN, HIGH);
    } else {
      manueller_modus = false;
      pumpe_aktiv = false; // Wird evtl. von Automatik wieder aktiviert
      digitalWrite(RELAIS_PIN, LOW);
    }
    xSemaphoreGive(dataMutex);
    
    // Serial Ausgabe nach Mutex Release
    if (pinValue == 1) Serial.println("[BLYNK] Pumpe MANUELL AN");
    else Serial.println("[BLYNK] Pumpe MANUELL AUS");
  }
}

// -------------------------------------------------------------------------
// 6. TASKS IMPLEMENTIERUNG
// -------------------------------------------------------------------------

// HC-SR04 Messfunktion - misst Distanz in cm
// Nimmt 3 Messungen und gibt den Median zurueck (Filter gegen Ausreisser)
float messeWasserstand() {
  float messungen[3];
  
  for (int i = 0; i < 3; i++) {
    // Trigger-Puls senden
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    // Echo-Zeit messen (Timeout 30ms = max ~5m)
    long dauer = pulseIn(ECHO_PIN, HIGH, 30000);
    
    if (dauer == 0) {
      messungen[i] = -1.0; // Fehler/Timeout
    } else {
      messungen[i] = dauer * 0.0343 / 2.0; // Umrechnung in cm
    }
    
    delay(30); // Kurze Pause zwischen Messungen
  }
  
  // Median von 3 Werten (sortieren, mittleren nehmen)
  // Einfaches Sortieren fuer 3 Werte
  if (messungen[0] > messungen[1]) { float t = messungen[0]; messungen[0] = messungen[1]; messungen[1] = t; }
  if (messungen[1] > messungen[2]) { float t = messungen[1]; messungen[1] = messungen[2]; messungen[2] = t; }
  if (messungen[0] > messungen[1]) { float t = messungen[0]; messungen[0] = messungen[1]; messungen[1] = t; }
  
  return messungen[1]; // Median
}

// Status-LED aktualisieren (Ampel-Logik)
void updateStatusLED(int wasser_prozent, bool fehler) {
  if (fehler) {
    // Sensor-Fehler: Beide LEDs blinken abwechselnd
    led_blink_state = !led_blink_state;
    digitalWrite(LED_ROT_PIN, led_blink_state ? HIGH : LOW);
    digitalWrite(LED_GRUEN_PIN, led_blink_state ? LOW : HIGH);
    return;
  }
  
  if (wasser_prozent <= WASSER_MIN_PROZENT) {
    // ALARM: Unter 5% - ROT BLINKT
    led_blink_state = !led_blink_state;
    digitalWrite(LED_ROT_PIN, led_blink_state ? HIGH : LOW);
    digitalWrite(LED_GRUEN_PIN, LOW);
  }
  else if (wasser_prozent <= WASSER_WARN_PROZENT) {
    // WARNUNG: 5-20% - ROT fest
    digitalWrite(LED_ROT_PIN, HIGH);
    digitalWrite(LED_GRUEN_PIN, LOW);
  }
  else if (wasser_prozent <= WASSER_OK_PROZENT) {
    // ACHTUNG: 20-50% - GELB (Rot + Gruen)
    digitalWrite(LED_ROT_PIN, HIGH);
    digitalWrite(LED_GRUEN_PIN, HIGH);
  }
  else {
    // OK: Ueber 50% - GRUEN
    digitalWrite(LED_ROT_PIN, LOW);
    digitalWrite(LED_GRUEN_PIN, HIGH);
  }
}

// TASK 1: Sensoren (DHT22 + Boden + HC-SR04)
// NEU v4.4: DHT22-Fehler werden abgefangen!
// Der DHT22 liefert manchmal NaN (Not a Number) zurueck.
// Das ist normales Verhalten - wir merken uns einfach den letzten
// gueltigen Wert und zeigen FEHLER erst nach 5x Fehlversuchen.
void taskSensorenLesen(void *pv) {
  SensorDaten daten;
  
  // Letzte gueltige DHT22 Werte (Cache)
  float letzte_temp = 0.0;
  float letzte_feuchte = 0.0;
  int dht_fehler_zaehler = 0;     // Zaehlt aufeinanderfolgende Fehler
  const int DHT_MAX_FEHLER = 5;   // Nach 5 Fehlern (10 Sek) -> FEHLER anzeigen
  
  while(1) {
    // DHT22 lesen
    float temp_neu = dht.readTemperature();
    float feuchte_neu = dht.readHumidity();
    
    if (isnan(temp_neu) || isnan(feuchte_neu)) {
      // DHT22 hat sich verschluckt - passiert manchmal!
      dht_fehler_zaehler++;
      
      if (dht_fehler_zaehler >= DHT_MAX_FEHLER) {
        // Erst nach 5 Fehlern in Folge als echten Fehler melden
        daten.dht_fehler = true;
        daten.temperatur = 0.0;
        daten.luftfeuchtigkeit = 0.0;
      } else {
        // Letzte gueltige Werte weiterverwenden (kein Flackern!)
        daten.dht_fehler = false;
        daten.temperatur = letzte_temp;
        daten.luftfeuchtigkeit = letzte_feuchte;
      }
    } else {
      // Gueltige Messung - Werte speichern!
      dht_fehler_zaehler = 0;  // Fehler-Zaehler zuruecksetzen
      daten.dht_fehler = false;
      daten.temperatur = temp_neu;
      daten.luftfeuchtigkeit = feuchte_neu;
      // Cache aktualisieren
      letzte_temp = temp_neu;
      letzte_feuchte = feuchte_neu;
    }
    
    // Bodensensor lesen
    daten.bodenwert = analogRead(BODEN_PIN);
    daten.boden_fehler = (daten.bodenwert < BODEN_MIN_PLAUSIBEL || daten.bodenwert > BODEN_MAX_PLAUSIBEL);
    
    // NEU v4.3: HC-SR04 Wasserstand messen
    daten.wasser_distanz_cm = messeWasserstand();
    
    if (daten.wasser_distanz_cm < 0 || daten.wasser_distanz_cm > 400) {
      // Sensor-Fehler (Timeout oder unplausibel)
      daten.wasser_fehler = true;
      daten.wasser_prozent = 0;
      daten.wasser_leer = true;
    } else {
      daten.wasser_fehler = false;
      // Distanz in Prozent umrechnen:
      // Kleine Distanz = voll, grosse Distanz = leer
      float fuellhoehe = TANK_HOEHE_CM - daten.wasser_distanz_cm;
      float nutzbare_hoehe = TANK_MAX_DIST_CM - TANK_MIN_DIST_CM;
      daten.wasser_prozent = (int)((fuellhoehe / nutzbare_hoehe) * 100.0);
      
      // Begrenzen auf 0-100%
      if (daten.wasser_prozent > 100) daten.wasser_prozent = 100;
      if (daten.wasser_prozent < 0) daten.wasser_prozent = 0;
      
      daten.wasser_leer = (daten.wasser_prozent <= WASSER_MIN_PROZENT);
    }
    
    
    daten.zeitstempel = millis();

    // Mailbox-Prinzip: immer aktuellsten Wert speichern
    xQueueOverwrite(sensorQueue, &daten);
    
    // 2.5 Sekunden Pause (etwas laenger als vorher fuer stabilere DHT22 Messung)
    vTaskDelay(2500 / portTICK_PERIOD_MS);
  }
}

// TASK 2: Display (OLED 1.3" SH1106 + Status-LED)
// NEU v4.4: Alle 8 Zeilen des Displays genutzt!
// Zeile 1: Projekt-Name
// Zeile 2: Trennlinie
// Zeile 3: Temperatur
// Zeile 4: Luftfeuchtigkeit (NEU!)
// Zeile 5: Bodenfeuchtigkeit
// Zeile 6: Wasserstand
// Zeile 7: Pumpen-Status
// Zeile 8: Uptime + Modus (NEU!)
void taskDisplay(void *pv) {
  SensorDaten daten;
  while(1) {
    if(xQueuePeek(sensorQueue, &daten, 100)) {
       display.clearDisplay();
       display.setCursor(0,0);
       display.setTextColor(SH110X_WHITE);
       display.setTextSize(1);
       
       // Zeile 1+2: Header
       display.println("== HydroMate v4.4 ==");
       display.println("--------------------");
       
       // Zeile 3: Temperatur
       if (daten.dht_fehler) {
         display.println("Temp: FEHLER!");
       } else {
         display.print("Temp: ");
         display.print(daten.temperatur, 1);
         display.println(" C");
       }
       
       // Zeile 4: Luftfeuchtigkeit (NEU! War vorher nicht auf dem Display!)
       if (!daten.dht_fehler) {
         display.print("Luft: ");
         display.print(daten.luftfeuchtigkeit, 1);
         display.println(" %");
       } else {
         display.println("Luft: FEHLER!");
       }
       
       // Zeile 5: Bodenfeuchtigkeit
       display.print("Boden: ");
       display.println(daten.bodenwert);
       
       // Zeile 6: Wasserstand
       if (daten.wasser_fehler) {
         display.println("Wasser: FEHLER!");
       } else {
         display.print("Wasser: ");
         display.print(daten.wasser_prozent);
         display.println("%");
       }
       
       // Zeile 7: Pumpen-Status + Sicherheit
       if (daten.wasser_leer) {
         display.println("!! TANK LEER !!");
       } else if(pumpe_aktiv) {
         display.println("PUMPE: >>> AN <<<");
       } else {
         display.println("Pumpe: AUS");
       }
       
       // Zeile 8: Uptime + Modus (NEU! Nutzt die letzte freie Zeile!)
       unsigned long sek = millis() / 1000;
       unsigned long min = sek / 60; sek = sek % 60;
       unsigned long std = min / 60; min = min % 60;
       display.print(std); display.print(":");
       if (min < 10) display.print("0");
       display.print(min); display.print(":");
       if (sek < 10) display.print("0");
       display.print(sek);
       // Modus rechts anzeigen
       if (automatik_aktiv) {
         display.println("    [AUTO]");
       } else {
         display.println("  [MANUELL]");
       }
       
       display.display();
       
       // Status-LED aktualisieren
       updateStatusLED(daten.wasser_prozent, daten.wasser_fehler);
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

// TASK 3: Bewaesserung Logik (mit Trockenlauf-Schutz)
void taskLogik(void *pv) {
  SensorDaten daten;
  while(1) {
    if(xQueuePeek(sensorQueue, &daten, 100)) {
       
       bool logStart = false;
       bool logStop = false;
       bool logSperre = false;

       if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
         
         // NEU v4.3: Trockenlauf-Sperre pruefen
         if (daten.wasser_leer || daten.wasser_fehler) {
           // Tank leer oder Sensor defekt -> Pumpe SOFORT STOPPEN!
           if (pumpe_aktiv) {
             pumpe_aktiv = false;
             digitalWrite(RELAIS_PIN, LOW);
             logSperre = true;
           }
           tank_sperre = true;  // Sperre aktivieren
         } else {
           tank_sperre = false; // Sperre aufheben wenn Wasser da
         }
         
         // Automatik-Logik (nur wenn KEINE Sperre aktiv)
         if (!daten.boden_fehler && automatik_aktiv && !manueller_modus 
             && !warte_nach_giessen && !tank_sperre) {
            
            if (daten.bodenwert > BODEN_SCHWELLWERT && !pumpe_aktiv) {
               pumpe_aktiv = true;
               digitalWrite(RELAIS_PIN, HIGH);
               giess_zaehler++;
               letztes_giessen = millis();
               warte_nach_giessen = true;
               logStart = true;
            }
         }
         
         // Stoppen nach Zeit
         if (pumpe_aktiv && automatik_aktiv && warte_nach_giessen) {
            if (millis() - letztes_giessen > 3000) {
               pumpe_aktiv = false;
               digitalWrite(RELAIS_PIN, LOW);
               logStop = true;
            }
         }
         
         // Timer update
         if (warte_nach_giessen && (millis() - letztes_giessen > 30000)) {
            warte_nach_giessen = false;
         }
         
         xSemaphoreGive(dataMutex);
         
         if (logStart) Serial.println("[LOGIK] Automatik Start");
         if (logStop) Serial.println("[LOGIK] Automatik Stopp");
         if (logSperre) Serial.println("[LOGIK] !! TROCKENLAUF-SPERRE !! Pumpe gestoppt!");
       }
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// TASK 5: Webserver
void taskWeb(void *pv) {
  // Routes definieren
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
     request->send_P(200, "text/html", index_html);
  });
  
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request){
     SensorDaten daten;
     xQueuePeek(sensorQueue, &daten, 10);
     
     JsonDocument doc;
     
     doc["temperature"] = daten.temperatur;
     doc["humidity"] = daten.luftfeuchtigkeit;
     doc["soil_moisture"] = map(daten.bodenwert, 4095, 0, 0, 100);
     // NEU v4.3: Wasserstand
     doc["water_level"] = daten.wasser_prozent;
     doc["water_error"] = daten.wasser_fehler;
     doc["water_distance"] = daten.wasser_distanz_cm;
     
     if(xSemaphoreTake(dataMutex, 10)) {
       doc["pump_state"] = pumpe_aktiv;
       doc["auto_mode"] = automatik_aktiv;
       doc["watering_count"] = giess_zaehler;
       doc["tank_locked"] = tank_sperre; // NEU v4.3
       doc["uptime"] = millis() / 1000;
       xSemaphoreGive(dataMutex);
     } else {
        doc["error"] = "busy";
     }
     
     String json;
     serializeJson(doc, json);
     request->send(200, "application/json", json);
  });
  
  server.on("/toggle", HTTP_GET, [](AsyncWebServerRequest *request){
     if (request->hasParam("target")) {
        String t = request->getParam("target")->value();
        
        // Kurzer Timeout für Webserver!
        if(xSemaphoreTake(dataMutex, 10)) {
           // Automatik umschalten
           if(t == "auto") {
              automatik_aktiv = !automatik_aktiv;
           }
           
            // Pumpe manuell umschalten (NEU v4.3: gesperrt wenn Tank leer!)
            if(t == "pump" && !tank_sperre) {
               pumpe_aktiv = !pumpe_aktiv;
               digitalWrite(RELAIS_PIN, pumpe_aktiv ? HIGH : LOW);
               manueller_modus = true;
            }
           
           xSemaphoreGive(dataMutex);
           
           // Flag setzen für Update im Main Loop (Thread-Safe!)
           if(xSemaphoreTake(dataMutex, 10)) {
              blynk_update_requested = true; 
              xSemaphoreGive(dataMutex);
           }
        } // Ende: if(xSemaphoreTake)
     } // Ende: if (request->hasParam)
     
     request->send(200, "text/plain", "OK");
  });

  server.begin();
  
  while(1) {
    vTaskDelay(10000 / portTICK_PERIOD_MS);
  }
}

// BLYNK läuft im LOOP - nicht als Task!

// -------------------------------------------------------------------------
// 7. SETUP
// -------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  
  // Hardware Init
  dht.begin();
  pinMode(BODEN_PIN, INPUT);
  pinMode(RELAIS_PIN, OUTPUT);
  digitalWrite(RELAIS_PIN, LOW); // Sicherstellen dass Pumpe aus ist
  
  // NEU v4.3: HC-SR04 Pins
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);
  
  // NEU v4.3: Status-LED Pins
  pinMode(LED_ROT_PIN, OUTPUT);
  pinMode(LED_GRUEN_PIN, OUTPUT);
  digitalWrite(LED_ROT_PIN, LOW);
  digitalWrite(LED_GRUEN_PIN, HIGH); // Gruen = System startet
  
  // =========================================================================
  // NEU v4.4: Display-Initialisierung fuer 1.3" OLED
  // SCHRITT 1: I2C Scanner - findet die tatsaechliche Adresse des Displays
  // SCHRITT 2: Display mit gefundener Adresse initialisieren
  // =========================================================================
  
  // 1. I2C Bus starten
  Wire.begin(OLED_SDA, OLED_SCK);
  delay(100);
  
  // 2. I2C Scanner - Welche Geraete sind angeschlossen?
  Serial.println("\n=== I2C SCANNER ===");
  int gefundene_geraete = 0;
  uint8_t display_adresse = 0x3C; // Standard
  
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.print("I2C Geraet gefunden auf Adresse: 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      gefundene_geraete++;
      // Wenn es eine typische OLED-Adresse ist, merken
      if (addr == 0x3C || addr == 0x3D) {
        display_adresse = addr;
        Serial.print(">> Das ist das OLED Display! Adresse: 0x");
        Serial.println(addr, HEX);
      }
    }
  }
  
  if (gefundene_geraete == 0) {
    Serial.println("KEINE I2C Geraete gefunden! Verkabelung pruefen!");
    Serial.println("SDA -> GPIO 21, SCL -> GPIO 22");
  } else {
    Serial.print("Gesamt: ");
    Serial.print(gefundene_geraete);
    Serial.println(" I2C Geraete gefunden.");
  }
  Serial.println("===================\n");
  
  // 3. Display initialisieren mit gefundener Adresse
  delay(250);
  if(!display.begin(display_adresse, true)) {
    Serial.println("OLED Init Fehler!");
    Serial.println("Moeglicherweise falscher Treiber-Chip!");
    Serial.println("Falls SSD1306 statt SH1106: Alte Bibliothek verwenden!");
  } else {
    Serial.print("OLED 1.3\" initialisiert auf Adresse 0x");
    Serial.println(display_adresse, HEX);
  }
  
  // 4. Display-Buffer komplett loeschen
  display.clearDisplay();
  display.display();
  delay(100);
  
  // 5. Boot-Nachricht auf dem OLED anzeigen
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("== HydroMate v4.4 ==");
  display.println("--------------------");
  display.println("");
  display.println("  OLED: 1.3\" SH1106");
  display.println("  Starte System...");
  display.display();

  if(!SPIFFS.begin(true)){
     Serial.println("SPIFFS Fehler");
  }

  // FreeRTOS Objekte
  // WICHTIG: Queue Größe 1 für "Mailbox", damit Overwrite funktioniert
  sensorQueue = xQueueCreate(1, sizeof(SensorDaten)); 
  dataMutex = xSemaphoreCreateMutex();
  serialMutex = xSemaphoreCreateMutex();
  
  // WiFi Verbindung aufbauen - Multi-WiFi Support
  // Der ESP32 probiert alle 3 Netzwerke und nimmt das erste verfügbare
  
  Serial.println("Starte Multi-WiFi Verbindung...");
  
  // Array mit allen WiFi-Netzwerken
  struct WiFiNetwork {
    const char* ssid;
    const char* password;
    const char* name; // Für Anzeige
  };
  
  WiFiNetwork networks[] = {
    {ssid_1, password_1, "Zu Hause"},
    {ssid_2, password_2, "TEKO Schule"},
    {ssid_3, password_3, "Handy Hotspot"}
  };
  
  bool connected = false;
  
  // Probiere alle Netzwerke durch
  for(int i = 0; i < 3 && !connected; i++) {
    Serial.print("Versuche: ");
    Serial.print(networks[i].name);
    Serial.print(" (");
    Serial.print(networks[i].ssid);
    Serial.println(")");
    
    WiFi.begin(networks[i].ssid, networks[i].password);
    
    // Warte maximal 10 Sekunden
    int tryCount = 0;
    while (WiFi.status() != WL_CONNECTED && tryCount < 20) {
      delay(500);
      Serial.print(".");
      tryCount++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      WiFi.setSleep(false); // WICHTIG: Verhindert Abstürze bei ESP32 Core 3.x
      Serial.println("\n✔ WiFi VERBUNDEN!");
      Serial.print("Netzwerk: ");
      Serial.println(networks[i].name);
      Serial.print("IP Adresse: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\n✗ Verbindung fehlgeschlagen");
    }
  }
  
  if (!connected) {
    Serial.println("FEHLER: Kein WiFi verfügbar!");
    Serial.println("System läuft offline (nur lokale Funktionen)");
  }

  // -------------------------------------------------------------------------
  // TASKS STARTEN
  // -------------------------------------------------------------------------
  // xTaskCreatePinnedToCore Parameter:
  //   1. Funktion        - Welche Funktion als Task laufen soll
  //   2. Name            - Name fuer Debugging (max 16 Zeichen)
  //   3. Stack-Groesse   - Speicher in Bytes fuer diesen Task
  //   4. Parameter       - Daten die an den Task uebergeben werden (NULL = keine)
  //   5. Prioritaet      - Hoeher = wichtiger (3 = hoechste hier)
  //   6. Handle          - Zeiger zum Ueberwachen/Steuern des Tasks
  //   7. Core            - Auf welchem CPU-Kern der Task laeuft (0 oder 1)
  
  // Core 1: Sensoren, Display, Logik, Blynk, Statistik
  // Core 0: Web (weil WiFi-Stack auf Core 0 laeuft)
  xTaskCreatePinnedToCore(taskSensorenLesen, "Sensoren",   4096, NULL, 2, &taskSensorenHandle,  1);
  xTaskCreatePinnedToCore(taskDisplay,       "Display",    4096, NULL, 1, &taskDisplayHandle,   1);
  xTaskCreatePinnedToCore(taskLogik,         "Logik",      4096, NULL, 3, &taskLogikHandle,     1);
  xTaskCreatePinnedToCore(taskWeb,           "Web",        8192, NULL, 1, &taskWebHandle,       0);
  xTaskCreatePinnedToCore(taskStatistik,     "Statistik",  2048, NULL, 1, &taskStatistikHandle, 1);
  
  // Blynk.config MUSS in setup laufen (ESP32 v3.x Network Stack!)
  Blynk.config(BLYNK_AUTH_TOKEN);
  
  Serial.println("HydroMate v4.4 gestartet - SH1106 1.3\" OLED + HC-SR04 + LED Ampel aktiv");
}

unsigned long letzterBlynkSend = 0;

void loop() {
  // Blynk MUSS hier laufen!
  if (WiFi.status() == WL_CONNECTED) {
     Blynk.run();
     
     // Check auf Webserver-Änderungen (Thread-Safe Sync)
     bool doSync = false;
     if(xSemaphoreTake(dataMutex, 10)) {
        if(blynk_update_requested) {
           doSync = true;
           blynk_update_requested = false;
        }
        xSemaphoreGive(dataMutex);
     }
     
     if(doSync && Blynk.connected()) {
        // Werte lesen für Sync
        int send_auto = 0;
        int send_pump = 0;
        if(xSemaphoreTake(dataMutex, 10)) {
           send_auto = automatik_aktiv ? 1 : 0;
           send_pump = pumpe_aktiv ? 1 : 0;
           xSemaphoreGive(dataMutex);
        }
        Blynk.virtualWrite(V6, send_auto);
        Blynk.virtualWrite(V7, send_pump);
     }
  }
  
  // Daten senden
  if (millis() - letzterBlynkSend > 15000) {
     letzterBlynkSend = millis();
     
     SensorDaten daten;
     if(xQueuePeek(sensorQueue, &daten, 100)) {
        Blynk.virtualWrite(V0, daten.temperatur);
        Blynk.virtualWrite(V1, daten.luftfeuchtigkeit);
        Blynk.virtualWrite(V2, daten.bodenwert);
         // NEU v4.3: Wasserstand an Blynk senden
         Blynk.virtualWrite(V3, daten.wasser_prozent);
     }
     
     if(xSemaphoreTake(dataMutex, 100)) {
        Blynk.virtualWrite(V4, pumpe_aktiv ? 1 : 0);
        Blynk.virtualWrite(V5, giess_zaehler);
        Blynk.virtualWrite(V6, automatik_aktiv ? 1 : 0);
         Blynk.virtualWrite(V8, tank_sperre ? 1 : 0); // NEU: Tank-Sperre
        xSemaphoreGive(dataMutex);
     }
  }
  
  vTaskDelay(10 / portTICK_PERIOD_MS);
}

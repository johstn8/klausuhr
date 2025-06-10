/*
  ExamTimer_ESP32.ino – Version 2025‑06‑05
  ==========================================================
  Steuer‑ und Anzeige‑Firmware für den schriftlichen Prüfungs‑Timer
  ----------------------------------------------------------------
  ▸ Hardware  : ESP32‑WROOM‑32 Dev‑Kit, 17 WS2812B‑Strip‑Segmente
  ▸ Anzeige   : • 5‑stelliger Countdown‑Timer (Minuten:Sekunden)
                • 5‑stelliger Nachteilsausgleich (Bonus‑Timer)
                • 5‑stellige Digitaluhr (Stunden:Minuten:Sekunden)
                • Zweizeilige Ladebalken‑Anzeige (40 & 39 Pixel)
  ▸ Netzwerk  : Dual‑Mode WLAN  ➜  Station‑Verbindung + eigener AP
  ▸ Web‑UI    : index.html (LittleFS)  |  REST‑Endpunkte „/executeFunction“ & „/resetTimer“
  ▸ OTA       : ElegantOTA über WebServer
  ▸ Max AP‑Clients : 4

  Aufbau der Datei
  ────────────────
  1. Bibliotheken & globale Konstanteinstellungen
  2. Initialisierung der NeoPixel‑Strips (17 Objekte)
  3. Datentyp „State“ für aktuellen Programm‑Mode
  4. Hilfsfunktionen  (clearAll, showAll, draw7Seg)
  5. Renderfunktionen (drawClock, drawCountdown, drawBonus)
  6. setup()  ➜  Hardware‑Init, WiFi/AP, DNS, WebServer, NTP
  7. loop()   ➜  DNS + WebServer + Display‑Routine‑Dispatcher
  8. Liste möglicher, aber noch nicht eingebundener Erweiterungen
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <DNSServer.h>
#include <FS.h>
#include <LittleFS.h>
#include <Adafruit_NeoPixel.h>
#include <time.h>

// ───────────────────────────── WLAN & NTP ─────────────────────────────
// Zugangsdaten für das Schul‑WLAN (Station‑Modus)
const char* STA_SSID = "<YOUR_SSID>";   // ← hier SSID eintragen
const char* STA_PASS = "<YOUR_PASS>";   // ← hier Passwort eintragen

// Angaben für den Access‑Point, den der ESP selbst öffnet
const char* AP_SSID  = "Exam Timer";
const char* AP_PASS  = "ExamTimer123";

WebServer server(80);          // synchroner HTTP‑Server
DNSServer dns;                 // leitet jedes Host‑Label auf die Portal‑IP um

// Statische AP‑IP, damit der Captive‑Portal‑DNS immer greift
const IPAddress AP_IP   (192, 168, 4,  1);
const IPAddress NETMASK (255, 255, 255, 0);

// Zeitzone : Europa/Berlin (UTC+1 / UTC+2 im Sommer)
constexpr long GMT_OFFSET = 3600;    // Sekunden
constexpr int  DST_OFFSET = 3600;    // Sekunden für Sommerzeit
const char*    NTP_POOL[] = { "de.pool.ntp.org", "pool.ntp.org", "time.nist.gov" };

// ─────────────────────────── LED‑Konstanten ───────────────────────────
#define NEO_TYPE   (NEO_GRB + NEO_KHZ800)   // WS2812B Timing & Farbreihenfolge
#define BRIGHTNESS 48                       // Grundhelligkeit 0‑255 ≈ 20 %
const unsigned long LED_TEST_DURATION  = 120000;  // 2 Minuten in Millisekunden
const unsigned long LED_TEST_INTERVAL  = 1000;    // Farbwechsel alle Sekunde

// Pixel‑Zahlen je Segment
constexpr uint16_t NUM_TIMER_LED = 21;   // je Digit des Countdowns
constexpr uint16_t NUM_NACH_LED  = 11;   // je Digit des Nachteilsausgleichs
constexpr uint16_t NUM_CLOCK_LED = 17;   // je Digit der Uhrzeit
constexpr uint16_t NUM_BAR_TOP   = 40;   // Ladebalken oben
constexpr uint16_t NUM_BAR_BOT   = 39;   // Ladebalken unten

// GPIO‑Belegung für alle 17 Strips (ein Pin pro Strip)
const uint8_t PIN_T[5] = {  2,  4, 16, 17,  5 };  // Timer
const uint8_t PIN_N[5] = { 18, 19, 21, 22, 23 };  // Nachteil
const uint8_t PIN_C[5] = { 12, 13, 14, 27, 26 };  // Clock
const uint8_t PIN_BAR_TOP = 32;                   // Ladebalken oben
const uint8_t PIN_BAR_BOT = 33;                   // Ladebalken unten

// ────────────────────────── NeoPixel‑Objekte ──────────────────────────
// Fünf parallele Strips für jeden Anzeige‑Bereich erzeugen
Adafruit_NeoPixel timerStrips[5] = {
  {NUM_TIMER_LED, PIN_T[0], NEO_TYPE}, {NUM_TIMER_LED, PIN_T[1], NEO_TYPE},
  {NUM_TIMER_LED, PIN_T[2], NEO_TYPE}, {NUM_TIMER_LED, PIN_T[3], NEO_TYPE},
  {NUM_TIMER_LED, PIN_T[4], NEO_TYPE}
};
Adafruit_NeoPixel nachStrips[5] = {
  {NUM_NACH_LED,  PIN_N[0], NEO_TYPE}, {NUM_NACH_LED,  PIN_N[1], NEO_TYPE},
  {NUM_NACH_LED,  PIN_N[2], NEO_TYPE}, {NUM_NACH_LED,  PIN_N[3], NEO_TYPE},
  {NUM_NACH_LED,  PIN_N[4], NEO_TYPE}
};
Adafruit_NeoPixel clockStrips[5] = {
  {NUM_CLOCK_LED, PIN_C[0], NEO_TYPE}, {NUM_CLOCK_LED, PIN_C[1], NEO_TYPE},
  {NUM_CLOCK_LED, PIN_C[2], NEO_TYPE}, {NUM_CLOCK_LED, PIN_C[3], NEO_TYPE},
  {NUM_CLOCK_LED, PIN_C[4], NEO_TYPE}
};
Adafruit_NeoPixel barTop(NUM_BAR_TOP, PIN_BAR_TOP, NEO_TYPE);
Adafruit_NeoPixel barBot(NUM_BAR_BOT, PIN_BAR_BOT, NEO_TYPE);

// ───────────────────────── Programm‑Zustand ───────────────────────────
struct State {
  enum Phase { SHOW_CLOCK, COUNTDOWN, BONUS } phase = SHOW_CLOCK;
  time_t   cdEnd    = 0;       // Endzeitpunkt des Countdowns (Unix‑Zeit)
  uint32_t bonusSec = 0;       // Länge des Nachteilsausgleichs
  uint32_t cdDur    = 0;       // Dauer des Countdowns (Sekunden)
} state;

// ───────────── LED‑Selbsttest‑Steuerung ─────────────
bool     ledTestRunning     = false;
uint32_t ledTestStart       = 0;
uint32_t ledTestLastChange  = 0;
uint8_t  ledTestColorIndex  = 0;

// ───────────────────── Hilfsfunktionen für die LEDs ───────────────────
// Löscht alle Strips (schwarz)
inline void clearAll() {
  for (auto& s : timerStrips)  s.clear();
  for (auto& s : nachStrips)   s.clear();
  for (auto& s : clockStrips)  s.clear();
  barTop.clear(); barBot.clear();
}

// Zeigt alle Strips gleichzeitig an
inline void showAll() {
  for (auto& s : timerStrips)  s.show();
  for (auto& s : nachStrips)   s.show();
  for (auto& s : clockStrips)  s.show();
  barTop.show(); barBot.show();
}

// Füllt alle Strips mit derselben Farbe
inline void fillAll(uint8_t r, uint8_t g, uint8_t b) {
  uint32_t c = timerStrips[0].Color(r, g, b);
  for (auto& s : timerStrips)  s.fill(c);
  c = nachStrips[0].Color(r, g, b);
  for (auto& s : nachStrips)   s.fill(c);
  c = clockStrips[0].Color(r, g, b);
  for (auto& s : clockStrips)  s.fill(c);
  barTop.fill(barTop.Color(r, g, b));
  barBot.fill(barBot.Color(r, g, b));
  showAll();
}

// Eine einfache LED-Testsequenz (Rot → Grün → Blau → Weiß)
inline void runLedTest() {
  static const uint8_t colors[4][3] = {
    {255, 0,   0},
    {0,   255, 0},
    {0,   0,   255},
    {255, 255, 255}
  };
  uint32_t now = millis();
  if (now - ledTestLastChange >= LED_TEST_INTERVAL) {
    ledTestColorIndex = (ledTestColorIndex + 1) % 4;
    ledTestLastChange = now;
  }
  const uint8_t* col = colors[ledTestColorIndex];
  fillAll(col[0], col[1], col[2]);
}

/*
  draw7Seg(): Abstrakte Platzhalter‑Routine
  ---------------------------------------
  ▸ Wandelt die gegebene Ziffer (0‑9) in ein 7‑Segment‑Muster um und färbt sie.
  ▸ Aktuell werden *alle* Pixel des übergebenen Strips einfärbt, weil die
    exakte Segment‑Zuordnung noch nicht implementiert ist.
*/
inline void draw7Seg(Adafruit_NeoPixel& strip, uint8_t digit,
                     uint8_t r, uint8_t g, uint8_t b) {
  (void)digit;                           // Platzhalter – Ziffer wird noch ignoriert
  strip.fill(strip.Color(r, g, b));      // komplette Farbe setzen
}

// ───────────────────────── Rendering‑Routinen ─────────────────────────
// ▸ drawClock()     – Aktuelle Uhrzeit (Stunden, Minuten, Sekunden)
// ▸ drawCountdown() – Laufender Countdown (Minuten, Sekunden)
// ▸ drawBonus()     – Laufender Nachteilsausgleich (Minuten, Sekunden)

void drawClock() {
  clearAll();
  struct tm t {}; time_t now = time(nullptr); localtime_r(&now, &t);
  uint8_t h = t.tm_hour, m = t.tm_min, s = t.tm_sec;
  draw7Seg(clockStrips[0], h / 10, 255, 0,   0);   // Zehner Stunden – rot
  draw7Seg(clockStrips[1], h % 10, 255, 0,   0);   // Einer  Stunden – rot
  draw7Seg(clockStrips[2], m / 10, 0,   255, 0);   // Zehner Minuten – grün
  draw7Seg(clockStrips[3], m % 10, 0,   255, 0);   // Einer  Minuten – grün
  draw7Seg(clockStrips[4], s / 10, 0,   0,   255); // Zehner Sekunden – blau
  showAll();
}

void drawCountdown(time_t now) {
  clearAll();
  int32_t rem = state.cdEnd - now;          // Restsekunden
  if (rem < 0) rem = 0;
  uint8_t m = rem / 60, s = rem % 60;
  draw7Seg(timerStrips[0], m / 10, 255, 128, 0);   // orange/gelb
  draw7Seg(timerStrips[1], m % 10, 255, 128, 0);
  draw7Seg(timerStrips[2], s / 10, 255, 255, 0);
  draw7Seg(timerStrips[3], s % 10, 255, 255, 0);
  // Ladebalken proportional füllen
  float progress = 1.0f - rem / (float)state.cdDur;
  barTop.fill(barTop.Color(0, 255, 0), 0, progress * NUM_BAR_TOP);
  barBot.fill(barBot.Color(0, 255, 0), 0, progress * NUM_BAR_BOT);
  showAll();
}

void drawBonus(time_t now) {
  clearAll();
  int32_t rem = state.cdEnd + state.bonusSec - now;
  if (rem < 0) rem = 0;
  uint8_t m = rem / 60, s = rem % 60;
  draw7Seg(nachStrips[0], m / 10, 128, 0, 128);     // lila
  draw7Seg(nachStrips[1], m % 10, 128, 0, 128);
  draw7Seg(nachStrips[2], s / 10, 138, 43, 226);    // blauviolett
  draw7Seg(nachStrips[3], s % 10, 138, 43, 226);
  float progress = 1.0f - rem / (float)state.bonusSec;
  barTop.fill(barTop.Color(128, 0, 128), 0, progress * NUM_BAR_TOP);
  barBot.fill(barBot.Color(128, 0, 128), 0, progress * NUM_BAR_BOT);
  showAll();
}

// ───────────────────────────── SETUP ───────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);                       // Stabilisierung nach Reset

  // 1) LED‑Streifen initialisieren
  auto initStrip = [](Adafruit_NeoPixel& s) { s.begin(); s.setBrightness(BRIGHTNESS); s.show(); };
  for (auto& s : timerStrips)  initStrip(s);
  for (auto& s : nachStrips)   initStrip(s);
  for (auto& s : clockStrips)  initStrip(s);
  initStrip(barTop);
  initStrip(barBot);

  // 2) LittleFS einbinden (Web‑Dateien)
  if (!LittleFS.begin(true)) {
    Serial.println(F("LittleFS Mount failed – stopping."));
    while (true) delay(200);
  }

  // 3) WLAN Setup
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(STA_SSID, STA_PASS);                        // Verbindung zur Schule
  WiFi.softAPConfig(AP_IP, AP_IP, NETMASK);              // fixe AP‑Adresse
  WiFi.softAP(AP_SSID, AP_PASS, 6 /*Channel*/, 0 /*sichtbar*/, 4 /*max*/);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);                   // volle Sendeleistung
  dns.start(53, "*", AP_IP);                            // alle Domains -> Captive‑Portal
  Serial.printf("AP bereit: SSID='%s'  IP=%s\n", AP_SSID, AP_IP.toString().c_str());

  // 4) Zeit per NTP holen
  configTime(GMT_OFFSET, DST_OFFSET, NTP_POOL[0], NTP_POOL[1], NTP_POOL[2]);

  // 5) Web‑Routen
  server.on("/", HTTP_GET, [](){                      // Root → index.html liefern
    File f = LittleFS.open("/index.html", "r");
    server.streamFile(f, "text/html"); f.close();
  });

  server.on("/executeFunction", HTTP_GET, [](){       // Countdown starten
    if (!server.hasArg("time") || !server.hasArg("bonus")) {
      server.send(400, "text/plain", "time & bonus args missing");
      return;
    }
    state.cdDur    = server.arg("time").toInt();
    state.bonusSec = server.arg("bonus").toInt();
    state.cdEnd    = time(nullptr) + state.cdDur;
    state.phase    = State::COUNTDOWN;
    server.send(200, "text/plain", "ok");
  });

  server.on("/resetTimer", HTTP_GET, [](){             // Timer zurücksetzen
    state.phase = State::SHOW_CLOCK;
    server.send(200, "text/plain", "reset ok");
  });

  server.on("/led-post", HTTP_GET, [](){               // LED-Selbsttest starten
    if (ledTestRunning) {
      server.send(200, "text/plain", "LED-Test läuft bereits");
    } else {
      ledTestRunning    = true;
      ledTestStart      = millis();
      ledTestLastChange = ledTestStart;
      ledTestColorIndex = 0;
      server.send(200, "text/plain", "LED-Test gestartet");
    }
  });

  ElegantOTA.begin(&server);                            // OTA‑Flash via /update
  server.begin();
}

// ───────────────────────────── LOOP ────────────────────────────────
void loop() {
  dns.processNextRequest();       // Captive‑Portal DNS annehmen
  server.handleClient();          // HTTP‑Anfragen bearbeiten

  if (ledTestRunning) {
    uint32_t nowMs = millis();
    if (nowMs - ledTestStart >= LED_TEST_DURATION) {
      ledTestRunning = false;
      clearAll();
      showAll();
    } else {
      runLedTest();
      return;                    // Anzeige während des Tests
    }
  }

  time_t now = time(nullptr);     // aktuelle Sekundenzahl

  switch (state.phase) {
    case State::SHOW_CLOCK:
      drawClock();
      break;

    case State::COUNTDOWN:
      if (now >= state.cdEnd) {
        state.phase = state.bonusSec ? State::BONUS : State::SHOW_CLOCK;
      } else {
        drawCountdown(now);
      }
      break;

    case State::BONUS:
      if (now >= state.cdEnd + state.bonusSec) {
        state.phase = State::SHOW_CLOCK;
      } else {
        drawBonus(now);
      }
      break;
  }
}

// ─────────────────────── Noch nicht eingebundene Features ─────────────
/*
  ▸ draw7Seg(): Aktuell simple Vollflächen‑Färbung. Eine echte 7‑Segment‑
               Bitmap‑Zuordnung (pro Pixel) kann eingebunden werden, um
               die Ziffern realistisch darzustellen.
  ▸ Helligkeits‑Regelung über Web‑Endpoint (z. B. /brightness?val=128)
  ▸ Persistente Speicherung (LittleFS/Preferences) von WLAN‑Credentials
    und Brightness für Stand‑Alone‑Betrieb ohne erneutes Flashen.
  ▸ WebSocket‑Push für Live‑Timer‑Updates im Browser
  ▸ Automatische Umschaltung auf Bonus‑Phase ohne HTTP‑Antwort (z. B. LED
    Animation), zurzeit nur Farbwechsel.
  ▸ Adaptive Tx‑Power je nach RSSI – der Code setzt aktuell fix den
    Maximalwert.
*/

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
  4. Hilfsfunktionen  (clearAll, showAll, drawDigit)
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
#include "font7seg.h"
#include <time.h>
#include <esp_pm.h> // Fehlerbehebung: WLAN-Disconnection bei Aufruf von Website: Light-Sleep-Modus deaktivieren
#include <esp_wifi.h>

// ───────────────────────────── WLAN & NTP ─────────────────────────────
// Globaler Handle für No-Light-Sleep | Fehlerbehebung: WLAN-Disconnection bei Aufruf von Website
esp_pm_lock_handle_t pm_lock_l0;

// Zugangsdaten für das Schul‑WLAN (Station‑Modus)
const char* STA_SSID = "<YOUR_SSID>";   // ← hier SSID eintragen
const char* STA_PASS = "<YOUR_PASS>";   // ← hier Passwort eintragen

// Angaben für den Access‑Point, den der ESP selbst öffnet
const char* AP_SSID  = "KlausUhr";
const char* AP_PASS  = "KlausUhr";

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

// Prozentuale Schwellen
constexpr uint8_t WARN_THRESHOLD_PCT  = 20;   // ab hier rote Darstellung
constexpr uint8_t BLINK_LAST_PCT      = 5;    // letzten % blinken 1 Hz

// GPIO‑Belegung für alle 17 Strips (ein Pin pro Strip)
const uint8_t PIN_T[5] = {  2,  4, 16, 17,  5 };  // Timer
const uint8_t PIN_N[5] = { 18, 19, 21, 22, 23 };  // Nachteil
const uint8_t PIN_C[5] = { 13, 12, 14, 27, 26 };  // Clock
const uint8_t PIN_BAR_TOP = 33;                   // Ladebalken oben (40 LED)
const uint8_t PIN_BAR_BOT = 32;                   // Ladebalken unten (39 LED)

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

// ────────────── NTP-Zeit-Handling ──────────────
bool     timeSynced        = false;   // true, sobald Uhrzeit erfolgreich geholt
unsigned long nextTimeSync = 0;       // millis() für nächsten Sync-Versuch
bool     firstSyncAttempt  = true;    // steuert 1‑minütigen Retry

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

// Holt die Uhrzeit über NTP. Gibt true bei Erfolg zurück.
bool syncTimeNow() {
  Serial.println(F("Versuche NTP-Zeit abzurufen..."));

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(STA_SSID, STA_PASS);

  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
    delay(100);
  }

  bool ok = false;
  if (WiFi.status() == WL_CONNECTED) {
    configTime(GMT_OFFSET, DST_OFFSET, NTP_POOL[0], NTP_POOL[1], NTP_POOL[2]);
    struct tm tinfo {};
    if (getLocalTime(&tinfo, 10000)) {
      Serial.println(F("NTP Sync erfolgreich"));
      ok = true;
    } else {
      Serial.println(F("NTP Sync fehlgeschlagen"));
    }
  } else {
    Serial.println(F("WLAN-Verbindung fehlgeschlagen"));
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);

  return ok;
}

// Ein Pixel unter Berücksichtigung der Strip-Richtung setzen
inline void setPixel(Adafruit_NeoPixel& strip, uint16_t pos, bool reversed,
                     uint8_t r, uint8_t g, uint8_t b) {
  uint16_t idx = reversed ? strip.numPixels() - 1 - pos : pos;
  strip.setPixelColor(idx, strip.Color(r, g, b));
}

// Zeichnet ein 3x5-Digit an gegebener Startspalte
inline void drawDigit(Adafruit_NeoPixel* strips, uint16_t start, bool reversed,
                      uint8_t digit, uint8_t r, uint8_t g, uint8_t b) {
  for (uint8_t row = 0; row < 5; ++row) {
    uint8_t bits = pgm_read_byte(&FONT_3x5[digit][row]);
    for (uint8_t col = 0; col < 3; ++col) {
      bool on = bits & (1 << (2 - col));
      setPixel(strips[row], start + col, reversed,
               on ? r : 0, on ? g : 0, on ? b : 0);
    }
  }
}

// Doppelpunkt zeichnen (Punkte in Zeile 2 und 4)
inline void drawColon(Adafruit_NeoPixel* strips, uint16_t col, bool reversed,
                      uint8_t r, uint8_t g, uint8_t b) {
  const bool dots[5] = {false, true, false, true, false};
  for (uint8_t row = 0; row < 5; ++row) {
    if (dots[row]) setPixel(strips[row], col, reversed, r, g, b);
    else           setPixel(strips[row], col, reversed, 0, 0, 0);
  }
}

// ───────────────────────── Rendering‑Routinen ─────────────────────────
// ▸ drawClock()     – Aktuelle Uhrzeit (Stunden, Minuten)
// ▸ drawCountdown() – Laufender Countdown (Minuten, Sekunden)
// ▸ drawBonusTop()    – Bonuszeit oben (nach Countdown)
// ▸ drawBonusBottom() – Bonuszeit unten während Countdowns

void drawClock() {
  clearAll();
  if (!timeSynced) {
    showAll();
    return;
  }
  struct tm t {}; time_t now = time(nullptr); localtime_r(&now, &t);
  uint8_t h = t.tm_hour, m = t.tm_min;
  // Stunden in Cyan, Minuten in Weiß
  drawDigit(clockStrips, 0,  false, h / 10, 0, 255, 255);
  drawDigit(clockStrips, 4,  false, h % 10, 0, 255, 255);
  drawColon(clockStrips, 8,  false, 255, 255, 255);
  drawDigit(clockStrips, 10, false, m / 10, 255, 255, 255);
  drawDigit(clockStrips, 14, false, m % 10, 255, 255, 255);
  showAll();
}

void drawCountdown(time_t now) {
  clearAll();
  int32_t rem = state.cdEnd - now;          // Restsekunden
  if (rem < 0) rem = 0;
  uint16_t totalMin = rem / 60;
  uint8_t s = rem % 60;

  float progress = state.cdDur ? rem / (float)state.cdDur : 0.0f; // 1.0 = Start, 0 = Ende
  bool warn  = progress <= (WARN_THRESHOLD_PCT / 100.0f);
  bool blink = progress <= (BLINK_LAST_PCT / 100.0f);

  if (blink && (now % 2)) {                      // jede zweite Sekunde dunkel
    showAll();
    return;
  }

  uint8_t r = warn ? 255 : 255;
  uint8_t g = warn ? 0   : 255;
  uint8_t b = warn ? 0   : 255;
  drawDigit(timerStrips, 0,  false, (totalMin / 100) % 10, r, g, b);
  drawDigit(timerStrips, 4,  false, (totalMin / 10) % 10, r, g, b);
  drawDigit(timerStrips, 8,  false, totalMin % 10, r, g, b);
  drawColon(timerStrips, 12, false, r, g, b);
  drawDigit(timerStrips, 14, false, s / 10, r, g, b);
  drawDigit(timerStrips, 18, false, s % 10, r, g, b);

  auto drawBar = [](Adafruit_NeoPixel& bar, uint16_t count, float prog) {
    bar.clear();
    int fill = round(prog * count);
    for (int i = 0; i < fill; ++i) {
      uint32_t c;
      if (i >= fill - 2)      c = bar.Color(255, 0, 0);      // letzte 2 -> Rot
      else if (i >= fill - 4) c = bar.Color(255, 255, 0);    // davor Gelb
      else                    c = bar.Color(0, 255, 0);      // Rest Grün
      bar.setPixelColor(i, c);
    }
  };

  drawBar(barTop, NUM_BAR_TOP, progress);
  drawBar(barBot, NUM_BAR_BOT, progress);
  showAll();
}

void drawBonusTop(time_t now) {
  clearAll();
  int32_t rem = state.cdEnd + state.bonusSec - now;
  if (rem < 0) rem = 0;
  uint16_t m = rem / 60;
  uint8_t r = 128, g = 0, b = 128;            // Lila
  drawDigit(timerStrips, 0,  false, (m / 100) % 10, r, g, b);
  drawDigit(timerStrips, 4,  false, (m / 10) % 10, r, g, b);
  drawDigit(timerStrips, 8,  false, m % 10, r, g, b);
  drawColon(timerStrips, 12, false, 0, 0, 0);
  drawDigit(timerStrips, 14, false, 0, 0, 0, 0);
  drawDigit(timerStrips, 18, false, 0, 0, 0, 0);
  float progress = 1.0f - rem / (float)state.bonusSec;
  int fillTop = round(progress * NUM_BAR_TOP);
  int fillBot = round(progress * NUM_BAR_BOT);
  barTop.clear();
  for (int i = 0; i < fillTop; ++i) {
    barTop.setPixelColor(i, barTop.Color(r, g, b));
  }
  barBot.clear();
  for (int i = 0; i < fillBot; ++i) {
    barBot.setPixelColor(i, barBot.Color(r, g, b));
  }
  showAll();
}

void drawBonusBottom(time_t now) {
  int32_t rem = state.cdEnd + state.bonusSec - now;
  if (rem < 0) rem = 0;
  uint16_t m = rem / 60;
  uint8_t r = 128, g = 0, b = 128;
  drawDigit(nachStrips, 0, true, (m / 100) % 10, r, g, b);
  drawDigit(nachStrips, 4, true, (m / 10) % 10, r, g, b);
  drawDigit(nachStrips, 8, true, m % 10, r, g, b);
  showAll();
}

// ───────────────────────────── SETUP ───────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(200);                       // Stabilisierung nach Reset

  // 1) Erzeuge und aktiviere den Lock, der Light-Sleep komplett verhindert | Fehlerbehebung: WLAN-Disconnection bei Aufruf von Website
  esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no_light_sleep", &pm_lock_l0);
  esp_pm_lock_acquire(pm_lock_l0);
  
  // 2) LED‑Streifen initialisieren
  auto initStrip = [](Adafruit_NeoPixel& s) { s.begin(); s.setBrightness(BRIGHTNESS); s.show(); };
  for (auto& s : timerStrips)  initStrip(s);
  for (auto& s : nachStrips)   initStrip(s);
  for (auto& s : clockStrips)  initStrip(s);
  initStrip(barTop);
  initStrip(barBot);

  // 3) LittleFS einbinden (Web‑Dateien)
  if (!LittleFS.begin(true)) {
    Serial.println(F("LittleFS Mount failed – stopping."));
    while (true) delay(200);
  }

  // 4) WLAN Setup (nur AP – STA wird für NTP temporär aktiviert)
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_IP, AP_IP, NETMASK);              // fixe AP‑Adresse
  WiFi.softAP(AP_SSID, AP_PASS, 6 /*Channel*/, 0 /*sichtbar*/, 4 /*max*/);
  WiFi.setSleep(false);                                   // Fehlerbehebung: WLAN-Disconnection zu Geräten bei Aufruf von Webseiten
  WiFi.setTxPower(WIFI_POWER_19_5dBm);                   // volle Sendeleistung
  dns.start(53, "*", AP_IP);                            // alle Domains -> Captive‑Portal
  Serial.printf("AP bereit: SSID='%s'  IP=%s\n", AP_SSID, AP_IP.toString().c_str());

  // 5) Ersten NTP-Sync planen (sofort)
  nextTimeSync = 0;

  // 6) Web‑Routen
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

  // NTP-Syncs abarbeiten
  if (millis() >= nextTimeSync) {
    bool ok = syncTimeNow();
    if (ok) {
      timeSynced       = true;
      firstSyncAttempt = true; // reset für spätere Ausfälle
      nextTimeSync     = millis() + 3600000UL; // stündlich neu syncen
    } else {
      timeSynced = false;
      if (firstSyncAttempt) {
        nextTimeSync = millis() + 60000UL; // nach 1 Minute erneut
        firstSyncAttempt = false;
      } else {
        nextTimeSync = millis() + 600000UL; // danach alle 10 Minuten
      }
    }
  }

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
        if (state.bonusSec) drawBonusBottom(now);
      }
      break;

    case State::BONUS:
      if (now >= state.cdEnd + state.bonusSec) {
        state.phase = State::SHOW_CLOCK;
      } else {
        drawBonusTop(now);
      }
      break;
  }
}

// ─────────────────────── Noch nicht eingebundene Features ─────────────
/*
  ▸ Segment-Schrift ist über FONT_3x5 definiert und wird von drawDigit
    genutzt, um reale 7‑Segment‑Muster darzustellen.
  ▸ Helligkeits‑Regelung über Web‑Endpoint (z. B. /brightness?val=128)
  ▸ Persistente Speicherung (LittleFS/Preferences) von WLAN‑Credentials
    und Brightness für Stand‑Alone‑Betrieb ohne erneutes Flashen.
  ▸ WebSocket‑Push für Live‑Timer‑Updates im Browser
  ▸ Automatische Umschaltung auf Bonus‑Phase ohne HTTP‑Antwort (z. B. LED
    Animation), zurzeit nur Farbwechsel.
  ▸ Adaptive Tx‑Power je nach RSSI – der Code setzt aktuell fix den
    Maximalwert.
*/

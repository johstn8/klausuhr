# KlausUhr – Aula‑Timer / LED‑Uhr 📘

> **Version:** Projektdokumentation v0.4 (07 Jun 2025)
> **Zielgruppe:** Firmware‑ & Frontend‑Dev‑Team
> **Scope:** Beschreibung / Architektur – *ohne Code‑Snippets*

---

## 1 Projektidee

KlausUhr ist eine **raumgroße LED‑Uhr** für schriftliche Prüfungen in der Aula. Sie vereint drei Funktionen:

1. **Countdown‑Timer** (Prüfungsdauer ↓)
2. **Nachteilsausgleich** – Bonus‑Timer für einzelne Schüler:innen
3. **Digitale Uhrzeit & Statusmeldungen** (falls kein Countdown läuft)

Alle Anzeigen sind farbcodiert und aus der letzten Reihe gut lesbar. Die Steuerung erfolgt per **ESP32** über ein responsives Web‑Interface im Schul‑WLAN (gleichzeitig AP‑Fallback).

---

## 2 Hardware‑Übersicht

| Position | Einheit             | LED‑Layout pro Strip | # Strips | Pixel gesamt | Zweck                                                |
| -------- | ------------------- | -------------------- | -------- | ------------ | ---------------------------------------------------- |
| oben     | **Timer‑Digits**    | 21 LED               | **5**    | 105          | MM : SS‑Countdown                               |
| Mitte    | **Loading Bar**    | 79 LED          | **1**    | 79           | Balken läuft synchron rückwärts – abgeschrägtes Ende |
| unten‑re | **Nachteil‑Digits** | 11 LED               | **5**    | 55           | Bonus‑Minuten                             |
| unten‑li | **Uhrzeit‑Digits**  | 17 LED               | **5**    | 85           | Uhrzeit (HH : MM) & Kurztexte                                  |

**Gesamt‑Pixel:** 105 + 79 + 55 + 85 = **324**  
**Controller:** ESP32‑WROOM‑32 DevKit (alle 16 GPIOs frei verfügbar)
**LED‑Typ:** WS2812B (NeoPixel)  
**Stromversorgung:** externes 5 V‑Netzteil (Dimensionierung erledigt)

### 2.1 GPIO‑Belegung (Default)

| Gruppe          | Pins (oben ➜ unten)             |
| --------------- | --------------------------------- |
| Timer‑Digits    | **2, 4, 16, 17, 5**               |
| Nachteil‑Digits | **18, 19, 21, 22, 23**            |
| Uhrzeit‑Digits  | **13, 12, 14, 27, 26**            |
| Loading Bar    | **33** (79 LED) |

Die 79 Pixel liegen als ein durchgehender Streifen, der oben bei Index 0 beginnt (40 LED)
und unten ab Index 40 weiterläuft (39 LED).

Die Pins, die für die jeweiligen Gruppen angegeben sind, steuern die jeweiligen LED-Streifen an. Die Pins, die zuerst genannt werden, sind immer für die oberen LED-Streifen. So ist z.B. bei Timer-Digits der Pin 2 für den obersten LED-Streifen, der Pin 4 für den zweit-obersten, etc.
LED‑Indexierung: Bei **Timer‑**, **Uhrzeit‑** und **Loading‑Strips** beginnt der Index 0 für die Strips **links** und zählt nach rechts (0 ist dann die 1. LED von links, 1 die zweite LED, etc.); bei den **Nachteil‑Strips** beginnt Index 0 **rechts** und zählt nach links.

### 2.2 LED‑Aufteilung der Zeit

Die fünf Strips eines Bereichs liegen übereinander und bilden damit eine Matrix aus **Zeilen** (Streifen) und **Spalten** (LED‑Index). Diese Matrix stellt jeweils eine 7‑Segment‑Anzeige dar. Die Strips sind so verkabelt, dass die folgenden LED‑Gruppen jeweils ein Zeit‑Digit ergeben (von links gezählt; beim Nachteilsausgleich liegt das Datenkabel rechts):

* **Timer:**
  1. 3 LED → Hunderter‑Minute
  2. 1 LED → Platzhalter (aus)
  3. 3 LED → Zehner‑Minute
  4. 1 LED → Platzhalter (aus)
  5. 3 LED → Einer‑Minute
  6. 1 LED → Platzhalter (aus)
  7. 1 LED → Doppelpunkt
  8. 1 LED → Platzhalter (aus)
  9. 3 LED → Zehner‑Sekunde
 10. 1 LED → Platzhalter (aus)
 11. 3 LED → Einer‑Sekunde

* **Uhrzeit:**
  1. 3 LED → Zehner‑Stunde
  2. 1 LED → Platzhalter (aus)
  3. 3 LED → Einer‑Stunde
  4. 1 LED → Platzhalter (aus)
  5. 1 LED → Doppelpunkt
  6. 1 LED → Platzhalter (aus)
  7. 3 LED → Zehner‑Minute
  8. 1 LED → Platzhalter (aus)
  9. 3 LED → Einer‑Minute

* **Nachteilsausgleich** (Ansteuerung von rechts):
  1. 3 LED → Hunderter‑Minute
  2. 1 LED → Platzhalter (aus)
  3. 3 LED → Zehner‑Minute
  4. 1 LED → Platzhalter (aus)
  5. 3 LED → Einer‑Minute

> *Kein MCP23017 mehr nötig – alle Segmente hängen direkt am ESP32.*

---

## 3 Systemarchitektur

### 3.1 Firmware‑Zustände

| State           | Beschreibung                                      |
| --------------- | ------------------------------------------------- |
| **SHOW_CLOCK** | Anzeige der aktuellen Uhrzeit (HH:MM)           |
| **COUNTDOWN**   | Laufender Prüfungs‑Countdown                      |
| **BONUS**       | Nachteilsausgleich läuft (übernimmt Rest‑Anzeige) |

Die State‑Maschine sorgt dafür, dass **pro Sekunde** exakt ein Frame gerendert wird; Dirty‑Flags verhindern unnötige LED‑Updates.

---

## 4 REST‑API (laut Firmware)

| Methode | Pfad               | Parameter                                 | Beschreibung                          |
| ------- | ------------------ | ----------------------------------------- | ------------------------------------- |
| **GET** | `/executeFunction` | `time` *(Sekunden)*, `bonus` *(Sekunden)* | Startet Countdown & Bonus‑Timer       |
| **GET** | `/resetTimer`      | –                                         | Stoppt Countdown, springt auf Uhrzeit |
| **GET** | `/led-post`        | –                                         | LED-Selbsttest (120 s)                |
| **GET** | `/`                | –                                         | Liefert SPA‑Frontend (index.html)     |

> *Aktuell existieren **nur** diese vier Endpunkte – bitte Frontend daran koppeln.*

---

## 5 Darstellungs‑Logik

### 5.1 Farben & Effekte

| Element         | Standardfarbe                                   | Effekt / Wechsel                                          |
| --------------- | ----------------------------------------------- | --------------------------------------------------------- |
| Timer‑Digits    | **Weiß** ➜ **Rot**                              | Farbwechsel bei ≥ **20 %** Restzeit                       |
| Loading Bar    | Grün ➜ Gelb (letzte 4 LED) ➜ Rot (letzte 2 LED) | Synchron schrumpfend, Farbabstufung                       |
| Nachteil‑Digits | Lila                                            | Bonuszeit; wandert nach oben, wenn Haupt‑Timer 0 erreicht |
| Uhrzeit‑Digits  | Cyan / Weiß                                     | Uhrzeit‑ / Status‑Anzeige                                 |

### 5.2 Schwellen (prozentual auf Gesamt‑Countdown)

| Bezeichner           | Wert     | Wirkung                  |
| -------------------- | -------- | ------------------------ |
| `WARN_THRESHOLD_PCT` | **20 %** | Ab hier Rot‑Darstellung  |
| `BLINK_LAST_PCT`     | **5 %**  | Letzte Phase blinkt 1 Hz |

> *Werte sind **fix** im Code hinterlegt und gelten für jede Prüfungsdauer.*

### 5.3 Nachteilsausgleich

*Fixe Bonus‑Stufen:* **10 % | 15 % | 20 % | 25 %** der eingestellten Prüfungs‑Dauer.  
Anzeige‑Ablauf: Bonus‑Timer startet parallel unten‑rechts ➜ bei Haupt‑Timer 0 wechselt Anzeige nach oben und zählt mit ↓.

---

## 6 Recovery‑Strategie

1. **State‑Snapshot** – `phase`, `tEnd`, `bonusSec` werden alle **5 s** in NVS gespeichert (schont Flash, genügt Genauigkeit).
2. **Cold‑Boot** – Beim Neustart wird Snapshot geladen; liegt `tEnd` noch in der Zukunft, zählt die Uhr **nahtlos weiter**.
3. **Zeitbasis** – RTC‑Uhr des ESP32; optional NTP‑Sync über Schul‑WLAN (nur wenn erreichbar).
4. **Watchdog** – Software‑Watchdog (8 s) resettet bei WLAN‑Lock‑up; Snapshot‑Restore sorgt für Weiterlauf.

---

## 7 Test‑ / Kalibrier‑Modus

*Ziel:* Schnelle Sichtprüfung aller 324 Pixel ohne spezial‑Sketch.

| Trigger                                  | Verhalten                                                                   |
| ---------------------------------------- | --------------------------------------------------------------------------- |
| **Boot‑Taste** ≥ 3 s halten              | Alle 16 Strips leuchten **Rot** für 30 s, anschließend automatischer Reboot |
| *(optional)* **HTTP GET** `/testPattern` | Wechselt Rot → Grün → Blau → Weiß (je 1 s) – *TODO implementieren*          |

---

## 8 Schriftart / Segment‑Mapping

Jede Ziffer wird jetzt aus einer 3×5‑Matrix gerendert. Die Zuordnung der
Pixel erfolgt über Konstanten in `font7seg.h`, aus denen `drawDigit()` die
7‑Segment‑Formen ableitet.

---

## 9 Offene Aufgaben (Roadmap)

### Firmware

- **[x] Segment‑Font:** LED‑Koordinaten für 7‑Segment‑Digits sind in `font7seg.h` definiert.
- **[ ] Prozent‑Schwellen:** Implementierung der Rot‑/Blink‑Logik basierend auf `WARN_THRESHOLD_PCT` & `BLINK_LAST_PCT`.
- **[ ] Bonus‑Stufen:** 10 / 15 / 20 / 25 % Berechnung + Anzeige synchronisieren.
- **[ ] Test‑Modus:** Rote‑LED‑Routine & `/testPattern`‑Endpoint ergänzen; 30 s Timeout & Reboot.
- **[ ] NVS‑Persistenz:** Snapshot alle 5 s; includiert `phase`, `tEnd`, `bonusSec`.
- **[ ] Watchdog:** Aktivieren & prüfen, dass Render‑Loop < 1 s bleibt.

### Frontend

- **[ ] Anpassung REST‑Calls** auf `/executeFunction` & `/resetTimer` (kein `/status` aktuell).
- **[ ] Feedback‑Overlay** bei Warn‑/Blink‑Phase (optional UI‑Feature).

### Dokumentation

- **[ ] Changelog pflegen** bei weiteren Firmware‑Commits.

---

## 10 Changelog

| Datum       | Version | Änderungen kurzgefasst                                                                         |
| ----------- | ------- | ---------------------------------------------------------------------------------------------- |
| 07 Jun 2025 | 0.4     | Pin‑Mapping angepasst (16 Strips), Expander gestrichen, Prozent‑Schwellen, Roadmap hinzugefügt |
| 07 Jun 2025 | 0.3     | Loading‑Bar (40 + 39), Pixel‑Summe 324, erste Tabellenkorrektur                                |
| 06 Jun 2025 | 0.2     | README‑Refactor, Schwellen + Recovery‑Skizze                                                   |
| 05 Jun 2025 | 0.1     | Initialer Entwurf                                                                              |

---

## 11 Lizenz

Dieses Projekt steht unter der **MIT‑Lizenz** (siehe `LICENSE`). Markennamen und Logos sind Eigentum ihrer jeweiligen Inhaber.

# Giessanlage (ESP32)

ESP32-basiertes Bewaesserungssystem mit Weboberflaeche, Zeitplaenen, OTA-Updates und Netzwerk-Logausgabe.

## Hauptfunktionen

- 3 Bewaesserungsplaene (aktiv/inaktiv, Uhrzeit, Dauer)
- Manuelles Starten/Stoppen ueber Weboberflaeche
- Persistente Speicherung der Plaene im EEPROM
- WLAN-Setup ueber WiFiManager Captive Portal
- mDNS unter `giessanlage.local`
- OTA-Firmware-Update via `espota`
- Remote-Logs via TCP (Telnet-Port 23) fuer kabellosen Monitor
- WLAN-Status in UI: Signalstaerke und aktuelle SSID

## Projektstruktur

- `src/main.cpp`: komplette Firmware (Webserver, Zeitplaene, WLAN, OTA)
- `platformio.ini`: Build-, Upload- und Monitor-Konfiguration
- `.vscode/tasks.json`: VS Code Tasks fuer OTA Upload und OTA Monitor

## Hardware / Pins

- Relais: GPIO 25 (`RELAY_PIN`)
- Status-LED: GPIO 2 (`LED_PIN`)

## Webzugriff

- URL: `http://giessanlage.local` (oder IP aus Serial-Log)
- HTTP Basic Auth (Default):
  - Benutzer: `admin`
  - Passwort: `schnitzel`

## API-Endpunkte

Alle Endpunkte sind per Basic Auth geschuetzt.

- `GET /api/time` -> Uhrzeit + Datum
- `GET /api/schedules` -> alle Plaene
- `POST /api/schedule` -> Plan speichern (`index`, `enabled`, `time`, `duration`)
- `GET /api/water/start` -> manuelles Giessen starten
- `GET /api/water/stop` -> manuelles Giessen stoppen
- `GET /api/wifi` -> WLAN-Daten (`rssi`, `ssid`)
- `GET /api/status` -> aktueller Status (`watering`, `activeSchedule`)

## OTA Update

### Voraussetzungen

- ESP32 ist im WLAN
- Hostname ist erreichbar (`giessanlage.local`)

### Relevante `platformio.ini`-Eintraege

- `upload_protocol = espota`
- `upload_port = giessanlage.local`
- `monitor_port = socket://giessanlage.local:23`

### Upload per CLI

```bash
~/.platformio/penv/bin/platformio run --environment esp32 --target upload
```

## OTA-Logs (kabellos)

### Monitor per CLI

```bash
~/.platformio/penv/bin/platformio device monitor --environment esp32
```

Der Monitor verbindet sich auf `socket://giessanlage.local:23`.

## VS Code Tasks

- `Upload OTA (giessanlage.local)`
- `Monitor OTA Logs (giessanlage.local)`

Start:

- `Cmd+Shift+P` -> `Run Task`
- oder `Cmd+Shift+B` fuer den Default-Build-Task (OTA Upload)

## WLAN-Setup (WiFiManager)

Wenn keine WLAN-Verbindung vorhanden ist, startet ein Konfigurations-AP:

- SSID: `Giessanlage-Setup`
- Passwort: `schnitzel`

Darueber kann das Ziel-WLAN gesetzt werden.

## NTP / Zeit

- NTP Server: `pool.ntp.org`
- Zeitzone: GMT+1, DST +1h (aktueller Code-Stand)

## Sicherheit (wichtig)

Im aktuellen Stand sind einige Zugangsdaten im Code hart kodiert (z. B. Web-Login und AP-Passwort).
Fuer produktiven Betrieb sollten diese geaendert und nicht in einem oeffentlichen Repo gespeichert werden.

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <EEPROM.h>

const char* ssid = "Vodafone-B008";
const char* password = "7mMTTEdBEquBUHUd";
const char* deviceHostname = "giessanlage";
const char* authUser = "admin";
const char* authPass = "schnitzel";

#define RELAY_PIN 25
#define LED_PIN 2

WebServer server(80);
WiFiServer telnetServer(23);
WiFiClient telnetClient;

void rlog(const String& msg) {
  Serial.println(msg);
  if (telnetClient && telnetClient.connected()) {
    telnetClient.println(msg);
  }
}

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 1 * 3600;
const int daylightOffset_sec = 1 * 3600;

struct WateringSchedule {
  bool enabled;
  uint8_t hour;
  uint8_t minute;
  uint16_t duration;
};

#define SCHEDULE_COUNT 3
#define EEPROM_SIZE 512
#define INIT_FLAG_ADDR 0
#define INIT_FLAG_VALUE 42

WateringSchedule schedules[SCHEDULE_COUNT];

unsigned long lastWateringCheck = 0;
bool isWatering = false;
unsigned long wateringStartTime = 0;
uint16_t wateringDuration = 0;
int lastActivatedSchedule = -1;
int lastMinuteChecked = -1;

void loadSchedules() {
  EEPROM.begin(EEPROM_SIZE);
  
  // Prueffe ob EEPROM bereits initialisiert wurde
  uint8_t initFlag = EEPROM.read(INIT_FLAG_ADDR);
  
  if (initFlag != INIT_FLAG_VALUE) {
    // Erster Start - setze Defaults
    Serial.println("[SYSTEM] Erster Start - Setze Standardwerte");
    for (int i = 0; i < SCHEDULE_COUNT; i++) {
      schedules[i].enabled = false;
      schedules[i].hour = (8 + i * 4) % 24;
      schedules[i].minute = 0;
      schedules[i].duration = 30;
      
      int addr = i * sizeof(WateringSchedule) + 10; // Offset um Init-Flag nicht zu ueberschreiben
      EEPROM.put(addr, schedules[i]);
    }
    
    // Schreibe Init-Flag
    EEPROM.write(INIT_FLAG_ADDR, INIT_FLAG_VALUE);
    EEPROM.commit();
    Serial.println("[SYSTEM] EEPROM initialisiert");
  } else {
    // EEPROM bereits initialisiert - lade Werte
    for (int i = 0; i < SCHEDULE_COUNT; i++) {
      int addr = i * sizeof(WateringSchedule) + 10;
      EEPROM.get(addr, schedules[i]);
    }
    Serial.println("[SYSTEM] Giessplaene aus EEPROM geladen");
  }
  
  EEPROM.end();
  
  // Debug-Ausgabe
  for (int i = 0; i < SCHEDULE_COUNT; i++) {
    Serial.print("[EEPROM] Plan ");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.print(schedules[i].enabled ? "AN" : "AUS");
    Serial.print(" | ");
    Serial.print(schedules[i].hour);
    Serial.print(":");
    Serial.print(schedules[i].minute);
    Serial.print(" | ");
    Serial.print(schedules[i].duration);
    Serial.println("s");
  }
}

void saveSchedules() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < SCHEDULE_COUNT; i++) {
    int addr = i * sizeof(WateringSchedule) + 10; // Offset um Init-Flag nicht zu ueberschreiben
    EEPROM.put(addr, schedules[i]);
  }
  EEPROM.commit();
  EEPROM.end();
  Serial.println("[SYSTEM] Giessplaene gespeichert");
}

bool isAuthenticated() {
  if (server.authenticate(authUser, authPass)) {
    return true;
  }
  server.requestAuthentication(BASIC_AUTH, "Giessanlage", "Login erforderlich");
  return false;
}

void setupWiFi() {
  Serial.println("\n[WiFi] Starte WiFi-Konfiguration...");
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(deviceHostname);

  WiFiManager wm;
  wm.setHostname(deviceHostname);
  wm.setConfigPortalTimeout(180);
  wm.setAPCallback([](WiFiManager* manager) {
    Serial.println("[WiFi] AP gestartet");
    Serial.print("[WiFi] AP SSID: ");
    Serial.println(manager->getConfigPortalSSID());
    Serial.print("[WiFi] Portal-IP: ");
    Serial.println(WiFi.softAPIP());
  });

  bool connected = wm.autoConnect("Giessanlage-Setup", "schnitzel");
  if (!connected) {
    Serial.println("[WiFi] Keine Verbindung - Setup-Portal beendet");
    Serial.println("[SYSTEM] Laufbetrieb ohne WLAN");
    return;
  }

  Serial.println("[WiFi] WLAN verbunden");
  Serial.print("[WiFi] Lokale IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("[WiFi] Hostname: ");
  Serial.println(deviceHostname);

  if (MDNS.begin(deviceHostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[mDNS] gestartet");
    Serial.println("[mDNS] URL: http://giessanlage.local");
  } else {
    Serial.println("[mDNS] FEHLER: mDNS konnte nicht gestartet werden!");
  }

  ArduinoOTA.setHostname(deviceHostname);
  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Update gestartet");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Update abgeschlossen");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Fortschritt: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Fehler [%u]: ", error);
    if      (error == OTA_AUTH_ERROR)    Serial.println("Authentifizierung fehlgeschlagen");
    else if (error == OTA_BEGIN_ERROR)   Serial.println("Start fehlgeschlagen");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Verbindung fehlgeschlagen");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Empfang fehlgeschlagen");
    else if (error == OTA_END_ERROR)     Serial.println("Abschluss fehlgeschlagen");
  });
  ArduinoOTA.begin();
  Serial.print("[OTA] Bereit | Hostname: ");
  Serial.println(deviceHostname);
  Serial.print("[OTA] IP: ");
  Serial.println(WiFi.localIP());

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("[NTP] Warte auf Zeit-Sync...");
  time_t now = time(nullptr);
  int syncAttempts = 0;
  while (now < 24 * 3600 && syncAttempts < 20) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    syncAttempts++;
  }
  if (now >= 24 * 3600) {
    Serial.println("\n[NTP] Zeit synchronisiert!");
  } else {
    Serial.println("\n[NTP] Keine Zeit-Sync, System laeuft weiter");
  }
}

String getFormattedTime() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char buffer[20];
  sprintf(buffer, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
  return String(buffer);
}

String getFormattedDate() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  char buffer[20];
  sprintf(buffer, "%02d.%02d.%04d", timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900);
  return String(buffer);
}

int getCurrentHour() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  return timeinfo->tm_hour;
}

int getCurrentMinute() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  return timeinfo->tm_min;
}

void startWatering(uint16_t durationSeconds) {
  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(LED_PIN, HIGH);
  isWatering = true;
  wateringStartTime = millis();
  wateringDuration = durationSeconds;
  Serial.print("[WATERING] Giessen gestartet fuer ");
  Serial.print(durationSeconds);
  Serial.println(" Sekunden");
}

void stopWatering() {
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  isWatering = false;
  Serial.println("[WATERING] Giessen gestoppt");
}

void checkWateringSchedule() {
  unsigned long now = millis();
  
  // Check nur alle 5 Sekunden
  if (now - lastWateringCheck < 5000) {
    return;
  }
  lastWateringCheck = now;
  
  int currentHour = getCurrentHour();
  int currentMinute = getCurrentMinute();
  
  // SCHRITT 0: Wenn Minute sich ändert, Reset des lastActivatedSchedule
  if (currentMinute != lastMinuteChecked) {
    lastMinuteChecked = currentMinute;
    lastActivatedSchedule = -1; // Erlaubt neuen Start in dieser Minute
    Serial.print("[SCHEDULE] Neue Minute: ");
    Serial.print(currentHour);
    Serial.print(":");
    Serial.println(currentMinute);
  }
  
  // SCHRITT 1: Prüfe ob aktuell gießendes Gerät beendet werden soll
  if (isWatering) {
    unsigned long elapsedSeconds = (millis() - wateringStartTime) / 1000;
    if (elapsedSeconds >= wateringDuration) {
      stopWatering();
      Serial.print("[SCHEDULE] Gießen beendet nach ");
      Serial.print(elapsedSeconds);
      Serial.println(" Sekunden");
      // lastActivatedSchedule NICHT auf -1 setzen - das passiert nur bei Minutenwechsel
      return;
    }
  }
  
  // SCHRITT 2: Prüfe ob neuer Gießplan starten soll (nur wenn nicht gerade gießen läuft)
  if (!isWatering) {
    for (int i = 0; i < SCHEDULE_COUNT; i++) {
      if (schedules[i].enabled && 
          schedules[i].hour == currentHour && 
          schedules[i].minute == currentMinute &&
          lastActivatedSchedule != i) {  // Verhindere doppelten Start in dieser Minute
        
        Serial.print("[SCHEDULE] Giessplan ");
        Serial.print(i + 1);
        Serial.print(" aktiviert um ");
        Serial.print(currentHour);
        Serial.print(":");
        Serial.println(currentMinute);
        
        lastActivatedSchedule = i;
        startWatering(schedules[i].duration);
        return;
      }
    }
  }
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>Giessanlage</title><style>* { margin: 0; padding: 0; box-sizing: border-box; }body { font-family: Arial, sans-serif; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; display: flex; justify-content: center; align-items: center; padding: 8px; }.container { background: white; border-radius: 15px; box-shadow: 0 20px 60px rgba(0,0,0,0.3); padding: 15px; max-width: 500px; width: 100%; }h1 { text-align: center; color: #333; margin-bottom: 8px; font-size: 1.5em; }.header-row { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; background: #f0f4ff; padding: 10px; border-radius: 8px; }.time-display { font-size: 1.1em; color: #667eea; font-weight: bold; flex: 1; }.wifi-inline { display: flex; align-items: center; gap: 4px; font-size: 0.75em; color: #888; }.wifi-bar { display: inline-block; width: 4px; height: 10px; border-radius: 2px; background: #ccc; vertical-align: middle; }.wifi-bar.active { background: #667eea; }.schedule-box { background: #f9f9f9; border: 1px solid #e0e0e0; border-radius: 8px; padding: 10px; margin-bottom: 10px; }.schedule-label { font-size: 0.9em; font-weight: bold; margin-bottom: 6px; display: flex; align-items: center; }.input-group { display: flex; gap: 6px; align-items: center; margin-top: 6px; }input[type='checkbox'] { width: 18px; height: 18px; cursor: pointer; }input[type='time'], input[type='number'] { padding: 6px; border: 1px solid #ddd; border-radius: 4px; font-size: 0.85em; }input[type='time'] { flex: 1; }input[type='number'] { width: 60px; }.btn { padding: 8px 12px; border: none; border-radius: 6px; cursor: pointer; font-weight: bold; font-size: 0.85em; }.btn-save { background: #667eea; color: white; width: 100%; margin-top: 6px; }.btn-save:hover { background: #5568d3; }.btn-water { background: #4caf50; color: white; flex: 1; }.btn-water:hover { background: #45a049; }.btn-stop { background: #f44336; color: white; flex: 1; }.btn-stop:hover { background: #da190b; }.button-group { display: flex; gap: 8px; margin-top: 12px; }.status { text-align: center; padding: 8px; margin-top: 10px; border-radius: 6px; font-weight: bold; font-size: 0.9em; }.status.active { background: #c8e6c9; color: #2e7d32; }.status.inactive { background: #ffcccc; color: #c62828; }.led { display: inline-block; width: 12px; height: 12px; border-radius: 50%; background: #ccc; margin-right: 6px; vertical-align: middle; box-shadow: 0 0 3px rgba(0,0,0,0.3); }.led.on { background: #4caf50; box-shadow: 0 0 8px #4caf50; }</style></head><body><div class='container'><h1>🌸 Giessanlage 💧</h1><div class='header-row'><div class='time-display' id='timeDisplay'>--:--:--</div><div class='wifi-inline' id='wifiInfo'><div class='wifi-bar' id='wb1'></div><div class='wifi-bar' id='wb2'></div><div class='wifi-bar' id='wb3'></div><div class='wifi-bar' id='wb4'></div><span id='wifiSSID' style='margin-left:5px;'></span></div></div><div id='schedules'></div><div class='button-group'><button class='btn btn-water' onclick='startManualWatering()'>Start</button><button class='btn btn-stop' onclick='stopWatering()'>Stop</button></div><div class='status inactive' id='status'>Bereit</div></div><script>function updateTime() { fetch('/api/time').then(r=>r.text()).then(t=>document.getElementById('timeDisplay').textContent=t); }function loadSchedules() { fetch('/api/schedules').then(r=>r.json()).then(s=>{ let h=''; s.forEach((x,i)=>{ h+='<div class=\"schedule-box\"><div class=\"schedule-label\"><span class=\"led\" id=\"led'+i+'\"></span>Plan '+(i+1)+'</div><div class=\"input-group\">'; h+='<input type=\"checkbox\" id=\"e'+i+'\" '+(x.enabled?'checked':'')+'> '; h+='<input type=\"time\" id=\"t'+i+'\" value=\"'+String(x.hour).padStart(2,'0')+':'+String(x.minute).padStart(2,'0')+'\"> '; h+='<input type=\"number\" id=\"d'+i+'\" min=\"1\" max=\"300\" value=\"'+x.duration+'\" title=\"Sekunden\"> '; h+='</div><button class=\"btn btn-save\" onclick=\"saveSchedule('+i+')\">Speichern</button></div>'; }); document.getElementById('schedules').innerHTML=h; }); }function saveSchedule(i) { const e=document.getElementById('e'+i).checked; const t=document.getElementById('t'+i).value; const d=parseInt(document.getElementById('d'+i).value); fetch('/api/schedule',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:i,enabled:e,time:t,duration:d})}).then(()=>{ alert('Plan '+(i+1)+' gespeichert!'); loadSchedules(); }); }function startManualWatering() { fetch('/api/water/start').then(()=>{ document.getElementById('status').textContent='Giessen aktiv!'; document.getElementById('status').className='status active'; }); }function stopWatering() { fetch('/api/water/stop').then(()=>{ document.getElementById('status').textContent='Giessen gestoppt'; document.getElementById('status').className='status inactive'; }); }function updateWifi() { fetch('/api/wifi').then(r=>r.json()).then(d=>{ const rssi=d.rssi; let bars=0; if(rssi>=-50){bars=4;}else if(rssi>=-60){bars=3;}else if(rssi>=-70){bars=2;}else{bars=1;} for(let i=1;i<=4;i++){document.getElementById('wb'+i).className='wifi-bar'+(i<=bars?' active':'');} const ssidEl=document.getElementById('wifiSSID'); if(ssidEl&&d.ssid) ssidEl.textContent=d.ssid; }); } function updateStatus() { fetch('/api/status').then(r=>r.json()).then(d=>{ for(let i=0;i<4;i++){ const el=document.getElementById('led'+i); if(el) el.className='led'+(d.watering&&d.activeSchedule===i?' on':''); } if(d.watering){ document.getElementById('status').textContent='Giessen aktiv!'; document.getElementById('status').className='status active'; } else { document.getElementById('status').textContent='Bereit'; document.getElementById('status').className='status inactive'; } }); } updateTime(); loadSchedules(); updateWifi(); updateStatus(); setInterval(updateTime,1000); setInterval(updateWifi,10000); setInterval(updateStatus,2000);</script></body></html>";
  server.send(200, "text/html", html);
}

void handleGetTime() {
  String time = getFormattedTime() + " | " + getFormattedDate();
  server.send(200, "text/plain", time);
}

void handleGetSchedules() {
  String json = "[";
  for (int i = 0; i < SCHEDULE_COUNT; i++) {
    if (i > 0) json += ",";
    json += "{\"enabled\":" + String(schedules[i].enabled ? "true" : "false") + ",\"hour\":" + String(schedules[i].hour) + ",\"minute\":" + String(schedules[i].minute) + ",\"duration\":" + String(schedules[i].duration) + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handlePostSchedule() {
  if (server.hasArg("plain")) {
    String json = server.arg("plain");
    
    // Verbose Debug
    Serial.print("[API] Received JSON: ");
    Serial.println(json);
    
    // Parse JSON - CORRECTED
    int indexPos = json.indexOf("\"index\":");
    // Suche nach der Ziffer nach "index":
    int index = 0;
    for (int i = indexPos + 8; i < json.length(); i++) {
      char c = json.charAt(i);
      if (c >= '0' && c <= '2') {
        index = c - '0';
        break;
      }
    }
    
    bool enabledValue = json.indexOf("\"enabled\":true") != -1;
    
    int timePos = json.indexOf("\"time\":\"");
    String timeStr = json.substring(timePos + 8, timePos + 13);
    
    Serial.print("[PARSE] TimeStr: '");
    Serial.print(timeStr);
    Serial.print("' Len: ");
    Serial.println(timeStr.length());
    
    int hourVal = (timeStr[0] - '0') * 10 + (timeStr[1] - '0');
    int minuteVal = (timeStr[3] - '0') * 10 + (timeStr[4] - '0');
    
    int durationPos = json.indexOf("\"duration\":");
    int durationEnd = json.indexOf("}", durationPos);
    String durationStr = json.substring(durationPos + 11, durationEnd);
    int durationVal = durationStr.toInt();
    
    Serial.print("[PARSE] Index: ");
    Serial.print(index);
    Serial.print(" | Hour: ");
    Serial.print(hourVal);
    Serial.print(" | Minute: ");
    Serial.print(minuteVal);
    Serial.print(" | Duration: ");
    Serial.print(durationVal);
    Serial.print(" | Enabled: ");
    Serial.println(enabledValue);
    
    // Validierung mit Detail-Fehler
    if (index < 0 || index >= SCHEDULE_COUNT) {
      Serial.println("[API] FEHLER: Index ungueltig");
      server.send(400, "text/plain", "INDEX_INVALID");
      return;
    }
    if (hourVal > 23) {
      Serial.print("[API] FEHLER: Hour zu gross: ");
      Serial.println(hourVal);
      server.send(400, "text/plain", "HOUR_INVALID");
      return;
    }
    if (minuteVal > 59) {
      Serial.print("[API] FEHLER: Minute zu gross: ");
      Serial.println(minuteVal);
      server.send(400, "text/plain", "MINUTE_INVALID");
      return;
    }
    if (durationVal <= 0 || durationVal > 300) {
      Serial.print("[API] FEHLER: Duration ungueltig: ");
      Serial.println(durationVal);
      server.send(400, "text/plain", "DURATION_INVALID");
      return;
    }
    
    // Alle Checks bestanden - speichere
    schedules[index].enabled = enabledValue;
    schedules[index].hour = hourVal;
    schedules[index].minute = minuteVal;
    schedules[index].duration = durationVal;
    
    saveSchedules();
    
    Serial.print("[API] ERFOLG: Plan ");
    Serial.print(index + 1);
    Serial.print(" gespeichert: ");
    Serial.print(hourVal);
    Serial.print(":");
    Serial.print(minuteVal);
    Serial.print(" | ");
    Serial.print(durationVal);
    Serial.print("s | Enabled: ");
    Serial.println(enabledValue);
    
    server.send(200, "text/plain", "OK");
  }
}

void handleWaterStart() {
  startWatering(60);
  server.send(200, "text/plain", "Giessen gestartet");
}

void handleWaterStop() {
  stopWatering();
  server.send(200, "text/plain", "Giessen gestoppt");
}

void handleGetWifi() {
  int rssi = WiFi.RSSI();
  String ssidStr = WiFi.SSID();
  String json = "{\"rssi\":" + String(rssi) + ",\"ssid\":\"" + ssidStr + "\"}";
  server.send(200, "application/json", json);
}

void handleGetStatus() {
  String json = "{\"watering\":" + String(isWatering ? "true" : "false") + ",\"activeSchedule\":" + String(lastActivatedSchedule) + "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n[SYSTEM] Giessanlage startet...");
  
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  Serial.print("[GPIO] Relais-Pin: ");
  Serial.println(RELAY_PIN);
  
  loadSchedules();
  setupWiFi();
  
  server.on("/", []() {
    if (!isAuthenticated()) return;
    handleRoot();
  });
  server.on("/api/time", []() {
    if (!isAuthenticated()) return;
    handleGetTime();
  });
  server.on("/api/schedules", []() {
    if (!isAuthenticated()) return;
    handleGetSchedules();
  });
  server.on("/api/schedule", HTTP_POST, []() {
    if (!isAuthenticated()) return;
    handlePostSchedule();
  });
  server.on("/api/water/start", []() {
    if (!isAuthenticated()) return;
    handleWaterStart();
  });
  server.on("/api/water/stop", []() {
    if (!isAuthenticated()) return;
    handleWaterStop();
  });
  server.on("/api/wifi", []() {
    if (!isAuthenticated()) return;
    handleGetWifi();
  });
  server.on("/api/status", []() {
    if (!isAuthenticated()) return;
    handleGetStatus();
  });
  
  server.begin();
  Serial.print("[WEBSERVER] Gestartet auf http://");
  Serial.println(WiFi.localIP());

  telnetServer.begin();
  telnetServer.setNoDelay(true);
  Serial.println("[TELNET] Bereit auf Port 23");
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  checkWateringSchedule();

  // Telnet: accept new client or drop disconnected one
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      telnetClient = telnetServer.accept();
      telnetClient.println("[TELNET] Verbunden mit Giessanlage");
      Serial.println("[TELNET] Client verbunden");
    } else {
      telnetServer.accept().stop(); // reject second client
    }
  }

  delay(10);
}

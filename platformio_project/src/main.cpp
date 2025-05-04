#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <ESP32Time.h>

// Kommunikationsbibliotheken
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// Eigene Module
#include "wifi_manager.h"
#include "mqtt_communication.h"
#include "rest_api.h"
#include "display.h"

// LVGL Puffergrößen
#define SCREEN_WIDTH  800
#define SCREEN_HEIGHT 480

// GPIO Pins für externe Komponenten
#define RED_PIN    45
#define GREEN_PIN  46
#define BLUE_PIN   47
#define MOTOR_PIN  48
#define SENSOR_PIN 49

// Programmdefinitionen (in Sekunden für einfacheres Testen)
// In der Produktionsversion auf Tage umstellen
#define PROGRAM_1_DURATION (7  * 24 * 60 * 60) // 7 Tage
#define PROGRAM_2_DURATION (14 * 24 * 60 * 60) // 14 Tage
#define PROGRAM_3_DURATION (21 * 24 * 60 * 60) // 21 Tage

// LVGL-Sprites und Widgets
static lv_obj_t *mainScreen;
static lv_obj_t *programScreen;
static lv_obj_t *settingsScreen;
static lv_obj_t *runningScreen;
static lv_obj_t *completedScreen;
static lv_obj_t *errorScreen;

// Anzeigen für den aktuellen Programm-Status
static lv_obj_t *programLabel;
static lv_obj_t *timeLabel;
static lv_obj_t *progressBar;
static lv_obj_t *statusLabel;

// Globale Variablen für Programmsteuerung
enum ProgramState {
  IDLE,
  RUNNING,
  COMPLETED,
  ERROR
};

struct SystemState {
  ProgramState state;
  int activeProgram;      // 1, 2, 3, oder 4 (individuell)
  uint32_t programDuration;  // Programmdauer in Sekunden
  uint32_t startTime;     // Zeitpunkt des Programmstarts
  uint32_t customDays;    // Für individuelles Programm
  bool tankLevelOk;       // Tankfüllstand OK?
  bool motorActive;       // Motor läuft?
  bool remoteControlEnabled; // Fernsteuerung aktiviert?
  String deviceId;        // Eindeutige Geräte-ID
};

SystemState systemState;
ESP32Time rtc;

// Kommunikationsmodule
WiFiManager wifiManager;
MQTTCommunication mqttClient;
RESTAPI restApi;

// Display-Treiber und LVGL-Puffer
TFT_eSPI tft = TFT_eSPI();
static lv_disp_draw_buf_t drawBuffer;
static lv_color_t buf1[SCREEN_WIDTH * 10];
static lv_color_t buf2[SCREEN_WIDTH * 10];
static lv_disp_drv_t dispDriver;
static lv_indev_drv_t indevDriver;

// Timer für LVGL und Programmabfragen
hw_timer_t *lvglTimer = NULL;
hw_timer_t *programTimer = NULL;

// Funktionsprototypen
void createMainScreen();
void createProgramScreen();
void createSettingsScreen();
void createRunningScreen();
void createCompletedScreen();
void createErrorScreen();
void updateRunningScreen();
void startProgram(int programIndex);
void stopProgram();
String formatTime(uint32_t seconds);
uint32_t getRemainingTime();
int getProgressPercent();
void setLedStatus(ProgramState state);
void checkTankLevel();

// MQTT-Callback-Funktion für Fernsteuerungsbefehle
void onMqttCommand(const String &command, const JsonObject &payload) {
  Serial.print("MQTT-Befehl empfangen: ");
  Serial.println(command);
  
  if (command == "start_program") {
    if (payload.containsKey("program")) {
      int programIndex = payload["program"].as<int>();
      startProgram(programIndex);
      
      // Bestätigung senden
      DynamicJsonDocument response(128);
      response["success"] = true;
      response["program"] = programIndex;
      mqttClient.publishStatus("program_started");
    }
  }
  else if (command == "stop_program") {
    stopProgram();
    
    // Bestätigung senden
    DynamicJsonDocument response(128);
    response["success"] = true;
    mqttClient.publishStatus("program_stopped");
  }
  else if (command == "get_status") {
    // Detaillierten Status senden
    DynamicJsonDocument statusDoc(256);
    statusDoc["state"] = (int)systemState.state;
    statusDoc["program"] = systemState.activeProgram;
    statusDoc["remaining_time"] = getRemainingTime();
    statusDoc["progress"] = getProgressPercent();
    statusDoc["tank_level_ok"] = systemState.tankLevelOk;
    
    mqttClient.publishDetailedStatus("status_update", statusDoc.as<JsonObject>());
  }
}

// Initialisiert die WiFi-Verbindung
void initWiFi() {
  Serial.println("Initialisiere WiFi-Verbindung...");
  
  // Geräte-ID aus MAC-Adresse erstellen
  systemState.deviceId = "desinfektion_" + String((uint32_t)(ESP.getEfuseMac() >> 32), HEX);
  Serial.print("Geräte-ID: ");
  Serial.println(systemState.deviceId);
  
  // WiFi-Manager initialisieren
  wifiManager.setConnectionCallback([](bool connected) {
    if (connected) {
      Serial.println("WiFi verbunden!");
      
      // MQTT starten, wenn WiFi verbunden ist
      mqttClient.begin();
      
      // REST API starten
      setupRestApi();
    } else {
      Serial.println("WiFi-Verbindung verloren!");
    }
  });
  
  wifiManager.begin();
}

// Initialisiert die REST API
void setupRestApi() {
  Serial.println("Initialisiere REST API...");
  
  // API-Endpunkte registrieren
  
  // Status-Endpunkt
  restApi.registerEndpoint("/api/status", "GET", [](WebServer &server, JsonDocument &doc) {
    DynamicJsonDocument response(256);
    response["state"] = (int)systemState.state;
    response["program"] = systemState.activeProgram;
    response["remaining_time"] = getRemainingTime();
    response["progress"] = getProgressPercent();
    response["tank_level_ok"] = systemState.tankLevelOk;
    response["device_id"] = systemState.deviceId;
    
    String responseStr;
    serializeJson(response, responseStr);
    server.send(200, "application/json", responseStr);
  });
  
  // Programm-Start-Endpunkt
  restApi.registerEndpoint("/api/program/start", "POST", [](WebServer &server, JsonDocument &doc) {
    if (doc.containsKey("program")) {
      int programIndex = doc["program"].as<int>();
      
      if (programIndex >= 1 && programIndex <= 4) {
        startProgram(programIndex);
        
        DynamicJsonDocument response(128);
        response["success"] = true;
        response["program"] = programIndex;
        
        String responseStr;
        serializeJson(response, responseStr);
        server.send(200, "application/json", responseStr);
      } else {
        server.send(400, "application/json", "{\"error\":\"Ungültiger Programmindex\"}");
      }
    } else {
      server.send(400, "application/json", "{\"error\":\"Programmindex fehlt\"}");
    }
  });
  
  // Programm-Stop-Endpunkt
  restApi.registerEndpoint("/api/program/stop", "POST", [](WebServer &server, JsonDocument &doc) {
    stopProgram();
    
    DynamicJsonDocument response(128);
    response["success"] = true;
    
    String responseStr;
    serializeJson(response, responseStr);
    server.send(200, "application/json", responseStr);
  });
  
  // Individuelle-Programmdauer-Endpunkt
  restApi.registerEndpoint("/api/program/custom_days", "POST", [](WebServer &server, JsonDocument &doc) {
    if (doc.containsKey("days")) {
      int days = doc["days"].as<int>();
      
      if (days >= 1 && days <= 99) {
        systemState.customDays = days;
        
        DynamicJsonDocument response(128);
        response["success"] = true;
        response["days"] = days;
        
        String responseStr;
        serializeJson(response, responseStr);
        server.send(200, "application/json", responseStr);
      } else {
        server.send(400, "application/json", "{\"error\":\"Ungültige Anzahl an Tagen\"}");
      }
    } else {
      server.send(400, "application/json", "{\"error\":\"Tagesanzahl fehlt\"}");
    }
  });
  
  // API starten
  restApi.begin();
}

// LVGL Display- und Flush-Funktionen
void lvglFlushCb(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(disp);
}

// Touchscreen-Lesefunction für LVGL
void touchpadReadCb(lv_indev_drv_t *indev_driver, lv_indev_data_t *data) {
  uint16_t touchX, touchY;
  bool touched = tft.getTouch(&touchX, &touchY);

  if (touched) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = touchX;
    data->point.y = touchY;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

// Timer-Interrupt für LVGL-Ticks
void IRAM_ATTR onLvglTimer() {
  lv_tick_inc(5); // LVGL Tick alle 5ms
}

// Timer für Programm- und Sensor-Überwachung
void IRAM_ATTR onProgramTimer() {
  if (systemState.state == RUNNING) {
    uint32_t elapsedTime = rtc.getEpoch() - systemState.startTime;
    
    // Programm beendet?
    if (systemState.programDuration > 0 && elapsedTime >= systemState.programDuration) {
      systemState.state = COMPLETED;
      systemState.motorActive = false;
      digitalWrite(MOTOR_PIN, LOW);
      setLedStatus(COMPLETED);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Desinfektionseinheit mit LVGL und Remote-Steuerung startet...");

  // GPIO-Setup
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);
  pinMode(SENSOR_PIN, INPUT);

  // Initialisierung der Status-LEDs
  digitalWrite(RED_PIN, LOW);
  digitalWrite(GREEN_PIN, HIGH);  // Grün im Standby
  digitalWrite(BLUE_PIN, LOW);
  digitalWrite(MOTOR_PIN, LOW);   // Motor aus

  // System-Status initialisieren
  systemState.state = IDLE;
  systemState.activeProgram = 2;  // Standard: 14-Tage-Programm
  systemState.programDuration = PROGRAM_2_DURATION;
  systemState.customDays = 7;     // Standardwert für individuelles Programm
  systemState.tankLevelOk = true;
  systemState.motorActive = false;
  systemState.remoteControlEnabled = true; // Remote-Steuerung standardmäßig aktiviert

  // LVGL initialisieren
  lv_init();

  // TFT-Display initialisieren
  tft.begin();
  tft.setRotation(1); // Landscape
  tft.fillScreen(TFT_BLACK);

  // Touchscreen kalibrieren - Werte anpassen, je nach Display
  uint16_t calData[5] = {275, 3620, 264, 3532, 1};
  tft.setTouch(calData);

  // LVGL-Displaytreiber initialisieren
  lv_disp_draw_buf_init(&drawBuffer, buf1, buf2, SCREEN_WIDTH * 10);
  lv_disp_drv_init(&dispDriver);
  dispDriver.hor_res = SCREEN_WIDTH;
  dispDriver.ver_res = SCREEN_HEIGHT;
  dispDriver.flush_cb = lvglFlushCb;
  dispDriver.draw_buf = &drawBuffer;
  lv_disp_drv_register(&dispDriver);

  // LVGL-Touchscreen-Treiber initialisieren
  lv_indev_drv_init(&indevDriver);
  indevDriver.type = LV_INDEV_TYPE_POINTER;
  indevDriver.read_cb = touchpadReadCb;
  lv_indev_drv_register(&indevDriver);

  // Timer für LVGL initialisieren
  lvglTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(lvglTimer, &onLvglTimer, true);
  timerAlarmWrite(lvglTimer, 5000, true); // 5ms
  timerAlarmEnable(lvglTimer);

  // Timer für Programmüberwachung
  programTimer = timerBegin(1, 80, true);
  timerAttachInterrupt(programTimer, &onProgramTimer, true);
  timerAlarmWrite(programTimer, 1000000, true); // 1 Sekunde
  timerAlarmEnable(programTimer);

  // MQTT-Callback für Fernsteuerungsbefehle registrieren
  mqttClient.setCommandCallback(onMqttCommand);

  // WiFi und Remote-Steuerung initialisieren
  initWiFi();

  // GUI erstellen
  createMainScreen();
  createProgramScreen();
  createSettingsScreen();
  createRunningScreen();
  createCompletedScreen();
  createErrorScreen();

  // Starte mit dem Hauptbildschirm
  lv_scr_load(mainScreen);
  
  Serial.println("Initialisierung abgeschlossen!");
}

void loop() {
  lv_timer_handler(); // LVGL-Tasks ausführen
  
  // Sensor-Check
  static uint32_t lastSensorCheck = 0;
  if (millis() - lastSensorCheck > 1000) {
    lastSensorCheck = millis();
    checkTankLevel();
  }
  
  // Status-Updates für laufendes Programm
  if (systemState.state == RUNNING) {
    static uint32_t lastStatusUpdate = 0;
    if (millis() - lastStatusUpdate > 1000) {
      lastStatusUpdate = millis();
      updateRunningScreen();
    }
  }
  
  // WiFi und Remote-Kommunikation verwalten
  wifiManager.loop();
  
  if (wifiManager.isConnected()) {
    mqttClient.loop();
    restApi.loop();
    
    // Telemetriedaten periodisch senden, wenn verbunden
    static uint32_t lastTelemetryUpdate = 0;
    if (millis() - lastTelemetryUpdate > 60000) { // Alle 60 Sekunden
      lastTelemetryUpdate = millis();
      
      // Telemetriedaten sammeln und senden
      DynamicJsonDocument telemetryDoc(256);
      telemetryDoc["state"] = (int)systemState.state;
      telemetryDoc["program"] = systemState.activeProgram;
      telemetryDoc["remaining_time"] = getRemainingTime();
      telemetryDoc["progress"] = getProgressPercent();
      telemetryDoc["tank_level_ok"] = systemState.tankLevelOk;
      telemetryDoc["uptime"] = millis() / 1000;
      
      mqttClient.publishTelemetry(telemetryDoc.as<JsonObject>());
    }
  }
  
  delay(5); // Kurze Pause für ESP-Stabilität
}

// Erstellt den Hauptbildschirm
void createMainScreen() {
  mainScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(mainScreen, lv_color_hex(0x003366), LV_PART_MAIN | LV_STATE_DEFAULT);
  
  // Titel
  lv_obj_t *title = lv_label_create(mainScreen);
  lv_label_set_text(title, "Desinfektionseinheit");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
  
  // Programmauswahl-Button
  lv_obj_t *programBtn = lv_btn_create(mainScreen);
  lv_obj_set_size(programBtn, 300, 80);
  lv_obj_align(programBtn, LV_ALIGN_CENTER, 0, -80);
  lv_obj_add_event_cb(programBtn, [](lv_event_t *e) {
    lv_scr_load(programScreen);
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *programLabel = lv_label_create(programBtn);
  lv_label_set_text(programLabel, "Programme");
  lv_obj_center(programLabel);
  
  // Einstellungen-Button
  lv_obj_t *settingsBtn = lv_btn_create(mainScreen);
  lv_obj_set_size(settingsBtn, 300, 80);
  lv_obj_align(settingsBtn, LV_ALIGN_CENTER, 0, 20);
  lv_obj_add_event_cb(settingsBtn, [](lv_event_t *e) {
    lv_scr_load(settingsScreen);
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *settingsLabel = lv_label_create(settingsBtn);
  lv_label_set_text(settingsLabel, "Einstellungen");
  lv_obj_center(settingsLabel);
  
  // Status-Anzeige
  statusLabel = lv_label_create(mainScreen);
  lv_label_set_text(statusLabel, "Bereit für Desinfektion");
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(statusLabel, LV_ALIGN_BOTTOM_MID, 0, -30);
}

// Erstellt den Programm-Auswahlbildschirm
void createProgramScreen() {
  // Hier Programmauswahlbildschirm-Code einfügen
  // (Gekürzt, um Platz zu sparen)
}

// Erstellt den Einstellungsbildschirm
void createSettingsScreen() {
  // Hier Einstellungsbildschirm-Code einfügen
  // (Gekürzt, um Platz zu sparen)
}

// Erstellt den Bildschirm für laufende Programme
void createRunningScreen() {
  // Hier Bildschirm für laufende Programme einfügen
  // (Gekürzt, um Platz zu sparen)
}

// Erstellt den Bildschirm für abgeschlossene Programme
void createCompletedScreen() {
  // Hier Bildschirm für abgeschlossene Programme einfügen
  // (Gekürzt, um Platz zu sparen)
}

// Erstellt den Fehlerbildschirm
void createErrorScreen() {
  // Hier Fehlerbildschirm-Code einfügen
  // (Gekürzt, um Platz zu sparen)
}

// Aktualisiert die Anzeige im laufenden Programm
void updateRunningScreen() {
  // Hier Code für die Aktualisierung der Anzeige einfügen
  // (Gekürzt, um Platz zu sparen)
}

// Programm starten
void startProgram(int programIndex) {
  // Abhängig vom Programmindex entsprechende Dauer setzen
  switch (programIndex) {
    case 1:
      systemState.programDuration = PROGRAM_1_DURATION;
      break;
    case 2:
      systemState.programDuration = PROGRAM_2_DURATION;
      break;
    case 3:
      systemState.programDuration = PROGRAM_3_DURATION;
      break;
    case 4:
      systemState.programDuration = systemState.customDays * 24 * 60 * 60;
      break;
    default:
      return; // Ungültiger Index
  }
  
  // Programm starten
  systemState.activeProgram = programIndex;
  systemState.state = RUNNING;
  systemState.startTime = rtc.getEpoch();
  systemState.motorActive = true;
  
  // Motor aktivieren
  digitalWrite(MOTOR_PIN, HIGH);
  
  // Status-LED auf Blau setzen
  setLedStatus(RUNNING);
  
  // Status-Text aktualisieren
  lv_label_set_text(statusLabel, "Programm läuft");
  
  // MQTT-Status senden, wenn Fernsteuerung aktiviert
  if (systemState.remoteControlEnabled && wifiManager.isConnected()) {
    // Detaillierten Status senden
    DynamicJsonDocument statusDoc(128);
    statusDoc["program"] = programIndex;
    statusDoc["duration"] = systemState.programDuration;
    
    mqttClient.publishDetailedStatus("program_started", statusDoc.as<JsonObject>());
  }
}

// Programm stoppen
void stopProgram() {
  systemState.state = IDLE;
  systemState.motorActive = false;
  
  // Motor deaktivieren
  digitalWrite(MOTOR_PIN, LOW);
  
  // Status-LED auf Grün setzen
  setLedStatus(IDLE);
  
  // Status-Text aktualisieren
  lv_label_set_text(statusLabel, "Bereit für Desinfektion");
  
  // MQTT-Status senden, wenn Fernsteuerung aktiviert
  if (systemState.remoteControlEnabled && wifiManager.isConnected()) {
    mqttClient.publishStatus("program_stopped");
  }
}

// Formatiert Zeit in Tage, Stunden, Minuten
String formatTime(uint32_t seconds) {
  uint32_t days = seconds / (24 * 60 * 60);
  seconds %= (24 * 60 * 60);
  uint32_t hours = seconds / (60 * 60);
  seconds %= (60 * 60);
  uint32_t minutes = seconds / 60;
  
  return String(days) + " Tage " + String(hours) + " Std " + String(minutes) + " Min";
}

// Berechnet die verbleibende Zeit des Programms
uint32_t getRemainingTime() {
  if (systemState.state != RUNNING) {
    return 0;
  }
  
  uint32_t elapsedTime = rtc.getEpoch() - systemState.startTime;
  
  if (elapsedTime >= systemState.programDuration) {
    return 0;
  }
  
  return systemState.programDuration - elapsedTime;
}

// Berechnet den Fortschritt in Prozent
int getProgressPercent() {
  if (systemState.state != RUNNING) {
    return 0;
  }
  
  uint32_t elapsedTime = rtc.getEpoch() - systemState.startTime;
  
  if (elapsedTime >= systemState.programDuration) {
    return 100;
  }
  
  return (elapsedTime * 100) / systemState.programDuration;
}

// Setzt die Status-LED entsprechend dem aktuellen Zustand
void setLedStatus(ProgramState state) {
  switch (state) {
    case IDLE:
      digitalWrite(RED_PIN, LOW);
      digitalWrite(GREEN_PIN, HIGH);
      digitalWrite(BLUE_PIN, LOW);
      break;
    case RUNNING:
      digitalWrite(RED_PIN, LOW);
      digitalWrite(GREEN_PIN, LOW);
      digitalWrite(BLUE_PIN, HIGH);
      break;
    case COMPLETED:
      digitalWrite(RED_PIN, LOW);
      digitalWrite(GREEN_PIN, HIGH);
      digitalWrite(BLUE_PIN, HIGH);
      break;
    case ERROR:
      digitalWrite(RED_PIN, HIGH);
      digitalWrite(GREEN_PIN, LOW);
      digitalWrite(BLUE_PIN, LOW);
      break;
  }
}

// Prüft den Tankfüllstand und behandelt Fehler
void checkTankLevel() {
  // Simulierter Tankfüllstand
  bool currentLevel = digitalRead(SENSOR_PIN) == HIGH;
  
  // Status geändert?
  if (currentLevel != systemState.tankLevelOk) {
    systemState.tankLevelOk = currentLevel;
    
    // Wenn Füllstand zu niedrig und ein Programm läuft
    if (!systemState.tankLevelOk && systemState.state == RUNNING) {
      // Fehler-Zustand setzen
      systemState.state = ERROR;
      systemState.motorActive = false;
      
      // Motor deaktivieren
      digitalWrite(MOTOR_PIN, LOW);
      
      // Status-LED auf Rot setzen
      setLedStatus(ERROR);
      
      // Zum Fehlerbildschirm wechseln
      lv_scr_load(errorScreen);
      
      // MQTT-Status senden, wenn Fernsteuerung aktiviert
      if (systemState.remoteControlEnabled && wifiManager.isConnected()) {
        mqttClient.publishStatus("error_tank_empty");
      }
    }
  }
}
/*
 * Desinfektionseinheit Controller mit LVGL GUI und Remote-Steuerung
 * 
 * Steuerungssystem für eine Desinfektionseinheit mit Touch-LCD-Interface,
 * verschiedenen Programmen, Laufzeiten, Status-Anzeigen und Remote-Steuerung.
 * 
 * Hardware:
 * - ESP32-S3-Touch-LCD-4.3B
 * - 4,3-Zoll-Touchscreen (800x480, 65K Farben)
 * - RGB-LED für externe Statusanzeige
 * - Motor/Relais für Desinfektionssteuerung
 * - Sensoren für Tankfüllstand
 * - WiFi für Remote-Steuerung und Überwachung
 * 
 * Kommunikationsfunktionen:
 * - MQTT für IoT-Integration und Fernüberwachung
 * - REST API für externe Steuerung und Statusabrufe
 * - WiFi-Verbindungsmanager mit Fallback auf Access Point Modus
 * 
 * Verwendete Bibliotheken:
 * - LVGL für die GUI
 * - TFT_eSPI als Display-Treiber
 * - ESP32Time für präzise Zeitfunktionen
 * - PubSubClient für MQTT-Kommunikation
 * - ArduinoJson für Datenserialierung
 * 
 * Hinweis: Anpassungen an Pin-Konfigurationen und Hardware-Setup sind 
 * möglicherweise erforderlich.
 */

// Grundlegende Bibliotheken
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
  programScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(programScreen, lv_color_hex(0x003366), LV_PART_MAIN | LV_STATE_DEFAULT);
  
  // Titel
  lv_obj_t *title = lv_label_create(programScreen);
  lv_label_set_text(title, "Programmauswahl");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
  
  // Programm 1 Button (7 Tage)
  lv_obj_t *prog1Btn = lv_btn_create(programScreen);
  lv_obj_set_size(prog1Btn, 700, 60);
  lv_obj_align(prog1Btn, LV_ALIGN_TOP_MID, 0, 80);
  lv_obj_add_event_cb(prog1Btn, [](lv_event_t *e) {
    startProgram(1);
    lv_scr_load(runningScreen);
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *prog1Label = lv_label_create(prog1Btn);
  lv_label_set_text(prog1Label, "Programm 1: 7 Tage Desinfektion");
  lv_obj_center(prog1Label);
  
  // Programm 2 Button (14 Tage)
  lv_obj_t *prog2Btn = lv_btn_create(programScreen);
  lv_obj_set_size(prog2Btn, 700, 60);
  lv_obj_align(prog2Btn, LV_ALIGN_TOP_MID, 0, 150);
  lv_obj_add_event_cb(prog2Btn, [](lv_event_t *e) {
    startProgram(2);
    lv_scr_load(runningScreen);
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *prog2Label = lv_label_create(prog2Btn);
  lv_label_set_text(prog2Label, "Programm 2: 14 Tage Desinfektion");
  lv_obj_center(prog2Label);
  
  // Programm 3 Button (21 Tage)
  lv_obj_t *prog3Btn = lv_btn_create(programScreen);
  lv_obj_set_size(prog3Btn, 700, 60);
  lv_obj_align(prog3Btn, LV_ALIGN_TOP_MID, 0, 220);
  lv_obj_add_event_cb(prog3Btn, [](lv_event_t *e) {
    startProgram(3);
    lv_scr_load(runningScreen);
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *prog3Label = lv_label_create(prog3Btn);
  lv_label_set_text(prog3Label, "Programm 3: 21 Tage Desinfektion");
  lv_obj_center(prog3Label);
  
  // Individuelles Programm Button
  lv_obj_t *prog4Btn = lv_btn_create(programScreen);
  lv_obj_set_size(prog4Btn, 700, 60);
  lv_obj_align(prog4Btn, LV_ALIGN_TOP_MID, 0, 290);
  
  // Individuelles Programm mit Eingabe
  static lv_obj_t *daysSpinbox;
  daysSpinbox = lv_spinbox_create(programScreen);
  lv_spinbox_set_range(daysSpinbox, 1, 99);
  lv_spinbox_set_value(daysSpinbox, systemState.customDays);
  lv_obj_set_size(daysSpinbox, 150, 50);
  lv_obj_align(daysSpinbox, LV_ALIGN_TOP_MID, 0, 360);
  
  lv_obj_t *daysLabel = lv_label_create(programScreen);
  lv_label_set_text(daysLabel, "Individuelle Tage:");
  lv_obj_set_style_text_color(daysLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(daysLabel, LV_ALIGN_TOP_MID, -120, 370);
  
  // Minus Button für Spinbox
  lv_obj_t *minusBtn = lv_btn_create(programScreen);
  lv_obj_set_size(minusBtn, 50, 50);
  lv_obj_align(minusBtn, LV_ALIGN_TOP_MID, -100, 360);
  lv_obj_add_event_cb(minusBtn, [](lv_event_t *e) {
    lv_spinbox_decrement((lv_obj_t*)daysSpinbox);
    systemState.customDays = lv_spinbox_get_value(daysSpinbox);
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *minusLabel = lv_label_create(minusBtn);
  lv_label_set_text(minusLabel, "-");
  lv_obj_center(minusLabel);
  
  // Plus Button für Spinbox
  lv_obj_t *plusBtn = lv_btn_create(programScreen);
  lv_obj_set_size(plusBtn, 50, 50);
  lv_obj_align(plusBtn, LV_ALIGN_TOP_MID, 100, 360);
  lv_obj_add_event_cb(plusBtn, [](lv_event_t *e) {
    lv_spinbox_increment((lv_obj_t*)daysSpinbox);
    systemState.customDays = lv_spinbox_get_value(daysSpinbox);
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *plusLabel = lv_label_create(plusBtn);
  lv_label_set_text(plusLabel, "+");
  lv_obj_center(plusLabel);
  
  // Start individuelles Programm Button
  lv_obj_t *startCustomBtn = lv_btn_create(programScreen);
  lv_obj_set_size(startCustomBtn, 300, 60);
  lv_obj_align(startCustomBtn, LV_ALIGN_BOTTOM_MID, 0, -60);
  lv_obj_add_event_cb(startCustomBtn, [](lv_event_t *e) {
    systemState.customDays = lv_spinbox_get_value((lv_obj_t*)daysSpinbox);
    startProgram(4);
    lv_scr_load(runningScreen);
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *startCustomLabel = lv_label_create(startCustomBtn);
  lv_label_set_text(startCustomLabel, "Individuelles Programm starten");
  lv_obj_center(startCustomLabel);
  
  // Zurück Button
  lv_obj_t *backBtn = lv_btn_create(programScreen);
  lv_obj_set_size(backBtn, 150, 60);
  lv_obj_align(backBtn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
  lv_obj_add_event_cb(backBtn, [](lv_event_t *e) {
    lv_scr_load(mainScreen);
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *backLabel = lv_label_create(backBtn);
  lv_label_set_text(backLabel, "Zurück");
  lv_obj_center(backLabel);
}

// Erstellt den Einstellungsbildschirm
void createSettingsScreen() {
  settingsScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(settingsScreen, lv_color_hex(0x003366), LV_PART_MAIN | LV_STATE_DEFAULT);
  
  // Titel
  lv_obj_t *title = lv_label_create(settingsScreen);
  lv_label_set_text(title, "Einstellungen");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
  
  // Helligkeit-Schieberegler
  lv_obj_t *brightnessLabel = lv_label_create(settingsScreen);
  lv_label_set_text(brightnessLabel, "Display-Helligkeit");
  lv_obj_set_style_text_color(brightnessLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(brightnessLabel, LV_ALIGN_TOP_MID, 0, 80);
  
  lv_obj_t *brightnessSlider = lv_slider_create(settingsScreen);
  lv_obj_set_size(brightnessSlider, 400, 20);
  lv_obj_align(brightnessSlider, LV_ALIGN_TOP_MID, 0, 120);
  lv_slider_set_range(brightnessSlider, 10, 100);
  lv_slider_set_value(brightnessSlider, 80, LV_ANIM_OFF);
  
  // Datum und Uhrzeit einstellen
  lv_obj_t *dateTimeLabel = lv_label_create(settingsScreen);
  lv_label_set_text(dateTimeLabel, "Datum und Uhrzeit");
  lv_obj_set_style_text_color(dateTimeLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(dateTimeLabel, LV_ALIGN_TOP_MID, -180, 160);
  
  lv_obj_t *dateTimeBtn = lv_btn_create(settingsScreen);
  lv_obj_set_size(dateTimeBtn, 250, 60);
  lv_obj_align(dateTimeBtn, LV_ALIGN_TOP_MID, -180, 200);
  
  lv_obj_t *dateTimeBtnLabel = lv_label_create(dateTimeBtn);
  lv_label_set_text(dateTimeBtnLabel, "Datum/Uhrzeit einstellen");
  lv_obj_center(dateTimeBtnLabel);
  
  // Tank-Niveau-Kalibrierung
  lv_obj_t *tankCalibLabel = lv_label_create(settingsScreen);
  lv_label_set_text(tankCalibLabel, "Tank-Sensor kalibrieren");
  lv_obj_set_style_text_color(tankCalibLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(tankCalibLabel, LV_ALIGN_TOP_MID, 180, 160);
  
  lv_obj_t *tankCalibBtn = lv_btn_create(settingsScreen);
  lv_obj_set_size(tankCalibBtn, 250, 60);
  lv_obj_align(tankCalibBtn, LV_ALIGN_TOP_MID, 180, 200);
  
  lv_obj_t *tankCalibBtnLabel = lv_label_create(tankCalibBtn);
  lv_label_set_text(tankCalibBtnLabel, "Kalibrieren");
  lv_obj_center(tankCalibBtnLabel);
  
  // WiFi-Einstellungen
  lv_obj_t *wifiLabel = lv_label_create(settingsScreen);
  lv_label_set_text(wifiLabel, "WLAN-Verbindung");
  lv_obj_set_style_text_color(wifiLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(wifiLabel, LV_ALIGN_TOP_MID, -180, 280);
  
  lv_obj_t *wifiBtn = lv_btn_create(settingsScreen);
  lv_obj_set_size(wifiBtn, 250, 60);
  lv_obj_align(wifiBtn, LV_ALIGN_TOP_MID, -180, 320);
  lv_obj_add_event_cb(wifiBtn, [](lv_event_t *e) {
    // WLAN zurücksetzen und Access Point starten
    wifiManager.reset();
    lv_label_set_text(statusLabel, "WLAN-Konfiguration gestartet");
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *wifiBtnLabel = lv_label_create(wifiBtn);
  lv_label_set_text(wifiBtnLabel, "WLAN konfigurieren");
  lv_obj_center(wifiBtnLabel);
  
  // Remote-Steuerung aktivieren/deaktivieren
  lv_obj_t *remoteLabel = lv_label_create(settingsScreen);
  lv_label_set_text(remoteLabel, "Fernsteuerung");
  lv_obj_set_style_text_color(remoteLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(remoteLabel, LV_ALIGN_TOP_MID, 180, 280);
  
  static lv_obj_t *remoteSwitch = lv_switch_create(settingsScreen);
  lv_obj_align(remoteSwitch, LV_ALIGN_TOP_MID, 180, 320);
  if (systemState.remoteControlEnabled) {
    lv_obj_add_state(remoteSwitch, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(remoteSwitch, [](lv_event_t *e) {
    systemState.remoteControlEnabled = lv_obj_has_state(remoteSwitch, LV_STATE_CHECKED);
    if (systemState.remoteControlEnabled) {
      lv_label_set_text(statusLabel, "Fernsteuerung aktiviert");
    } else {
      lv_label_set_text(statusLabel, "Fernsteuerung deaktiviert");
    }
  }, LV_EVENT_VALUE_CHANGED, NULL);
  
  // Geräte-ID anzeigen
  lv_obj_t *deviceIdLabel = lv_label_create(settingsScreen);
  lv_label_set_text(deviceIdLabel, "Geräte-ID:");
  lv_obj_set_style_text_color(deviceIdLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(deviceIdLabel, LV_ALIGN_BOTTOM_MID, 0, -80);
  
  lv_obj_t *deviceIdValue = lv_label_create(settingsScreen);
  lv_label_set_text(deviceIdValue, systemState.deviceId.c_str());
  lv_obj_set_style_text_color(deviceIdValue, lv_color_hex(0x00FFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(deviceIdValue, LV_ALIGN_BOTTOM_MID, 0, -60);
  
  // Zurück Button
  lv_obj_t *backBtn = lv_btn_create(settingsScreen);
  lv_obj_set_size(backBtn, 150, 60);
  lv_obj_align(backBtn, LV_ALIGN_BOTTOM_LEFT, 20, -20);
  lv_obj_add_event_cb(backBtn, [](lv_event_t *e) {
    lv_scr_load(mainScreen);
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *backLabel = lv_label_create(backBtn);
  lv_label_set_text(backLabel, "Zurück");
  lv_obj_center(backLabel);
}

// Erstellt den Bildschirm für laufende Programme
void createRunningScreen() {
  runningScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(runningScreen, lv_color_hex(0x003366), LV_PART_MAIN | LV_STATE_DEFAULT);
  
  // Titel
  lv_obj_t *title = lv_label_create(runningScreen);
  lv_label_set_text(title, "Programm aktiv");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
  
  // Programm-Name
  programLabel = lv_label_create(runningScreen);
  lv_label_set_text(programLabel, "Programm 2: 14 Tage Desinfektion");
  lv_obj_set_style_text_color(programLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(programLabel, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(programLabel, LV_ALIGN_TOP_MID, 0, 80);
  
  // Fortschrittsbalken
  progressBar = lv_bar_create(runningScreen);
  lv_obj_set_size(progressBar, 700, 30);
  lv_obj_align(progressBar, LV_ALIGN_TOP_MID, 0, 130);
  lv_bar_set_range(progressBar, 0, 100);
  lv_bar_set_value(progressBar, 0, LV_ANIM_OFF);
  
  // Zeit-Anzeige
  timeLabel = lv_label_create(runningScreen);
  lv_label_set_text(timeLabel, "Verbleibende Zeit: 14 Tage 0 Std 0 Min");
  lv_obj_set_style_text_color(timeLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(timeLabel, LV_ALIGN_TOP_MID, 0, 180);
  
  // Tank-Status Anzeige
  lv_obj_t *tankStatusLabel = lv_label_create(runningScreen);
  lv_label_set_text(tankStatusLabel, "Tank-Status:");
  lv_obj_set_style_text_color(tankStatusLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(tankStatusLabel, LV_ALIGN_TOP_MID, -80, 240);
  
  lv_obj_t *tankStatusIcon = lv_label_create(runningScreen);
  lv_label_set_text(tankStatusIcon, LV_SYMBOL_OK);
  lv_obj_set_style_text_color(tankStatusIcon, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(tankStatusIcon, LV_ALIGN_TOP_MID, 20, 240);
  
  // Stop-Button
  lv_obj_t *stopBtn = lv_btn_create(runningScreen);
  lv_obj_set_size(stopBtn, 250, 60);
  lv_obj_align(stopBtn, LV_ALIGN_BOTTOM_MID, 0, -60);
  lv_obj_set_style_bg_color(stopBtn, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_add_event_cb(stopBtn, [](lv_event_t *e) {
    stopProgram();
    lv_scr_load(mainScreen);
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *stopLabel = lv_label_create(stopBtn);
  lv_label_set_text(stopLabel, "Programm stoppen");
  lv_obj_center(stopLabel);
}

// Erstellt den Bildschirm für abgeschlossene Programme
void createCompletedScreen() {
  completedScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(completedScreen, lv_color_hex(0x006600), LV_PART_MAIN | LV_STATE_DEFAULT);
  
  // Titel
  lv_obj_t *title = lv_label_create(completedScreen);
  lv_label_set_text(title, "Programm abgeschlossen");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
  
  // Erfolgssymbol
  lv_obj_t *successIcon = lv_label_create(completedScreen);
  lv_label_set_text(successIcon, LV_SYMBOL_OK);
  lv_obj_set_style_text_color(successIcon, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(successIcon, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(successIcon, LV_ALIGN_CENTER, 0, -60);
  
  // Erfolgsmeldung
  lv_obj_t *successMessage = lv_label_create(completedScreen);
  lv_label_set_text(successMessage, "Desinfektion erfolgreich abgeschlossen!");
  lv_obj_set_style_text_color(successMessage, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(successMessage, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(successMessage, LV_ALIGN_CENTER, 0, 20);
  
  // Zurück zum Hauptmenü Button
  lv_obj_t *homeBtn = lv_btn_create(completedScreen);
  lv_obj_set_size(homeBtn, 300, 60);
  lv_obj_align(homeBtn, LV_ALIGN_BOTTOM_MID, 0, -60);
  lv_obj_add_event_cb(homeBtn, [](lv_event_t *e) {
    lv_scr_load(mainScreen);
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *homeLabel = lv_label_create(homeBtn);
  lv_label_set_text(homeLabel, "Zum Hauptmenü");
  lv_obj_center(homeLabel);
}

// Erstellt den Fehlerbildschirm
void createErrorScreen() {
  errorScreen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(errorScreen, lv_color_hex(0x990000), LV_PART_MAIN | LV_STATE_DEFAULT);
  
  // Titel
  lv_obj_t *title = lv_label_create(errorScreen);
  lv_label_set_text(title, "Fehler");
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_28, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
  
  // Fehlersymbol
  lv_obj_t *errorIcon = lv_label_create(errorScreen);
  lv_label_set_text(errorIcon, LV_SYMBOL_WARNING);
  lv_obj_set_style_text_color(errorIcon, lv_color_hex(0xFFFF00), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(errorIcon, &lv_font_montserrat_48, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(errorIcon, LV_ALIGN_CENTER, 0, -60);
  
  // Fehlermeldung
  lv_obj_t *errorMessage = lv_label_create(errorScreen);
  lv_label_set_text(errorMessage, "Tankfüllstand zu niedrig!");
  lv_obj_set_style_text_color(errorMessage, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_font(errorMessage, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(errorMessage, LV_ALIGN_CENTER, 0, 0);
  
  // Anweisungen
  lv_obj_t *instructions = lv_label_create(errorScreen);
  lv_label_set_text(instructions, "Bitte Tank auffüllen und neu starten.");
  lv_obj_set_style_text_color(instructions, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(instructions, LV_ALIGN_CENTER, 0, 40);
  
  // OK-Button
  lv_obj_t *okBtn = lv_btn_create(errorScreen);
  lv_obj_set_size(okBtn, 200, 60);
  lv_obj_align(okBtn, LV_ALIGN_BOTTOM_MID, 0, -60);
  lv_obj_add_event_cb(okBtn, [](lv_event_t *e) {
    if (systemState.tankLevelOk) {
      lv_scr_load(mainScreen);
    }
  }, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t *okLabel = lv_label_create(okBtn);
  lv_label_set_text(okLabel, "OK");
  lv_obj_center(okLabel);
}

// Aktualisiert die Anzeige im laufenden Programm
void updateRunningScreen() {
  if (systemState.state != RUNNING) return;
  
  // Programm-Label aktualisieren
  char programText[50];
  const char* programNames[] = {"Programm 1: 7 Tage", "Programm 2: 14 Tage", "Programm 3: 21 Tage", "Individuell"};
  sprintf(programText, "%s Desinfektion", programNames[systemState.activeProgram - 1]);
  lv_label_set_text(programLabel, programText);
  
  // Fortschrittsbalken aktualisieren
  int progress = getProgressPercent();
  lv_bar_set_value(progressBar, progress, LV_ANIM_ON);
  
  // Zeitanzeige aktualisieren
  char timeText[50];
  if (systemState.activeProgram == 4 && systemState.programDuration == 0) {
    // Für individuelles Programm ohne Zeitbegrenzung
    sprintf(timeText, "Individuelles Programm läuft");
  } else {
    // Für Programme mit Zeitbegrenzung
    uint32_t remainingSeconds = getRemainingTime();
    sprintf(timeText, "Verbleibende Zeit: %s", formatTime(remainingSeconds).c_str());
  }
  lv_label_set_text(timeLabel, timeText);
}

// Startet ein Programm
void startProgram(int programIndex) {
  systemState.activeProgram = programIndex;
  
  // Programmdauer setzen basierend auf gewähltem Programm
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
  }
  
  systemState.state = RUNNING;
  systemState.startTime = rtc.getEpoch();
  systemState.motorActive = true;
  digitalWrite(MOTOR_PIN, HIGH);
  setLedStatus(RUNNING);
  
  // Anzeige aktualisieren
  updateRunningScreen();
  
  Serial.print("Programm gestartet: ");
  Serial.print(programIndex);
  Serial.print(" mit Dauer: ");
  Serial.print(systemState.programDuration);
  Serial.println(" Sekunden");
}

// Stoppt das aktuelle Programm
void stopProgram() {
  systemState.state = IDLE;
  systemState.motorActive = false;
  digitalWrite(MOTOR_PIN, LOW);
  setLedStatus(IDLE);
  
  Serial.println("Programm gestoppt");
}

// Formatiert eine Zeitangabe in Sekunden zu einer lesbaren Form
String formatTime(uint32_t seconds) {
  uint32_t days = seconds / (24 * 60 * 60);
  seconds %= (24 * 60 * 60);
  uint32_t hours = seconds / (60 * 60);
  seconds %= (60 * 60);
  uint32_t minutes = seconds / 60;
  
  char buffer[50];
  if (days > 0) {
    sprintf(buffer, "%lu Tage %lu Std %lu Min", days, hours, minutes);
  } else if (hours > 0) {
    sprintf(buffer, "%lu Std %lu Min", hours, minutes);
  } else {
    sprintf(buffer, "%lu Min", minutes);
  }
  
  return String(buffer);
}

// Berechnet die verbleibende Zeit des aktuellen Programms
uint32_t getRemainingTime() {
  if (systemState.state != RUNNING || systemState.programDuration == 0) {
    return 0;
  }
  
  uint32_t elapsedSeconds = rtc.getEpoch() - systemState.startTime;
  if (elapsedSeconds >= systemState.programDuration) {
    return 0;
  }
  
  return systemState.programDuration - elapsedSeconds;
}

// Berechnet den Fortschritt in Prozent
int getProgressPercent() {
  if (systemState.state != RUNNING || systemState.programDuration == 0) {
    return 0;
  }
  
  uint32_t elapsedSeconds = rtc.getEpoch() - systemState.startTime;
  if (elapsedSeconds >= systemState.programDuration) {
    return 100;
  }
  
  return (elapsedSeconds * 100) / systemState.programDuration;
}

// Setzt die RGB-LED basierend auf dem Programmstatus
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
      digitalWrite(BLUE_PIN, LOW);
      break;
    case ERROR:
      digitalWrite(RED_PIN, HIGH);
      digitalWrite(GREEN_PIN, LOW);
      digitalWrite(BLUE_PIN, LOW);
      break;
  }
}

// Überprüft den Tankfüllstand
void checkTankLevel() {
  // Tankfüllstand lesen - für reale Anwendung Sensorlogik anpassen
  int sensorValue = digitalRead(SENSOR_PIN);
  
  // Simulierter Wert: LOW = leerer Tank
  bool tankIsOk = (sensorValue == HIGH);
  
  if (systemState.tankLevelOk != tankIsOk) {
    systemState.tankLevelOk = tankIsOk;
    
    if (!tankIsOk) {
      // Tank ist leer - Fehlerbehandlung
      if (systemState.state == RUNNING) {
        stopProgram();
      }
      systemState.state = ERROR;
      setLedStatus(ERROR);
      lv_scr_load(errorScreen);
    }
  }
}
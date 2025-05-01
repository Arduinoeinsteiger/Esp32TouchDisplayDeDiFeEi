/*
 * Desinfektionseinheit Controller mit LVGL GUI
 * 
 * Steuerungssystem für eine Desinfektionseinheit mit Touch-LCD-Interface und
 * verschiedenen Programmen, Laufzeiten, und Status-Anzeigen.
 * 
 * Hardware:
 * - ESP32-S3-Touch-LCD-4.3B
 * - 4,3-Zoll-Touchscreen (800x480, 65K Farben)
 * - RGB-LED für externe Statusanzeige
 * - Motor/Relais für Desinfektionssteuerung
 * - Sensoren für Tankfüllstand
 * 
 * Verwendete Bibliotheken:
 * - LVGL für die GUI
 * - TFT_eSPI als Display-Treiber
 * - ESP32Time für präzise Zeitfunktionen
 * 
 * Hinweis: Anpassungen an Pin-Konfigurationen und Hardware-Setup sind 
 * möglicherweise erforderlich.
 */

// Grundlegende Bibliotheken
#include <Arduino.h>
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <ESP32Time.h>

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
};

SystemState systemState;
ESP32Time rtc;

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

void setup() {
  Serial.begin(115200);
  Serial.println("Desinfektionseinheit mit LVGL startet...");

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
  lv_obj_align(dateTimeLabel, LV_ALIGN_TOP_MID, 0, 160);
  
  lv_obj_t *dateTimeBtn = lv_btn_create(settingsScreen);
  lv_obj_set_size(dateTimeBtn, 250, 60);
  lv_obj_align(dateTimeBtn, LV_ALIGN_TOP_MID, 0, 200);
  
  lv_obj_t *dateTimeBtnLabel = lv_label_create(dateTimeBtn);
  lv_label_set_text(dateTimeBtnLabel, "Datum/Uhrzeit einstellen");
  lv_obj_center(dateTimeBtnLabel);
  
  // Tank-Niveau-Kalibrierung
  lv_obj_t *tankCalibLabel = lv_label_create(settingsScreen);
  lv_label_set_text(tankCalibLabel, "Tank-Sensor kalibrieren");
  lv_obj_set_style_text_color(tankCalibLabel, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_align(tankCalibLabel, LV_ALIGN_TOP_MID, 0, 280);
  
  lv_obj_t *tankCalibBtn = lv_btn_create(settingsScreen);
  lv_obj_set_size(tankCalibBtn, 250, 60);
  lv_obj_align(tankCalibBtn, LV_ALIGN_TOP_MID, 0, 320);
  
  lv_obj_t *tankCalibBtnLabel = lv_label_create(tankCalibBtn);
  lv_label_set_text(tankCalibBtnLabel, "Kalibrieren");
  lv_obj_center(tankCalibBtnLabel);
  
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
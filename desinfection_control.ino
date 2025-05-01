/*
 * Desinfektionseinheit Controller
 * 
 * Das ist der Code für die Steuerung einer Desinfektionseinheit mit
 * verschiedenen Programmen und Laufzeiten.
 * 
 * Hardware:
 * - ESP8266 (z.B. D1 Mini)
 * - I2C OLED-Display (0.96" 128x64 SSD1306)
 * - RGB-LED für Statusanzeige
 * - Taster für Programmwechsel
 * - Plus/Minus Taster für Konfiguration
 * - Motor/Relais für Desinfektionssteuerung
 * - Sensor für Tankfüllstand
 * 
 * Pin-Konfiguration:
 * - OLED (I2C): SDA = D2 (GPIO4), SCL = D1 (GPIO5)
 * - Blaue LED: D0 (GPIO16)
 * - Taster: D8 (GPIO15)
 * - Rote LED: D6 (GPIO12)
 * - Grüne LED: D7 (GPIO13)
 * - Motor/Relais: D4 (GPIO2)
 * - Sensor: D5 (GPIO14)
 * - Plus-Button: D2 (GPIO4)
 * - Minus-Button: D3 (GPIO0)
 */

#include <TimeLib.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include "leds.h"
#include "display.h"
#include "programs.h"
#include "menu.h"

// Display-Konfiguration
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1  // Reset-Pin wird nicht verwendet
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Pin-Definitionen
#define RED_PIN D6      // Rote LED
#define GREEN_PIN D7    // Grüne LED 
#define BLUE_PIN D0     // Blaue LED
#define BUTTON_PIN D8   // Haupttaster für Programmwechsel
#define PLUS_BUTTON D2  // Plus-Taster für Einstellungen
#define MINUS_BUTTON D3 // Minus-Taster für Einstellungen
#define MOTOR_PIN D4    // Motor/Relais
#define SENSOR_PIN D5   // Tankfüllstand-Sensor

// Globale Variablen
MenuState currentMenuState = START_SCREEN;
ButtonState buttonState = IDLE;
String activeProgram = "Programm 2"; // Standardprogramm (14 Tage)
unsigned long programDuration = 1209600000; // 14 Tage in Millisekunden
unsigned long startTime = 0;
bool programActive = false;
int customDays = 7; // Standardwert für benutzerdefiniertes Programm
unsigned long lastDisplayUpdate = 0;
unsigned long autoStartTimer = 0;
bool autoStartActive = true;

// Variablen für Scrolling Text
int scrollPosition = 0;
const int scrollSpeed = 200;
unsigned long lastScrollTime = 0;
int maxScrollPosition_StartScreenTitle = 0;
int maxScrollPosition_StartScreenMessage = 0;
int maxScrollPosition_MainMenu = 0;
int maxScrollPosition_NewSetupInit = 0;

// Button-Zeitsteuerung
unsigned long buttonPressStart = 0;
unsigned long shortPressDetectedAt = 0;
const unsigned long shortPressDuration = 200;
const unsigned long longPressDuration = 2000;
const unsigned long waitingDuration = 5000;
unsigned long lastButtonCheck = 0;
const unsigned long buttonCheckInterval = 100; // Prüfe Buttons alle 100ms

// Sensor-Zeitsteuerung
unsigned long lastSensorCheck = 0;
const unsigned long sensorCheckInterval = 1000; // Prüfe Sensor alle 1000ms

void setup() {
  Serial.begin(115200);
  Serial.println("\nDesinfektionseinheit startet...");
  
  // Pin-Modi setzen
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  pinMode(MOTOR_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(PLUS_BUTTON, INPUT_PULLUP);
  pinMode(MINUS_BUTTON, INPUT_PULLUP);
  pinMode(SENSOR_PIN, INPUT);
  
  // Ausgänge initialisieren
  digitalWrite(MOTOR_PIN, LOW);
  
  // I2C und Display initialisieren
  Wire.begin(4, 5); // SDA, SCL
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 Display-Initialisierung fehlgeschlagen"));
    while(true) {
      // Fehleranzeige über LEDs, falls Display nicht funktioniert
      digitalWrite(RED_PIN, HIGH);
      delay(500);
      digitalWrite(RED_PIN, LOW);
      delay(500);
    }
  }
  
  // Display einrichten
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("Desinfektionseinheit"));
  display.println(F("startet..."));
  display.display();
  
  // Standard-LED-Status setzen
  setLED("normal");
  
  // Initialisiere Auto-Start-Timer
  autoStartTimer = millis();
  
  delay(1000); // Kurze Verzögerung für bessere Lesbarkeit
  
  // Initialen Displayzustand setzen und anzeigen
  updateDisplay();
}

void loop() {
  unsigned long currentMillis = millis();
  
  // Prüfe Buttons in regelmäßigen Abständen
  if (currentMillis - lastButtonCheck >= buttonCheckInterval) {
    lastButtonCheck = currentMillis;
    handleButtonPress();
    checkPlusMinusButtons();
  }
  
  // Prüfe Sensor in regelmäßigen Abständen
  if (currentMillis - lastSensorCheck >= sensorCheckInterval) {
    lastSensorCheck = currentMillis;
    checkTankLevel();
  }
  
  // Aktualisiere Display wenn nötig (abhängig vom Zustand)
  if (programActive || currentMillis - lastDisplayUpdate >= 1000) {
    lastDisplayUpdate = currentMillis;
    updateDisplay();
  }
  
  // Scroll Text bei Bedarf
  handleTextScrolling(currentMillis);
  
  // Prüfe Auto-Start Bedingung
  if (autoStartActive && currentMenuState == START_SCREEN && (currentMillis - autoStartTimer > 30000)) {
    autoStartActive = false; // Deaktiviere nach einmaligem Ausführen
    switchToProgram("Programm 2"); // 14-Tage-Programm
    startProgram();
    currentMenuState = PROGRAM_RUNNING;
  }
  
  // Prüfe, ob laufendes Programm beendet werden muss
  if (programActive) {
    unsigned long elapsedTime = currentMillis - startTime;
    if (programDuration > 0 && elapsedTime >= programDuration) {
      stopProgram();
      currentMenuState = PROGRAM_COMPLETED;
    }
  }
}

// Prüft Füllstand des Tanks über den Sensor
void checkTankLevel() {
  int sensorValue = digitalRead(SENSOR_PIN);
  
  // Wenn Sensor LOW ist, ist der Tank leer (abhängig von der Sensorlogik)
  if (sensorValue == LOW) {
    if (programActive) {
      stopProgram();
    }
    currentMenuState = ERROR_TANK_LOW;
    setLED("problem");
  }
}

// Behandelt Plus- und Minus-Tastendrücke für Menünavigation und Einstellungen
void checkPlusMinusButtons() {
  bool plusPressed = (digitalRead(PLUS_BUTTON) == LOW);
  bool minusPressed = (digitalRead(MINUS_BUTTON) == LOW);
  
  if (plusPressed || minusPressed) {
    switch (currentMenuState) {
      case MAIN_MENU:
        if (plusPressed) navigateMenu(true);
        if (minusPressed) navigateMenu(false);
        break;
      
      case SETUP_NEW_DAYS_ADJUST:
        if (plusPressed && customDays < 99) customDays++;
        if (minusPressed && customDays > 1) customDays--;
        break;
        
      case SETUP_14_DAYS_CONFIRM:
      case SETUP_21_DAYS_CONFIRM:
      case SETUP_CONFIRMATION:
        if (plusPressed) confirmSelection();
        if (minusPressed) cancelSelection();
        break;
    }
    
    updateDisplay();
    delay(200); // Simple debouncing
  }
}

// Navigiert durch das Menü nach oben oder unten
void navigateMenu(bool up) {
  switch (currentMenuState) {
    case MAIN_MENU:
      if (up) {
        currentMenuState = SETUP_14_DAYS_CONFIRM;
      } else {
        currentMenuState = SETUP_21_DAYS_CONFIRM;
      }
      break;
      
    case SETUP_14_DAYS_CONFIRM:
      if (up) {
        currentMenuState = SETUP_21_DAYS_CONFIRM;
      } else {
        currentMenuState = SETUP_NEW_DAYS_INIT;
      }
      break;
      
    case SETUP_21_DAYS_CONFIRM:
      if (up) {
        currentMenuState = SETUP_NEW_DAYS_INIT;
      } else {
        currentMenuState = MAIN_MENU;
      }
      break;
      
    case SETUP_NEW_DAYS_INIT:
      if (up) {
        currentMenuState = MAIN_MENU;
      } else {
        currentMenuState = SETUP_14_DAYS_CONFIRM;
      }
      break;
  }
}

// Bestätigt die aktuelle Auswahl
void confirmSelection() {
  switch (currentMenuState) {
    case SETUP_14_DAYS_CONFIRM:
      switchToProgram("Programm 2"); // 14 Tage
      startProgram();
      currentMenuState = PROGRAM_RUNNING;
      break;
      
    case SETUP_21_DAYS_CONFIRM:
      switchToProgram("Programm 3"); // 21 Tage
      startProgram();
      currentMenuState = PROGRAM_RUNNING;
      break;
      
    case SETUP_NEW_DAYS_INIT:
      currentMenuState = SETUP_NEW_DAYS_ADJUST;
      break;
      
    case SETUP_CONFIRMATION:
      switchToProgram("Programm 4");
      // Berechne benutzerdefinierte Dauer basierend auf den Tagen
      programDuration = (unsigned long)customDays * 24 * 60 * 60 * 1000;
      startProgram();
      currentMenuState = PROGRAM_RUNNING;
      break;
  }
}

// Bricht die aktuelle Auswahl ab
void cancelSelection() {
  currentMenuState = MAIN_MENU;
}

// Wechselt zu einem bestimmten Programm
void switchToProgram(String program) {
  activeProgram = program;
  
  if (program == "Programm 1") {
    programDuration = 604800000; // 7 Tage in Millisekunden
  } else if (program == "Programm 2") {
    programDuration = 1209600000; // 14 Tage in Millisekunden
  } else if (program == "Programm 3") {
    programDuration = 1814400000; // 21 Tage in Millisekunden
  } else if (program == "Programm 4") {
    // Für Programm 4 wird die Dauer separat über customDays gesetzt
  }
  
  Serial.print("Programm gewechselt zu: ");
  Serial.println(activeProgram);
  Serial.print("Programmdauer: ");
  Serial.print(programDuration / 1000 / 60 / 60 / 24);
  Serial.println(" Tage");
}

// Startet das gewählte Programm
void startProgram() {
  if (!programActive) {
    startTime = millis();
    programActive = true;
    digitalWrite(MOTOR_PIN, HIGH);
    setLED("program_active");
    
    Serial.print("Programm gestartet: ");
    Serial.println(activeProgram);
  }
}

// Stoppt das laufende Programm
void stopProgram() {
  if (programActive) {
    programActive = false;
    digitalWrite(MOTOR_PIN, LOW);
    setLED("normal");
    
    Serial.println("Programm beendet");
  }
}

// Funktion zur Steuerung der RGB-LED (Statusanzeige)
void setLED(String status) {
  if (status == "normal") {
    digitalWrite(RED_PIN, LOW);
    digitalWrite(GREEN_PIN, HIGH);
    digitalWrite(BLUE_PIN, LOW);
  } else if (status == "problem") {
    digitalWrite(RED_PIN, HIGH);
    digitalWrite(GREEN_PIN, LOW);
    digitalWrite(BLUE_PIN, LOW);
  } else if (status == "program_active") {
    digitalWrite(RED_PIN, LOW);
    digitalWrite(GREEN_PIN, LOW);
    digitalWrite(BLUE_PIN, HIGH);
  } else {
    // Alle LEDs aus
    digitalWrite(RED_PIN, LOW);
    digitalWrite(GREEN_PIN, LOW);
    digitalWrite(BLUE_PIN, LOW);
  }
}

// Behandelt die Scrollfunktion für Texte auf dem Display
void handleTextScrolling(unsigned long currentMillis) {
  if (currentMillis - lastScrollTime >= scrollSpeed) {
    lastScrollTime = currentMillis;
    
    // Scrolle je nach aktuellem Menüzustand
    switch (currentMenuState) {
      case START_SCREEN:
        // Scrolle Titel
        scrollPosition--;
        if (scrollPosition < -maxScrollPosition_StartScreenTitle) {
          scrollPosition = SCREEN_WIDTH;
        }
        break;
        
      case MAIN_MENU:
        // Scrolle Menütitel
        scrollPosition--;
        if (scrollPosition < -maxScrollPosition_MainMenu) {
          scrollPosition = SCREEN_WIDTH;
        }
        break;
        
      case SETUP_NEW_DAYS_INIT:
        // Scrolle Erklärungstext
        scrollPosition--;
        if (scrollPosition < -maxScrollPosition_NewSetupInit) {
          scrollPosition = SCREEN_WIDTH;
        }
        break;
    }
    
    updateDisplay();
  }
}

// Tasterlogik für Programmwechsel
void handleButtonPress() {
  int buttonStateCurrent = digitalRead(BUTTON_PIN);

  switch (buttonState) {
    case IDLE:
      if (buttonStateCurrent == LOW) {
        buttonPressStart = millis();
        while (digitalRead(BUTTON_PIN) == LOW) {
          delay(10);
        }
        if (millis() - buttonPressStart < shortPressDuration) {
          buttonState = SHORT_PRESS_DETECTED;
          shortPressDetectedAt = millis();
          Serial.println("Kurzer Druck erkannt, Warte auf langen Druck...");
        }
      }
      break;

    case SHORT_PRESS_DETECTED:
      if (millis() - shortPressDetectedAt > waitingDuration) {
        buttonState = IDLE;
        Serial.println("Zeitfenster abgelaufen, zurück zu IDLE");
      } else if (buttonStateCurrent == LOW) {
        buttonPressStart = millis();
        while (digitalRead(BUTTON_PIN) == LOW) {
          delay(10);
        }
        if (millis() - buttonPressStart >= longPressDuration) {
          buttonState = WAITING_FOR_LONG_PRESS;
          Serial.println("Langer Druck erkannt, Programm wird geändert...");
        }
      }
      break;

    case WAITING_FOR_LONG_PRESS:
      changeProgram(); // Funktion zum Programmwechsel aufrufen
      buttonState = IDLE;
      break;
  }
}

// Programm ändern (Zyklisch durch die Programme wechseln)
void changeProgram() {
  if (programActive) {
    stopProgram();
  }
  
  if (activeProgram == "Programm 1") {
    switchToProgram("Programm 2");
  } else if (activeProgram == "Programm 2") {
    switchToProgram("Programm 3");
  } else if (activeProgram == "Programm 3") {
    switchToProgram("Programm 4");
  } else if (activeProgram == "Programm 4") {
    switchToProgram("Programm 1");
  }
  
  updateDisplay();
}

// Funktion zur Berechnung der Restlaufzeit und Formatierung
String getFormattedRemainingTime() {
  if (!programActive || programDuration == 0) {
    return programActive ? "Individuell" : "Bereit";
  }

  unsigned long elapsedTime = millis() - startTime;
  if (elapsedTime >= programDuration) {
    return "Abgelaufen";
  }

  unsigned long remainingTimeMillis = programDuration - elapsedTime;
  long remainingSeconds = remainingTimeMillis / 1000;
  long remainingMinutes = remainingSeconds / 60;
  long remainingHours = remainingMinutes / 60;
  long remainingDays = remainingHours / 24;

  remainingSeconds %= 60;
  remainingMinutes %= 60;
  remainingHours %= 24;

  String formattedTime = "";
  if (remainingDays > 0) {
    formattedTime += String(remainingDays) + "T ";
  }
  if (remainingHours > 0 || remainingDays > 0) {
    formattedTime += String(remainingHours) + "Std ";
  }
  formattedTime += String(remainingMinutes) + "Min";

  return formattedTime;
}

// Display aktualisieren (OLED)
void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  int16_t x1, y1;
  uint16_t w, h;
  String textToScroll;
  int textWidth;

  switch (currentMenuState) {
    case START_SCREEN: {
      // Titel
      display.setTextSize(1);
      textToScroll = F("Desinfektionseinheit");
      display.getTextBounds(textToScroll, 0, 0, &x1, &y1, &w, &h);
      maxScrollPosition_StartScreenTitle = w;
      display.setCursor(scrollPosition, 1);
      display.println(textToScroll);
      
      // Linie unter dem Titel
      display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, WHITE);
      
      // Nachricht
      display.setTextSize(1);
      textToScroll = F("14 Tage Programm startet...");
      display.getTextBounds(textToScroll, 0, 0, &x1, &y1, &w, &h);
      maxScrollPosition_StartScreenMessage = w;
      display.setCursor(scrollPosition, 14);
      display.println(textToScroll);
      
      // Zeit bis zum Start
      display.setCursor(0, 25);
      unsigned long timeLeft = 30 - ((millis() - autoStartTimer) / 1000);
      if (timeLeft > 30) timeLeft = 30;
      display.print("Start in: ");
      display.print(timeLeft);
      display.println(" Sek.");
      
      // Fortschrittsbalken für Auto-Start
      int progressWidth = map(30000 - (millis() - autoStartTimer), 0, 30000, 0, SCREEN_WIDTH - 1);
      if (progressWidth < 0) progressWidth = 0;
      if (progressWidth > SCREEN_WIDTH - 1) progressWidth = SCREEN_WIDTH - 1;
      display.drawRect(0, 35, SCREEN_WIDTH - 1, 10, WHITE);
      display.fillRect(0, 35, progressWidth, 10, WHITE);
      
      // Hinweis
      display.setCursor(0, 50);
      display.println("Taste druecken zum Abbrechen");
      break;
    }
    
    case MAIN_MENU: {
      // Menütitel
      display.setTextSize(1);
      textToScroll = F("Hauptmenu");
      display.getTextBounds(textToScroll, 0, 0, &x1, &y1, &w, &h);
      maxScrollPosition_MainMenu = w;
      display.setCursor(scrollPosition, 1);
      display.println(textToScroll);
      
      // Linie unter dem Titel
      display.drawLine(0, 10, SCREEN_WIDTH - 1, 10, WHITE);
      
      // Menüoptionen
      display.setCursor(0, 15);
      display.println(F("1: 14 Tage Programm"));
      display.setCursor(0, 25);
      display.println(F("2: 21 Tage Programm"));
      display.setCursor(0, 35);
      display.println(F("3: Individuelle Dauer"));
      
      // Aktuelles Programm
      display.setCursor(0, 50);
      display.print(F("Aktiv: "));
      display.print(activeProgram);
      break;
    }
    
    case SETUP_14_DAYS_CONFIRM: {
      display.setCursor(0, 0);
      display.println(F("14 Tage Programm"));
      display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, WHITE);
      
      display.setCursor(0, 15);
      display.println(F("Dieses Programm laeuft"));
      display.setCursor(0, 25);
      display.println(F("fuer genau 14 Tage."));
      
      display.setCursor(0, 40);
      display.println(F("+ Taste: Starten"));
      display.setCursor(0, 50);
      display.println(F("- Taste: Zurueck"));
      break;
    }
    
    case SETUP_21_DAYS_CONFIRM: {
      display.setCursor(0, 0);
      display.println(F("21 Tage Programm"));
      display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, WHITE);
      
      display.setCursor(0, 15);
      display.println(F("Dieses Programm laeuft"));
      display.setCursor(0, 25);
      display.println(F("fuer genau 21 Tage."));
      
      display.setCursor(0, 40);
      display.println(F("+ Taste: Starten"));
      display.setCursor(0, 50);
      display.println(F("- Taste: Zurueck"));
      break;
    }
    
    case SETUP_NEW_DAYS_INIT: {
      display.setCursor(0, 0);
      display.println(F("Individuelle Dauer"));
      display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, WHITE);
      
      textToScroll = F("Setzen Sie eine eigene Programmdauer.");
      display.getTextBounds(textToScroll, 0, 0, &x1, &y1, &w, &h);
      maxScrollPosition_NewSetupInit = w;
      display.setCursor(scrollPosition, 15);
      display.println(textToScroll);
      
      display.setCursor(0, 40);
      display.println(F("+ Taste: Fortfahren"));
      display.setCursor(0, 50);
      display.println(F("- Taste: Zurueck"));
      break;
    }
    
    case SETUP_NEW_DAYS_ADJUST: {
      display.setCursor(0, 0);
      display.println(F("Tage einstellen"));
      display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, WHITE);
      
      display.setCursor(0, 15);
      display.println(F("Wie viele Tage soll das"));
      display.setCursor(0, 25);
      display.println(F("Programm laufen?"));
      
      // Tage mit großer Schrift anzeigen
      display.setTextSize(2);
      display.setCursor(45, 35);
      display.print(customDays);
      display.println(F(" Tage"));
      display.setTextSize(1);
      
      display.setCursor(0, 55);
      display.println(F("+ Taste: Mehr, - Taste: Weniger"));
      break;
    }
    
    case SETUP_CONFIRMATION: {
      display.setCursor(0, 0);
      display.println(F("Bestaetigung"));
      display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, WHITE);
      
      display.setCursor(0, 15);
      display.print(F("Programmdauer: "));
      display.print(customDays);
      display.println(F(" Tage"));
      
      display.setCursor(0, 30);
      display.println(F("Programm starten?"));
      
      display.setCursor(0, 45);
      display.println(F("+ Taste: Starten"));
      display.setCursor(0, 55);
      display.println(F("- Taste: Abbrechen"));
      break;
    }
    
    case PROGRAM_RUNNING: {
      display.setCursor(0, 0);
      display.println(F("Programm aktiv"));
      display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, WHITE);
      
      display.setCursor(0, 12);
      display.println(activeProgram);
      
      display.setCursor(0, 22);
      display.println(F("Verbleibende Zeit:"));
      display.setCursor(0, 32);
      display.println(getFormattedRemainingTime());
      
      // Fortschrittsbalken
      if (programDuration > 0) {
        unsigned long elapsedTime = millis() - startTime;
        int progressPercent = map(elapsedTime, 0, programDuration, 0, 100);
        if (progressPercent > 100) progressPercent = 100;
        
        int progressWidth = map(progressPercent, 0, 100, 0, SCREEN_WIDTH - 1);
        display.drawRect(0, 45, SCREEN_WIDTH - 1, 10, WHITE);
        display.fillRect(0, 45, progressWidth, 10, WHITE);
        
        display.setCursor(0, 56);
        display.print(progressPercent);
        display.print(F("% abgeschlossen"));
      } else {
        display.setCursor(0, 45);
        display.println(F("Individuelle Laufzeit"));
        display.setCursor(0, 55);
        display.println(F("Taste zum Beenden druecken"));
      }
      break;
    }
    
    case PROGRAM_COMPLETED: {
      display.setCursor(0, 0);
      display.println(F("Programm abgeschlossen"));
      display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, WHITE);
      
      display.setCursor(0, 15);
      display.println(F("Desinfektion erfolgreich"));
      display.setCursor(0, 25);
      display.println(F("abgeschlossen!"));
      
      display.setCursor(0, 40);
      display.println(F("Bitte Tank entfernen und"));
      display.setCursor(0, 50);
      display.println(F("Taste druecken für Neustart."));
      break;
    }
    
    case ERROR_TANK_LOW: {
      display.setCursor(0, 0);
      display.println(F("FEHLER!"));
      display.drawLine(0, 8, SCREEN_WIDTH - 1, 8, WHITE);
      
      display.setCursor(0, 15);
      display.println(F("Tankfüllstand zu niedrig!"));
      
      display.setCursor(0, 35);
      display.println(F("Bitte Tank befüllen und"));
      display.setCursor(0, 45);
      display.println(F("System neu starten."));
      break;
    }
  }
  
  display.display();
}

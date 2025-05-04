#ifndef LEDS_H
#define LEDS_H

#include <Arduino.h>

/**
 * LED-Manager zur Steuerung der Status-LEDs.
 * Ermöglicht einfaches Setzen von Farben, Blinken und Pulsen.
 */
class LedManager {
private:
  // Pin-Definitionen für die RGB-LED
  int redPin;
  int greenPin;
  int bluePin;
  
  // Aktuelle LED-Farbe
  uint8_t currentRed;
  uint8_t currentGreen;
  uint8_t currentBlue;
  
  // Blink-Modus
  bool blinkEnabled;
  bool blinkState;
  unsigned long lastBlinkTime;
  unsigned long blinkInterval;
  
  // Puls-Modus
  bool pulseEnabled;
  uint8_t pulseValue;
  int pulseDirection;
  unsigned long lastPulseTime;

public:
  /**
   * Konstruktor für den LED-Manager.
   * 
   * @param rPin Roter LED-Pin
   * @param gPin Grüner LED-Pin
   * @param bPin Blauer LED-Pin
   */
  LedManager(int rPin, int gPin, int bPin) 
    : redPin(rPin), greenPin(gPin), bluePin(bPin),
      currentRed(0), currentGreen(0), currentBlue(0),
      blinkEnabled(false), blinkState(false), lastBlinkTime(0), blinkInterval(500),
      pulseEnabled(false), pulseValue(0), pulseDirection(1), lastPulseTime(0) {
  }
  
  /**
   * Initialisiert die LED-Pins.
   */
  void begin() {
    pinMode(redPin, OUTPUT);
    pinMode(greenPin, OUTPUT);
    pinMode(bluePin, OUTPUT);
    
    // Initial ausschalten
    digitalWrite(redPin, LOW);
    digitalWrite(greenPin, LOW);
    digitalWrite(bluePin, LOW);
  }
  
  /**
   * Setzt die LED-Farbe.
   * 
   * @param red Rotwert (0-255)
   * @param green Grünwert (0-255)
   * @param blue Blauwert (0-255)
   */
  void setColor(uint8_t red, uint8_t green, uint8_t blue) {
    currentRed = red;
    currentGreen = green;
    currentBlue = blue;
    
    // Blink und Pulse deaktivieren
    blinkEnabled = false;
    pulseEnabled = false;
    
    updateLeds();
  }
  
  /**
   * Vordefinierte Farben setzen.
   * 
   * @param colorCode 0: aus, 1: rot, 2: grün, 3: blau, 4: gelb, 5: türkis, 6: magenta, 7: weiß
   */
  void setPresetColor(uint8_t colorCode) {
    switch (colorCode) {
      case 0: // Aus
        setColor(0, 0, 0);
        break;
      case 1: // Rot
        setColor(255, 0, 0);
        break;
      case 2: // Grün
        setColor(0, 255, 0);
        break;
      case 3: // Blau
        setColor(0, 0, 255);
        break;
      case 4: // Gelb
        setColor(255, 255, 0);
        break;
      case 5: // Türkis
        setColor(0, 255, 255);
        break;
      case 6: // Magenta
        setColor(255, 0, 255);
        break;
      case 7: // Weiß
        setColor(255, 255, 255);
        break;
      default:
        setColor(0, 0, 0);
    }
  }
  
  /**
   * Aktiviert den Blink-Modus.
   * 
   * @param interval Intervall in Millisekunden
   */
  void enableBlink(unsigned long interval = 500) {
    blinkEnabled = true;
    pulseEnabled = false;
    blinkInterval = interval;
    lastBlinkTime = millis();
    blinkState = true;
    
    updateLeds();
  }
  
  /**
   * Aktiviert den Puls-Modus.
   * Die LED pulsiert in der eingestellten Farbe.
   */
  void enablePulse() {
    blinkEnabled = false;
    pulseEnabled = true;
    pulseValue = 0;
    pulseDirection = 1;
    lastPulseTime = millis();
    
    updateLeds();
  }
  
  /**
   * Aktualisiert die LEDs basierend auf dem aktuellen Modus.
   * Muss regelmäßig aufgerufen werden, wenn Blink oder Puls aktiv ist.
   */
  void update() {
    if (blinkEnabled) {
      unsigned long currentTime = millis();
      
      if (currentTime - lastBlinkTime >= blinkInterval) {
        lastBlinkTime = currentTime;
        blinkState = !blinkState;
        updateLeds();
      }
    }
    
    if (pulseEnabled) {
      unsigned long currentTime = millis();
      
      if (currentTime - lastPulseTime >= 10) { // Alle 10ms aktualisieren
        lastPulseTime = currentTime;
        
        // Pulsrichtung ändern, wenn Grenzen erreicht
        if (pulseValue >= 255) {
          pulseDirection = -1;
        } else if (pulseValue <= 0) {
          pulseDirection = 1;
        }
        
        // Pulswert aktualisieren
        pulseValue += pulseDirection * 5;
        
        // Auf gültigen Bereich begrenzen
        if (pulseValue > 255) pulseValue = 255;
        if (pulseValue < 0) pulseValue = 0;
        
        updateLeds();
      }
    }
  }
  
  /**
   * Ruft die aktuelle Farbe ab.
   * 
   * @param red Zeiger auf Variable für Rotwert
   * @param green Zeiger auf Variable für Grünwert
   * @param blue Zeiger auf Variable für Blauwert
   */
  void getColor(uint8_t *red, uint8_t *green, uint8_t *blue) {
    *red = currentRed;
    *green = currentGreen;
    *blue = currentBlue;
  }
  
protected:
  /**
   * Aktualisiert die physischen LEDs.
   */
  void updateLeds() {
    if (blinkEnabled && !blinkState) {
      // Im Blink-Modus und aktuell aus
      analogWrite(redPin, 0);
      analogWrite(greenPin, 0);
      analogWrite(bluePin, 0);
    } else if (pulseEnabled) {
      // Im Puls-Modus
      float factor = pulseValue / 255.0;
      analogWrite(redPin, currentRed * factor);
      analogWrite(greenPin, currentGreen * factor);
      analogWrite(bluePin, currentBlue * factor);
    } else {
      // Normaler Modus
      analogWrite(redPin, currentRed);
      analogWrite(greenPin, currentGreen);
      analogWrite(bluePin, currentBlue);
    }
  }
};

#endif // LEDS_H
#ifndef LEDS_H
#define LEDS_H

// LED-Zustände
enum LedState {
  LED_OFF,
  LED_NORMAL,
  LED_PROBLEM,
  LED_PROGRAM_ACTIVE
};

/**
 * Setzt die RGB-LED auf einen bestimmten Zustand.
 * 
 * @param redPin Pin für rote Komponente
 * @param greenPin Pin für grüne Komponente
 * @param bluePin Pin für blaue Komponente
 * @param state Zustand der LED (siehe LedState)
 */
void setRgbLed(int redPin, int greenPin, int bluePin, LedState state) {
  switch (state) {
    case LED_NORMAL:
      // Grün (Normalbetrieb)
      digitalWrite(redPin, LOW);
      digitalWrite(greenPin, HIGH);
      digitalWrite(bluePin, LOW);
      break;
      
    case LED_PROBLEM:
      // Rot (Fehler)
      digitalWrite(redPin, HIGH);
      digitalWrite(greenPin, LOW);
      digitalWrite(bluePin, LOW);
      break;
      
    case LED_PROGRAM_ACTIVE:
      // Blau (Programm aktiv)
      digitalWrite(redPin, LOW);
      digitalWrite(greenPin, LOW);
      digitalWrite(bluePin, HIGH);
      break;
      
    case LED_OFF:
    default:
      // Alle LEDs aus
      digitalWrite(redPin, LOW);
      digitalWrite(greenPin, LOW);
      digitalWrite(bluePin, LOW);
      break;
  }
}

/**
 * Lässt die RGB-LED blinken.
 * 
 * @param redPin Pin für rote Komponente
 * @param greenPin Pin für grüne Komponente
 * @param bluePin Pin für blaue Komponente
 * @param state Zustand der LED (siehe LedState)
 * @param blinkCount Anzahl der Blinkvorgänge
 * @param blinkDelay Verzögerung zwischen An- und Ausschalten in ms
 */
void blinkRgbLed(int redPin, int greenPin, int bluePin, LedState state, int blinkCount, int blinkDelay) {
  for (int i = 0; i < blinkCount; i++) {
    setRgbLed(redPin, greenPin, bluePin, state);
    delay(blinkDelay);
    setRgbLed(redPin, greenPin, bluePin, LED_OFF);
    delay(blinkDelay);
  }
}

/**
 * Pulsiert die RGB-LED (weicher Übergang von aus zu an und zurück).
 * 
 * @param redPin Pin für rote Komponente
 * @param greenPin Pin für grüne Komponente
 * @param bluePin Pin für blaue Komponente
 * @param state Zustand der LED (siehe LedState)
 * @param pulseCount Anzahl der Pulsvorgänge
 */
void pulseRgbLed(int redPin, int greenPin, int bluePin, LedState state, int pulseCount) {
  // Pins für PWM vorbereiten
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  
  // Farbwerte je nach Zustand festlegen
  int redValue = 0;
  int greenValue = 0;
  int blueValue = 0;
  
  switch (state) {
    case LED_NORMAL:
      greenValue = 255;
      break;
    case LED_PROBLEM:
      redValue = 255;
      break;
    case LED_PROGRAM_ACTIVE:
      blueValue = 255;
      break;
    default:
      break;
  }
  
  // Pulsieren durchführen
  for (int p = 0; p < pulseCount; p++) {
    // Aufleuchten
    for (int i = 0; i <= 255; i += 5) {
      analogWrite(redPin, map(i, 0, 255, 0, redValue));
      analogWrite(greenPin, map(i, 0, 255, 0, greenValue));
      analogWrite(bluePin, map(i, 0, 255, 0, blueValue));
      delay(10);
    }
    
    // Abklingen
    for (int i = 255; i >= 0; i -= 5) {
      analogWrite(redPin, map(i, 0, 255, 0, redValue));
      analogWrite(greenPin, map(i, 0, 255, 0, greenValue));
      analogWrite(bluePin, map(i, 0, 255, 0, blueValue));
      delay(10);
    }
  }
  
  // Pins zurücksetzen auf digitale Ausgabe
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
}

#endif // LEDS_H

#ifndef PROGRAMS_H
#define PROGRAMS_H

// Programmkonfigurationen
struct ProgramConfig {
  String name;
  unsigned long durationMs;
  int motorPower; // Optional: Motorleistung in Prozent, falls steuerbar
  bool useSensor; // Optional: Ob Sensor für dieses Programm verwendet werden soll
};

// Vordefinierte Programme
const ProgramConfig PROGRAM_1 = {
  "Programm 1", // 7 Tage
  7 * 24 * 60 * 60 * 1000, // 7 Tage in ms
  100, // Motorleistung 100%
  true // Sensor verwenden
};

const ProgramConfig PROGRAM_2 = {
  "Programm 2", // 14 Tage
  14 * 24 * 60 * 60 * 1000, // 14 Tage in ms
  100, // Motorleistung 100%
  true // Sensor verwenden
};

const ProgramConfig PROGRAM_3 = {
  "Programm 3", // 21 Tage
  21 * 24 * 60 * 60 * 1000, // 21 Tage in ms
  100, // Motorleistung 100%
  true // Sensor verwenden
};

const ProgramConfig PROGRAM_4 = {
  "Programm 4", // Individuell einstellbar
  0, // Dauer muss später gesetzt werden
  100, // Motorleistung 100%
  true // Sensor verwenden
};

// Maximum 12 Stunden für Debugging und Tests
const ProgramConfig PROGRAM_DEBUG = {
  "Debug Modus",
  12 * 60 * 60 * 1000, // 12 Stunden in ms
  50, // Halbierte Motorleistung für Tests
  false // Sensor ignorieren
};

// Array mit allen vordefinierten Programmen
const ProgramConfig PREDEFINED_PROGRAMS[] = {
  PROGRAM_1,
  PROGRAM_2,
  PROGRAM_3,
  PROGRAM_4,
  PROGRAM_DEBUG
};

// Anzahl der vordefinierten Programme
const int NUM_PREDEFINED_PROGRAMS = sizeof(PREDEFINED_PROGRAMS) / sizeof(ProgramConfig);

/**
 * Formatiert eine Zeitdauer in Millisekunden zu einem lesbaren String.
 * 
 * @param durationMs Dauer in Millisekunden
 * @return Formatierter String (z.B. "14 Tage 2 Std 30 Min")
 */
String formatDuration(unsigned long durationMs) {
  // Bei Individueller Dauer (0 ms)
  if (durationMs == 0) {
    return "Individuell";
  }
  
  // Umrechnung in Tage, Stunden, Minuten, Sekunden
  unsigned long seconds = durationMs / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  seconds %= 60;
  minutes %= 60;
  hours %= 24;
  
  String result = "";
  
  if (days > 0) {
    result += String(days) + " Tage ";
  }
  
  if (hours > 0 || days > 0) {
    result += String(hours) + " Std ";
  }
  
  if (minutes > 0 || hours > 0 || days > 0) {
    result += String(minutes) + " Min";
  } else {
    result += String(seconds) + " Sek";
  }
  
  return result;
}

/**
 * Berechnet die verbleibende Zeit eines laufenden Programms.
 * 
 * @param startTimeMs Startzeit in Millisekunden (von millis())
 * @param durationMs Gesamtdauer des Programms in Millisekunden
 * @return Verbleibende Zeit in Millisekunden (0, wenn das Programm abgelaufen ist)
 */
unsigned long getRemainingTime(unsigned long startTimeMs, unsigned long durationMs) {
  // Bei Individueller Dauer (0 ms) gibt es keine Restzeit
  if (durationMs == 0) {
    return 0;
  }
  
  unsigned long currentTimeMs = millis();
  unsigned long elapsedTimeMs = currentTimeMs - startTimeMs;
  
  // Verhindern von Überläufen, wenn millis() zurückgesetzt wurde
  if (elapsedTimeMs > durationMs) {
    return 0;
  }
  
  return durationMs - elapsedTimeMs;
}

/**
 * Berechnet den Fortschritt eines laufenden Programms in Prozent.
 * 
 * @param startTimeMs Startzeit in Millisekunden (von millis())
 * @param durationMs Gesamtdauer des Programms in Millisekunden
 * @return Fortschritt in Prozent (0-100, 100 wenn das Programm abgelaufen ist)
 */
int getProgressPercent(unsigned long startTimeMs, unsigned long durationMs) {
  // Bei Individueller Dauer (0 ms) gibt es keinen prozentualen Fortschritt
  if (durationMs == 0) {
    return 0;
  }
  
  unsigned long currentTimeMs = millis();
  unsigned long elapsedTimeMs = currentTimeMs - startTimeMs;
  
  // Verhindern von Überläufen, wenn millis() zurückgesetzt wurde
  if (elapsedTimeMs > durationMs) {
    return 100;
  }
  
  return (int)((elapsedTimeMs * 100) / durationMs);
}

/**
 * Setzt die Motorgeschwindigkeit basierend auf dem Programm.
 * 
 * @param motorPin Pin des Motors
 * @param powerPercent Leistung in Prozent (0-100)
 * @param isPwmCapable Ob der Motor PWM-fähig ist (true) oder nur ein-/ausgeschaltet werden kann (false)
 */
void setMotorPower(int motorPin, int powerPercent, bool isPwmCapable) {
  if (isPwmCapable) {
    // PWM-Steuerung für variable Geschwindigkeit
    int pwmValue = map(powerPercent, 0, 100, 0, 255);
    analogWrite(motorPin, pwmValue);
  } else {
    // Einfache Ein/Aus-Steuerung
    if (powerPercent > 0) {
      digitalWrite(motorPin, HIGH);
    } else {
      digitalWrite(motorPin, LOW);
    }
  }
}

#endif // PROGRAMS_H

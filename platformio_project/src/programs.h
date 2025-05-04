#ifndef PROGRAMS_H
#define PROGRAMS_H

#include <Arduino.h>

/**
 * Struktur zur Definition eines Desinfektionsprogramms.
 */
struct Program {
  const char* name;        // Programmname
  uint32_t duration;       // Dauer in Sekunden
  uint8_t motorPower;      // Motorleistung in Prozent (0-100)
  bool customizable;       // Ob Dauer anpassbar ist
  const char* description; // Programmbeschreibung
};

/**
 * Manager für die Desinfektionsprogramme.
 * Verwaltet die vordefinierten Programme und ermöglicht auch
 * benutzerdefinierte Programme.
 */
class ProgramManager {
private:
  // Vordefinierte Programme
  Program programs[4];
  
  // Individuelles Programm (für Anpassungen)
  uint32_t customDays;

public:
  /**
   * Konstruktor für den Program Manager.
   * Initializer mit den Standard-Programmen.
   */
  ProgramManager() : customDays(7) {
    // Programm 1: 7 Tage
    programs[0] = {
      "7-Tage-Programm",
      7 * 24 * 60 * 60,   // 7 Tage in Sekunden
      100,                // 100% Motorleistung
      false,              // Nicht anpassbar
      "Standard-Desinfektion für normale Nutzung"
    };
    
    // Programm 2: 14 Tage
    programs[1] = {
      "14-Tage-Programm",
      14 * 24 * 60 * 60,  // 14 Tage in Sekunden
      100,                // 100% Motorleistung
      false,              // Nicht anpassbar
      "Erweiterte Desinfektion für mittlere Nutzung"
    };
    
    // Programm 3: 21 Tage
    programs[2] = {
      "21-Tage-Programm",
      21 * 24 * 60 * 60,  // 21 Tage in Sekunden
      100,                // 100% Motorleistung
      false,              // Nicht anpassbar
      "Intensive Desinfektion für starke Nutzung"
    };
    
    // Programm 4: Individuell
    programs[3] = {
      "Individuelles Programm",
      customDays * 24 * 60 * 60,  // Individuelle Tage in Sekunden
      100,                // 100% Motorleistung
      true,               // Anpassbar
      "Benutzerdefinierte Dauer für spezielle Anforderungen"
    };
  }
  
  /**
   * Gibt die Anzahl der verfügbaren Programme zurück.
   * 
   * @return Anzahl der Programme
   */
  int getProgramCount() {
    return 4; // Feste Anzahl von 4 Programmen
  }
  
  /**
   * Gibt ein bestimmtes Programm zurück.
   * 
   * @param index Programmindex (0-3)
   * @return Referenz auf das Programm
   */
  Program& getProgram(int index) {
    if (index >= 0 && index < 4) {
      // Für das individuelle Programm die aktuell eingestellte Dauer verwenden
      if (index == 3) {
        programs[3].duration = customDays * 24 * 60 * 60;
      }
      return programs[index];
    }
    // Fallback auf Programm 1
    return programs[0];
  }
  
  /**
   * Aktualisiert die Dauer des individuellen Programms.
   * 
   * @param days Anzahl der Tage
   */
  void setCustomDays(uint32_t days) {
    customDays = days;
    programs[3].duration = customDays * 24 * 60 * 60;
  }
  
  /**
   * Gibt die eingestellte Anzahl der Tage für das individuelle Programm zurück.
   * 
   * @return Anzahl der eingestellten Tage
   */
  uint32_t getCustomDays() {
    return customDays;
  }
  
  /**
   * Formatiert die Programmdauer für die Anzeige.
   * 
   * @param seconds Dauer in Sekunden
   * @return Formatierter String (z.B. "7 Tage")
   */
  String formatDuration(uint32_t seconds) {
    uint32_t days = seconds / (24 * 60 * 60);
    return String(days) + " Tage";
  }
  
  /**
   * Formatiert die verbleibende Zeit für die Anzeige.
   * 
   * @param seconds Verbleibende Zeit in Sekunden
   * @return Formatierter String (z.B. "6 Tage 23 Std 45 Min")
   */
  String formatRemainingTime(uint32_t seconds) {
    uint32_t days = seconds / (24 * 60 * 60);
    seconds %= (24 * 60 * 60);
    uint32_t hours = seconds / (60 * 60);
    seconds %= (60 * 60);
    uint32_t minutes = seconds / 60;
    
    return String(days) + " Tage " + String(hours) + " Std " + String(minutes) + " Min";
  }
};

#endif // PROGRAMS_H
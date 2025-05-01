#ifndef MENU_H
#define MENU_H

// Menü Status Definition
enum MenuState {
    START_SCREEN,
    MAIN_MENU,
    SETUP_14_DAYS_CONFIRM,
    SETUP_21_DAYS_CONFIRM,
    SETUP_NEW_DAYS_INIT,
    SETUP_NEW_DAYS_ADJUST,
    SETUP_CONFIRMATION,
    PROGRAM_RUNNING,
    PROGRAM_COMPLETED,
    ERROR_TANK_LOW
};

// Tastenlogik-Zustände für Programmwechsel
enum ButtonState {
    IDLE,
    SHORT_PRESS_DETECTED,
    WAITING_FOR_LONG_PRESS
};

/**
 * Menüeintrag für die Menüstruktur.
 */
struct MenuItem {
    String label;
    MenuState nextState;
};

// Definierte Menüs
const MenuItem MAIN_MENU_ITEMS[] = {
    {"14 Tage Programm", SETUP_14_DAYS_CONFIRM},
    {"21 Tage Programm", SETUP_21_DAYS_CONFIRM},
    {"Individuelles Programm", SETUP_NEW_DAYS_INIT}
};

const int MAIN_MENU_ITEMS_COUNT = sizeof(MAIN_MENU_ITEMS) / sizeof(MenuItem);

/**
 * Rendert ein Menü auf dem Display
 * 
 * @param display Das OLED-Display-Objekt
 * @param items Array von Menüeinträgen
 * @param itemCount Anzahl der Einträge im Array
 * @param selectedIndex Index des aktuell ausgewählten Eintrags
 * @param startY Y-Position, an der das Menü beginnen soll
 */
void renderMenu(Adafruit_SSD1306 &display, const MenuItem items[], int itemCount, int selectedIndex, int startY) {
    display.setTextSize(1);
    
    for (int i = 0; i < itemCount; i++) {
        display.setCursor(0, startY + (i * 10));
        
        if (i == selectedIndex) {
            display.print(F("-> "));
        } else {
            display.print(F("   "));
        }
        
        display.println(items[i].label);
    }
}

/**
 * Rendert einen Bestätigungsbildschirm
 * 
 * @param display Das OLED-Display-Objekt
 * @param title Titel des Bildschirms
 * @param message Nachricht/Frage
 * @param confirmLabel Text für Bestätigung
 * @param cancelLabel Text für Abbrechen
 */
void renderConfirmation(Adafruit_SSD1306 &display, const String &title, const String &message, 
                         const String &confirmLabel, const String &cancelLabel) {
    display.setTextSize(1);
    
    // Titel
    display.setCursor(0, 0);
    display.println(title);
    display.drawLine(0, 8, display.width() - 1, 8, WHITE);
    
    // Nachricht
    display.setCursor(0, 12);
    display.println(message);
    
    // Bestätigungsoptionen
    display.setCursor(0, 40);
    display.print(F("+ Taste: "));
    display.println(confirmLabel);
    
    display.setCursor(0, 50);
    display.print(F("- Taste: "));
    display.println(cancelLabel);
}

/**
 * Zeichnet einen Zahlenauswähler für numerische Einstellungen.
 * 
 * @param display Das OLED-Display-Objekt
 * @param title Titel des Bildschirms
 * @param value Aktueller Wert
 * @param unit Einheit (z.B. "Tage", "Stunden")
 * @param x X-Position
 * @param y Y-Position
 * @param minValue Minimaler erlaubter Wert
 * @param maxValue Maximaler erlaubter Wert
 */
void renderNumberSelector(Adafruit_SSD1306 &display, const String &title, int value, const String &unit,
                          int x, int y, int minValue, int maxValue) {
    display.setTextSize(1);
    
    // Titel
    display.setCursor(x, y);
    display.println(title);
    
    // Wert und Einheit mit größerer Schrift
    display.setTextSize(2);
    
    // Berechne Breite des Wertes für Zentrierung
    int16_t x1, y1;
    uint16_t w, h;
    String valueStr = String(value) + " " + unit;
    display.getTextBounds(valueStr, 0, 0, &x1, &y1, &w, &h);
    
    display.setCursor(x + (display.width() - w) / 2, y + 20);
    display.print(valueStr);
    
    // Pfeile für +/- anzeigen
    display.setTextSize(1);
    
    // "+" erlaubt?
    if (value < maxValue) {
        display.setCursor(x + 5, y + 20);
        display.print(F("< +"));
    }
    
    // "-" erlaubt?
    if (value > minValue) {
        display.setCursor(display.width() - 15, y + 20);
        display.print(F("- >"));
    }
}

/**
 * Zeichnet einen Laufzeitbildschirm für das aktive Programm.
 * 
 * @param display Das OLED-Display-Objekt
 * @param programName Name des aktiven Programms
 * @param elapsedTime Abgelaufene Zeit in Millisekunden
 * @param totalDuration Gesamtdauer in Millisekunden (0 für unbestimmte Dauer)
 */
void renderRunningProgram(Adafruit_SSD1306 &display, const String &programName, 
                          unsigned long elapsedTime, unsigned long totalDuration) {
    display.setTextSize(1);
    
    // Titel
    display.setCursor(0, 0);
    display.println(F("Programm aktiv"));
    display.drawLine(0, 8, display.width() - 1, 8, WHITE);
    
    // Programmname
    display.setCursor(0, 12);
    display.println(programName);
    
    // Berechne und zeige Restzeit
    display.setCursor(0, 22);
    display.println(F("Verbleibende Zeit:"));
    
    if (totalDuration > 0) {
        // Restzeit berechnen
        unsigned long remainingTime = totalDuration - elapsedTime;
        if (remainingTime > totalDuration) { // Überlaufschutz
            remainingTime = 0;
        }
        
        // Zeit in Tage, Stunden, Minuten umrechnen
        unsigned long seconds = remainingTime / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        unsigned long days = hours / 24;
        
        hours %= 24;
        minutes %= 60;
        
        // Formatierte Zeit anzeigen
        String timeStr = "";
        if (days > 0) {
            timeStr += String(days) + "T ";
        }
        timeStr += String(hours) + "Std " + String(minutes) + "Min";
        
        display.setCursor(0, 32);
        display.println(timeStr);
        
        // Fortschrittsbalken
        int progress = (int)((elapsedTime * 100) / totalDuration);
        if (progress > 100) progress = 100;
        
        display.drawRect(0, 45, display.width(), 10, WHITE);
        display.fillRect(0, 45, (display.width() * progress) / 100, 10, WHITE);
        
        // Prozentanzeige
        display.setCursor(0, 56);
        display.print(progress);
        display.print(F("% abgeschlossen"));
    } else {
        // Für Individuelles Programm ohne feste Laufzeit
        display.setCursor(0, 32);
        display.println(F("Individuell"));
        
        // Laufzeit anzeigen
        unsigned long seconds = elapsedTime / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        
        minutes %= 60;
        seconds %= 60;
        
        String runTime = String(hours) + ":" + 
                        (minutes < 10 ? "0" : "") + String(minutes) + ":" + 
                        (seconds < 10 ? "0" : "") + String(seconds);
                        
        display.setCursor(0, 45);
        display.print(F("Laufzeit: "));
        display.println(runTime);
    }
}

/**
 * Zeichnet den Fertigstellungsbildschirm nach Abschluss des Programms.
 * 
 * @param display Das OLED-Display-Objekt
 * @param programName Name des abgeschlossenen Programms
 */
void renderCompletedProgram(Adafruit_SSD1306 &display, const String &programName) {
    display.setTextSize(1);
    
    // Titel
    display.setCursor(0, 0);
    display.println(F("Programm abgeschlossen"));
    display.drawLine(0, 8, display.width() - 1, 8, WHITE);
    
    // Programmname
    display.setCursor(0, 12);
    display.print(F("Programm: "));
    display.println(programName);
    
    // Erfolgsmeldung
    display.setCursor(0, 25);
    display.println(F("Desinfektion erfolgreich"));
    display.setCursor(0, 35);
    display.println(F("abgeschlossen!"));
    
    // Anweisungen
    display.setCursor(0, 50);
    display.println(F("Taste druecken fuer Neustart"));
}

/**
 * Zeichnet einen Fehlerbildschirm.
 * 
 * @param display Das OLED-Display-Objekt
 * @param errorTitle Fehlertitel
 * @param errorMessage Fehlermeldung
 * @param instruction Anweisung zur Fehlerbehebung
 */
void renderErrorScreen(Adafruit_SSD1306 &display, const String &errorTitle, 
                       const String &errorMessage, const String &instruction) {
    display.setTextSize(1);
    
    // Titel
    display.setCursor(0, 0);
    display.println(errorTitle);
    display.drawLine(0, 8, display.width() - 1, 8, WHITE);
    
    // Fehlermeldung
    display.setCursor(0, 15);
    display.println(errorMessage);
    
    // Anweisung
    display.setCursor(0, 35);
    display.println(instruction);
}

#endif // MENU_H

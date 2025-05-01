#ifndef DISPLAY_H
#define DISPLAY_H

#include <Adafruit_SSD1306.h>

// Fortschrittsbalken-Typen
enum ProgressBarType {
  BAR_HORIZONTAL,
  BAR_VERTICAL,
  BAR_CIRCULAR
};

/**
 * Zeichnet einen Fortschrittsbalken auf dem Display.
 * 
 * @param display Das OLED-Display-Objekt
 * @param x X-Position des Balkens
 * @param y Y-Position des Balkens
 * @param width Breite des Balkens (für horizontal/circular)
 * @param height Höhe des Balkens (für vertical/circular)
 * @param progress Fortschritt in Prozent (0-100)
 * @param type Typ des Fortschrittsbalkens (horizontal, vertical, circular)
 */
void drawProgressBar(Adafruit_SSD1306 &display, int x, int y, int width, int height, int progress, ProgressBarType type) {
  // Fortschritt auf gültigen Bereich begrenzen
  if (progress < 0) progress = 0;
  if (progress > 100) progress = 100;
  
  switch (type) {
    case BAR_HORIZONTAL:
      // Äußerer Rahmen
      display.drawRect(x, y, width, height, WHITE);
      
      // Fortschrittsbalken
      int progressWidth = (progress * (width - 2)) / 100;
      display.fillRect(x + 1, y + 1, progressWidth, height - 2, WHITE);
      break;
      
    case BAR_VERTICAL:
      // Äußerer Rahmen
      display.drawRect(x, y, width, height, WHITE);
      
      // Fortschrittsbalken
      int progressHeight = (progress * (height - 2)) / 100;
      display.fillRect(x + 1, y + height - 1 - progressHeight, width - 2, progressHeight, WHITE);
      break;
      
    case BAR_CIRCULAR:
      // Äußerer Kreis
      int radius = min(width, height) / 2;
      display.drawCircle(x + width / 2, y + height / 2, radius, WHITE);
      
      // Fortschrittskreis als Kreissegmente
      int segments = 36; // Auflösung des Kreises
      int filledSegments = (progress * segments) / 100;
      
      for (int i = 0; i < filledSegments; i++) {
        float angle = i * 2 * PI / segments;
        float nextAngle = (i + 1) * 2 * PI / segments;
        
        int x1 = x + width / 2;
        int y1 = y + height / 2;
        int x2 = x + width / 2 + cos(angle) * radius;
        int y2 = y + height / 2 + sin(angle) * radius;
        int x3 = x + width / 2 + cos(nextAngle) * radius;
        int y3 = y + height / 2 + sin(nextAngle) * radius;
        
        display.drawLine(x1, y1, x2, y2, WHITE);
        display.drawLine(x1, y1, x3, y3, WHITE);
        display.drawLine(x2, y2, x3, y3, WHITE);
      }
      break;
  }
}

/**
 * Zeichnet einen Titel mit einer Linie darunter auf dem Display.
 * 
 * @param display Das OLED-Display-Objekt
 * @param title Der anzuzeigende Titel
 * @param y Y-Position des Titels
 * @param size Textgröße (1-3)
 */
void drawTitleWithLine(Adafruit_SSD1306 &display, const String &title, int y, int size) {
  display.setTextSize(size);
  display.setCursor(0, y);
  display.println(title);
  
  // Höhe einer Textzeile basierend auf Größe berechnen
  int lineHeight = 8 * size; 
  
  // Linie unter dem Titel zeichnen
  display.drawLine(0, y + lineHeight, display.width() - 1, y + lineHeight, WHITE);
}

/**
 * Zeichnet einen scrollenden Text auf dem Display.
 * 
 * @param display Das OLED-Display-Objekt
 * @param text Der anzuzeigende Text
 * @param x X-Position des Textes
 * @param y Y-Position des Textes
 * @param width Verfügbare Breite für den Text
 * @param size Textgröße (1-3)
 * @param position Aktuelle Scrollposition
 * @return Maximale Textbreite (nützlich für Scrollberechnungen)
 */
int drawScrollingText(Adafruit_SSD1306 &display, const String &text, int x, int y, int width, int size, int position) {
  display.setTextSize(size);
  
  // Textbegrenzungen abrufen, um Breite zu ermitteln
  int16_t x1, y1;
  uint16_t textWidth, textHeight;
  display.getTextBounds(text, 0, 0, &x1, &y1, &textWidth, &textHeight);
  
  // Clipping-Fenster erstellen
  display.setCursor(x + position, y);
  
  // Text anzeigen
  display.print(text);
  
  return textWidth;
}

/**
 * Zeichnet eine Statusanzeige mit Icon.
 * 
 * @param display Das OLED-Display-Objekt
 * @param label Beschriftung der Anzeige
 * @param value Anzuzeigender Wert
 * @param x X-Position der Anzeige
 * @param y Y-Position der Anzeige
 * @param type Typ des Status-Icons (0: OK, 1: Warning, 2: Error)
 */
void drawStatusIndicator(Adafruit_SSD1306 &display, const String &label, const String &value, int x, int y, int type) {
  display.setTextSize(1);
  
  // Label zeichnen
  display.setCursor(x + 12, y);
  display.print(label);
  
  // Wert zeichnen
  display.setCursor(x + 12, y + 9);
  display.print(value);
  
  // Icon je nach Typ zeichnen
  switch (type) {
    case 0: // OK (Häkchen)
      display.drawLine(x + 2, y + 5, x + 4, y + 7, WHITE);
      display.drawLine(x + 4, y + 7, x + 8, y + 3, WHITE);
      break;
      
    case 1: // Warning (Ausrufezeichen)
      display.drawChar(x + 3, y + 2, '!', WHITE, BLACK, 1);
      break;
      
    case 2: // Error (X)
      display.drawLine(x + 2, y + 2, x + 8, y + 8, WHITE);
      display.drawLine(x + 2, y + 8, x + 8, y + 2, WHITE);
      break;
  }
}

/**
 * Zeichnet eine einfache Animation (z.B. für Ladebildschirm).
 * 
 * @param display Das OLED-Display-Objekt
 * @param x X-Position der Animation
 * @param y Y-Position der Animation
 * @param size Größe der Animation
 * @param frame Aktueller Frame (0-7 für 8 Frames)
 */
void drawAnimation(Adafruit_SSD1306 &display, int x, int y, int size, int frame) {
  frame = frame % 8; // 8 Frames für die Animation
  
  // Äußerer Kreis
  display.drawCircle(x, y, size, WHITE);
  
  // Drehender Indikator
  float angle = frame * PI / 4; // 8 Positionen (0 bis 7) * 45 Grad
  int xpos = x + sin(angle) * size;
  int ypos = y - cos(angle) * size;
  
  display.fillCircle(xpos, ypos, size / 4, WHITE);
}

#endif // DISPLAY_H

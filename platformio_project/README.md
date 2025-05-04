# SwissAirDry PlatformIO Projekt

Dies ist die PlatformIO-Version des SwissAirDry Desinfektionseinheit-Projekts.

## Installation

1. PlatformIO in VSCode installieren
2. Dieses Projekt öffnen
3. `pio run` ausführen, um zu kompilieren
4. `pio run -t upload` ausführen, um auf das Gerät zu übertragen

## Struktur

- `platformio.ini` - PlatformIO-Konfiguration mit allen erforderlichen Einstellungen
- `include/lv_conf.h` - LVGL-Konfigurationsdatei
- `src/` - Quellcode-Dateien
  - `main.cpp` - Hauptprogramm
  - `mqtt_communication.h` - MQTT-Client für IoT-Funktionalität
  - `rest_api.h` - REST API für externe Steuerung
  - `wifi_manager.h` - WiFi-Verbindungsmanager
  - `display.h` - Display-Funktionen und UI-Komponenten
  - `programs.h` - Desinfektionsprogramme
  - `leds.h` - LED-Statusanzeige
  - `menu.h` - Menüsystem

## Vorteile gegenüber Arduino IDE

- **Automatische Bibliotheksverwaltung**: Alle Abhängigkeiten werden in der `platformio.ini` definiert und automatisch installiert
- **Keine manuellen TFT_eSPI-Anpassungen**: Die Konfiguration erfolgt direkt über Build-Flags in `platformio.ini`
- **Versionskontrolle der Bibliotheken**: Spezifische Versionen werden gesperrt, um Kompatibilitätsprobleme zu vermeiden
- **Bessere Fehlerbehandlung**: Verbesserte Kompilierungsfehler mit präziseren Meldungen
- **Optimierte Build-Tools**: Schnellere Kompilierung und Optimierung

## Debugging

PlatformIO unterstützt erweiterte Debugging-Funktionen:

1. Debugging über serielle Schnittstelle
   - `pio device monitor` zum Anzeigen der seriellen Ausgabe
   - CORE_DEBUG_LEVEL definiert in platformio.ini für detaillierte Logs

2. Onboard-Debugging mit JTAG
   - `pio debug` für Echtzeitdebugging
   - Breakpoints, Variableninspektionen und Schrittverfolgung möglich

## Problembehebung bei LVGL

Sollte es zu Problemen mit LVGL kommen:

1. Überprüfen Sie, ob `lv_conf.h` in `include/` liegt
2. Verwenden Sie die richtige LVGL-Version (8.3.7)
3. Reduzieren Sie ggf. die Buffergröße in `main.cpp`, falls der Speicher nicht ausreicht
4. Überprüfen Sie die Pin-Definitionen in den Build-Flags von `platformio.ini`

Mit PlatformIO sollten die LVGL-Kompilierungsprobleme, die in der Arduino IDE aufgetreten sind, nicht mehr vorkommen.
# SwissAirDry Desinfektionseinheit

Embedded-Steuerungssystem für eine Desinfektionseinheit mit mehreren Programmen, LED-Statusanzeige und OLED-Display-Menü.

![Hauptbildschirm](generated-icon.png)

## Funktionen

- **Touchscreen-Interface** mit intuitivem Menüsystem
- **Mehrere Desinfektionsprogramme**: 7, 14, 21 Tage und individuell anpassbar
- **Fernsteuerung** via MQTT und REST API
- **Tankfüllstandsüberwachung** mit automatischer Fehlerbehandlung
- **WLAN-Konnektivität** mit Access Point Fallback

![Programmbildschirm](attached_assets/S433851bede1a4c3793861b5fb643d6f3s.avif)

## Hardware

- ESP32-S3-Touch-LCD-4.3B
- 4,3-Zoll-Touchscreen (800x480)
- RGB-LEDs für Statusanzeige
- Motor/Relais-Steuerung
- Tankfüllstandssensor

![Einstellungsbildschirm](attached_assets/S56070200f8a4446fb06db8e067c650405.avif)

## Remote-Steuerung

Fernüberwachung und -steuerung über:
- MQTT für IoT-Integration
- REST API für App-Anbindung
- Nextcloud AppAPI-Integration möglich

![Programmfortschritt](attached_assets/S6a71f53db4d6477595281e14980d78e4r.avif)

## Installation

PlatformIO:
```bash
pio run -t upload
```

Arduino IDE:
1. Bibliotheken installieren (LVGL, TFT_eSPI, etc.)
2. TFT_eSPI für ESP32-S3 konfigurieren
3. Sketch hochladen

![Erfolgsmeldung](attached_assets/S8f152c0235a345998b7c792cc6863d95q.avif)
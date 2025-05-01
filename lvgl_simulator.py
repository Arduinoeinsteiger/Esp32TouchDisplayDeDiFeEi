#!/usr/bin/env python3
"""
LVGL Desinfektionseinheit Simulator

Dieses Skript simuliert die GUI der Desinfektionseinheit mit ESP32-S3 und LVGL,
indem es eine Web-basierte Vorschau der Benutzeroberfläche erzeugt.
"""

import http.server
import socketserver
import json
import time
import threading
import os
from http import HTTPStatus
import io
import base64
from PIL import Image, ImageDraw, ImageFont

# Grundeinstellungen
PORT = 5000
SCREEN_WIDTH = 800
SCREEN_HEIGHT = 480

# Status-Werte für die Simulation
class DeviceState:
    def __init__(self):
        self.program_state = "IDLE"  # IDLE, RUNNING, COMPLETED, ERROR
        self.active_program = 2      # 1, 2, 3, 4 (individuell)
        self.program_duration = 14 * 24 * 60 * 60  # in Sekunden
        self.start_time = 0          # Startzeitpunkt in Epoch-Sekunden
        self.custom_days = 7         # Benutzerdefinierte Tage
        self.tank_level_ok = True    # Tank-Füllstand OK?
        self.motor_active = False    # Motor aktiv?
        self.current_screen = "main" # main, program, settings, running, completed, error
        self.brightness = 80         # Display-Helligkeit (%)

    def get_state_dict(self):
        return {
            "program_state": self.program_state,
            "active_program": self.active_program,
            "program_duration": self.program_duration,
            "start_time": self.start_time,
            "custom_days": self.custom_days,
            "tank_level_ok": self.tank_level_ok,
            "motor_active": self.motor_active,
            "current_screen": self.current_screen,
            "brightness": self.brightness,
            "remaining_time": self.get_remaining_time(),
            "progress_percent": self.get_progress_percent()
        }
    
    def get_remaining_time(self):
        if self.program_state != "RUNNING" or self.program_duration == 0:
            return "0"
        
        elapsed_seconds = int(time.time() - self.start_time)
        if elapsed_seconds >= self.program_duration:
            return "0"
        
        remaining_seconds = self.program_duration - elapsed_seconds
        days = remaining_seconds // (24 * 60 * 60)
        remaining_seconds %= (24 * 60 * 60)
        hours = remaining_seconds // (60 * 60)
        remaining_seconds %= (60 * 60)
        minutes = remaining_seconds // 60
        
        if days > 0:
            return f"{days} Tage {hours} Std {minutes} Min"
        elif hours > 0:
            return f"{hours} Std {minutes} Min"
        else:
            return f"{minutes} Min"
    
    def get_progress_percent(self):
        if self.program_state != "RUNNING" or self.program_duration == 0:
            return 0
        
        elapsed_seconds = int(time.time() - self.start_time)
        if elapsed_seconds >= self.program_duration:
            return 100
        
        return int((elapsed_seconds * 100) / self.program_duration)
    
    def start_program(self, program_index):
        self.active_program = program_index
        
        # Programmdauer basierend auf Programm
        if program_index == 1:
            self.program_duration = 7 * 24 * 60 * 60  # 7 Tage
        elif program_index == 2:
            self.program_duration = 14 * 24 * 60 * 60  # 14 Tage
        elif program_index == 3:
            self.program_duration = 21 * 24 * 60 * 60  # 21 Tage
        elif program_index == 4:
            self.program_duration = self.custom_days * 24 * 60 * 60  # Individuelle Tage
        
        self.program_state = "RUNNING"
        self.start_time = time.time()
        self.motor_active = True
        self.current_screen = "running"
    
    def stop_program(self):
        self.program_state = "IDLE"
        self.motor_active = False
    
    def set_custom_days(self, days):
        self.custom_days = days
        if self.active_program == 4:
            self.program_duration = days * 24 * 60 * 60

# Globaler Zustand
device_state = DeviceState()

# LVGL Screen Renderer
class LVGLRenderer:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.bg_color = (0, 51, 102)  # Dunkelblau für den Hintergrund
        self.text_color = (255, 255, 255)  # Weiß für Text
        
        # Schriftarten laden
        try:
            # Versuche, eine Schriftart zu laden, die ähnlich wie Montserrat aussieht
            self.large_font = ImageFont.truetype("arial.ttf", 28)
            self.medium_font = ImageFont.truetype("arial.ttf", 22)
            self.small_font = ImageFont.truetype("arial.ttf", 16)
        except:
            # Fallback auf Standard
            self.large_font = ImageFont.load_default()
            self.medium_font = ImageFont.load_default()
            self.small_font = ImageFont.load_default()
    
    def create_image(self):
        # Erstelle ein neues Bild
        image = Image.new("RGB", (self.width, self.height), self.bg_color)
        draw = ImageDraw.Draw(image)
        return image, draw
    
    def render_to_base64(self, screen_name):
        # Rendert den angegebenen Bildschirm und gibt einen Base64-String zurück
        method_name = f"render_{screen_name}_screen"
        if hasattr(self, method_name) and callable(func := getattr(self, method_name)):
            image = func()
            buf = io.BytesIO()
            image.save(buf, format="PNG")
            return base64.b64encode(buf.getvalue()).decode("utf-8")
        else:
            # Fallback auf einfachen Bildschirm, wenn der angeforderte nicht existiert
            image, draw = self.create_image()
            draw.text((self.width//2, self.height//2), f"Bildschirm '{screen_name}' nicht implementiert", 
                     fill=self.text_color, font=self.medium_font, anchor="mm")
            buf = io.BytesIO()
            image.save(buf, format="PNG")
            return base64.b64encode(buf.getvalue()).decode("utf-8")
    
    def draw_button(self, draw, x, y, width, height, text, color=(76, 175, 80)):
        # Zeichnet einen Button mit Text
        draw.rectangle([(x, y), (x + width, y + height)], fill=color)
        draw.text((x + width//2, y + height//2), text, fill=(255, 255, 255), font=self.medium_font, anchor="mm")
    
    def draw_progress_bar(self, draw, x, y, width, height, percent):
        # Zeichnet einen Fortschrittsbalken
        draw.rectangle([(x, y), (x + width, y + height)], outline=(255, 255, 255))
        progress_width = int((width - 2) * percent / 100)
        if progress_width > 0:
            draw.rectangle([(x + 1, y + 1), (x + 1 + progress_width, y + height - 1)], fill=(76, 175, 80))
    
    def render_main_screen(self):
        # Hauptbildschirm rendern
        image, draw = self.create_image()
        
        # Titel
        draw.text((self.width//2, 30), "Desinfektionseinheit", 
                 fill=self.text_color, font=self.large_font, anchor="mm")
        
        # Programmauswahl-Button
        self.draw_button(draw, self.width//2 - 150, self.height//2 - 80, 300, 60, "Programme")
        
        # Einstellungen-Button
        self.draw_button(draw, self.width//2 - 150, self.height//2 + 20, 300, 60, "Einstellungen")
        
        # Status
        status_text = "Bereit für Desinfektion"
        if device_state.program_state == "RUNNING":
            status_text = "Programm läuft"
        elif device_state.program_state == "COMPLETED":
            status_text = "Programm abgeschlossen"
        elif device_state.program_state == "ERROR":
            status_text = "Fehler: Tank leer"
        
        draw.text((self.width//2, self.height - 30), status_text, 
                 fill=self.text_color, font=self.medium_font, anchor="mm")
        
        return image
    
    def render_program_screen(self):
        # Programmauswahl-Bildschirm rendern
        image, draw = self.create_image()
        
        # Titel
        draw.text((self.width//2, 30), "Programmauswahl", 
                 fill=self.text_color, font=self.large_font, anchor="mm")
        
        # Programm 1 Button
        self.draw_button(draw, self.width//2 - 350, 80, 700, 60, "Programm 1: 7 Tage Desinfektion")
        
        # Programm 2 Button
        self.draw_button(draw, self.width//2 - 350, 150, 700, 60, "Programm 2: 14 Tage Desinfektion")
        
        # Programm 3 Button
        self.draw_button(draw, self.width//2 - 350, 220, 700, 60, "Programm 3: 21 Tage Desinfektion")
        
        # Individuell
        draw.text((self.width//2 - 120, 300), "Individuelle Tage:", 
                 fill=self.text_color, font=self.medium_font, anchor="mm")
        
        # Spinbox für Tage
        draw.rectangle([(self.width//2 - 50, 290), (self.width//2 + 50, 310)], outline=(255, 255, 255))
        draw.text((self.width//2, 300), str(device_state.custom_days), 
                 fill=self.text_color, font=self.medium_font, anchor="mm")
        
        # Plus/Minus Buttons
        self.draw_button(draw, self.width//2 - 100, 290, 40, 20, "-", (150, 150, 150))
        self.draw_button(draw, self.width//2 + 60, 290, 40, 20, "+", (150, 150, 150))
        
        # Start individuelles Programm Button
        self.draw_button(draw, self.width//2 - 150, self.height - 80, 300, 60, 
                        "Individuelles Programm starten")
        
        # Zurück Button
        self.draw_button(draw, 20, self.height - 80, 150, 60, "Zurück", (150, 150, 150))
        
        return image
    
    def render_settings_screen(self):
        # Einstellungen-Bildschirm rendern
        image, draw = self.create_image()
        
        # Titel
        draw.text((self.width//2, 30), "Einstellungen", 
                 fill=self.text_color, font=self.large_font, anchor="mm")
        
        # Helligkeit
        draw.text((self.width//2, 80), "Display-Helligkeit", 
                 fill=self.text_color, font=self.medium_font, anchor="mm")
        
        # Helligkeits-Slider
        draw.rectangle([(self.width//2 - 200, 120), (self.width//2 + 200, 140)], outline=(255, 255, 255))
        slider_pos = int((self.width//2 - 200) + (400 * device_state.brightness / 100))
        draw.rectangle([(self.width//2 - 200, 120), (slider_pos, 140)], fill=(76, 175, 80))
        
        # Datum/Uhrzeit
        draw.text((self.width//2, 180), "Datum und Uhrzeit", 
                 fill=self.text_color, font=self.medium_font, anchor="mm")
        
        self.draw_button(draw, self.width//2 - 125, 210, 250, 60, "Datum/Uhrzeit einstellen")
        
        # Tank-Kalibrierung
        draw.text((self.width//2, 300), "Tank-Sensor kalibrieren", 
                 fill=self.text_color, font=self.medium_font, anchor="mm")
        
        self.draw_button(draw, self.width//2 - 125, 330, 250, 60, "Kalibrieren")
        
        # Zurück Button
        self.draw_button(draw, 20, self.height - 80, 150, 60, "Zurück", (150, 150, 150))
        
        return image
    
    def render_running_screen(self):
        # Laufendes Programm-Bildschirm rendern
        image, draw = self.create_image()
        
        # Titel
        draw.text((self.width//2, 30), "Programm aktiv", 
                 fill=self.text_color, font=self.large_font, anchor="mm")
        
        # Programm-Name
        program_names = ["Programm 1: 7 Tage", "Programm 2: 14 Tage", 
                        "Programm 3: 21 Tage", "Individuell"]
        program_text = f"{program_names[device_state.active_program-1]} Desinfektion"
        draw.text((self.width//2, 80), program_text, 
                 fill=self.text_color, font=self.medium_font, anchor="mm")
        
        # Fortschrittsbalken
        progress = device_state.get_progress_percent()
        self.draw_progress_bar(draw, self.width//2 - 350, 130, 700, 30, progress)
        
        # Restzeit
        time_text = "Verbleibende Zeit: " + device_state.get_remaining_time()
        draw.text((self.width//2, 180), time_text, 
                 fill=self.text_color, font=self.medium_font, anchor="mm")
        
        # Tank-Status
        draw.text((self.width//2 - 80, 240), "Tank-Status:", 
                 fill=self.text_color, font=self.small_font, anchor="mm")
        
        status_color = (0, 255, 0) if device_state.tank_level_ok else (255, 0, 0)
        status_text = "OK" if device_state.tank_level_ok else "LEER"
        draw.text((self.width//2 + 20, 240), status_text, 
                 fill=status_color, font=self.small_font, anchor="mm")
        
        # Stop-Button
        self.draw_button(draw, self.width//2 - 125, self.height - 80, 250, 60, 
                        "Programm stoppen", (255, 0, 0))
        
        return image
    
    def render_completed_screen(self):
        # Abgeschlossenes Programm-Bildschirm rendern
        image, draw = self.create_image()
        image = Image.new("RGB", (self.width, self.height), (0, 102, 0))  # Grüner Hintergrund
        draw = ImageDraw.Draw(image)
        
        # Titel
        draw.text((self.width//2, 30), "Programm abgeschlossen", 
                 fill=self.text_color, font=self.large_font, anchor="mm")
        
        # Erfolgssymbol (Häkchen simulieren)
        draw.ellipse([(self.width//2 - 50, self.height//2 - 50), 
                     (self.width//2 + 50, self.height//2 + 50)], outline=(255, 255, 255), width=3)
        draw.line([(self.width//2 - 30, self.height//2), (self.width//2 - 10, self.height//2 + 20)], 
                 fill=(255, 255, 255), width=6)
        draw.line([(self.width//2 - 10, self.height//2 + 20), (self.width//2 + 30, self.height//2 - 20)], 
                 fill=(255, 255, 255), width=6)
        
        # Erfolgsmeldung
        draw.text((self.width//2, self.height//2 + 80), "Desinfektion erfolgreich abgeschlossen!", 
                 fill=self.text_color, font=self.medium_font, anchor="mm")
        
        # Hauptmenü-Button
        self.draw_button(draw, self.width//2 - 150, self.height - 80, 300, 60, "Zum Hauptmenü")
        
        return image
    
    def render_error_screen(self):
        # Fehlerbildschirm rendern
        image, draw = self.create_image()
        image = Image.new("RGB", (self.width, self.height), (153, 0, 0))  # Roter Hintergrund
        draw = ImageDraw.Draw(image)
        
        # Titel
        draw.text((self.width//2, 30), "Fehler", 
                 fill=self.text_color, font=self.large_font, anchor="mm")
        
        # Warnsymbol (Ausrufezeichen)
        draw.polygon([(self.width//2, self.height//2 - 60), 
                     (self.width//2 - 40, self.height//2 + 20), 
                     (self.width//2 + 40, self.height//2 + 20)], 
                    outline=(255, 255, 0), fill=(255, 255, 0))
        draw.rectangle([(self.width//2 - 5, self.height//2 - 40), 
                       (self.width//2 + 5, self.height//2 - 10)], 
                      fill=(0, 0, 0))
        draw.rectangle([(self.width//2 - 5, self.height//2), 
                       (self.width//2 + 5, self.height//2 + 10)], 
                      fill=(0, 0, 0))
        
        # Fehlermeldung
        draw.text((self.width//2, self.height//2 + 60), "Tankfüllstand zu niedrig!", 
                 fill=self.text_color, font=self.medium_font, anchor="mm")
        
        # Anweisungen
        draw.text((self.width//2, self.height//2 + 100), "Bitte Tank auffüllen und neu starten.", 
                 fill=self.text_color, font=self.small_font, anchor="mm")
        
        # OK-Button
        self.draw_button(draw, self.width//2 - 100, self.height - 80, 200, 60, "OK")
        
        return image

# LVGL Renderer initialisieren
lvgl_renderer = LVGLRenderer(SCREEN_WIDTH, SCREEN_HEIGHT)

# Automatische Statusüberprüfung in einem separaten Thread
def status_checker():
    while True:
        # Überprüfen, ob ein laufendes Programm beendet werden muss
        if device_state.program_state == "RUNNING" and device_state.program_duration > 0:
            elapsed_seconds = int(time.time() - device_state.start_time)
            if elapsed_seconds >= device_state.program_duration:
                device_state.program_state = "COMPLETED"
                device_state.motor_active = False
                device_state.current_screen = "completed"
        
        time.sleep(1)

# HTTP-Handler für die Simulation
class LVGLSimulationHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/":
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-type", "text/html")
            self.end_headers()
            self.wfile.write(self.get_html_content().encode("utf-8"))
        elif self.path == "/api/state":
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(device_state.get_state_dict()).encode("utf-8"))
        elif self.path == "/api/screen":
            # Rendere den aktuellen Bildschirm
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-type", "application/json")
            self.end_headers()
            screen_data = {
                "image": lvgl_renderer.render_to_base64(device_state.current_screen)
            }
            self.wfile.write(json.dumps(screen_data).encode("utf-8"))
        else:
            # Statische Dateien aus dem aktuellen Verzeichnis bereitstellen
            return http.server.SimpleHTTPRequestHandler.do_GET(self)
    
    def do_POST(self):
        if self.path == "/api/action":
            content_length = int(self.headers["Content-Length"])
            post_data = self.rfile.read(content_length).decode("utf-8")
            action_data = json.loads(post_data)
            
            action = action_data.get("action", "")
            
            if action == "start_program":
                program_index = int(action_data.get("program", 2))
                device_state.start_program(program_index)
            elif action == "stop_program":
                device_state.stop_program()
                device_state.current_screen = "main"
            elif action == "set_custom_days":
                days = int(action_data.get("days", 7))
                device_state.set_custom_days(days)
            elif action == "set_tank_level":
                level_ok = action_data.get("level_ok", True)
                device_state.tank_level_ok = level_ok
                if not level_ok and device_state.program_state == "RUNNING":
                    device_state.stop_program()
                    device_state.program_state = "ERROR"
                    device_state.current_screen = "error"
            elif action == "navigate":
                screen = action_data.get("screen", "main")
                device_state.current_screen = screen
            elif action == "set_brightness":
                brightness = int(action_data.get("brightness", 80))
                device_state.brightness = brightness
            
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"status": "success"}).encode("utf-8"))
        else:
            self.send_response(HTTPStatus.NOT_FOUND)
            self.end_headers()
    
    def get_html_content(self):
        html = """
        <!DOCTYPE html>
        <html lang="de">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>ESP32-S3 LVGL Desinfektionseinheit Simulator</title>
            <style>
                body {
                    font-family: Arial, sans-serif;
                    margin: 0;
                    padding: 20px;
                    background-color: #f4f4f4;
                }
                .container {
                    display: flex;
                    flex-wrap: wrap;
                    gap: 20px;
                }
                .panel {
                    background: white;
                    border-radius: 5px;
                    padding: 15px;
                    box-shadow: 0 2px 4px rgba(0,0,0,0.1);
                    flex: 1;
                    min-width: 300px;
                }
                h1, h2 {
                    color: #333;
                }
                .display-panel {
                    display: flex;
                    flex-direction: column;
                    align-items: center;
                }
                .lvgl-display {
                    background: #000;
                    border: 10px solid #333;
                    border-radius: 5px;
                    margin-bottom: 15px;
                    position: relative;
                    width: 800px;
                    height: 480px;
                    overflow: hidden;
                }
                .lvgl-display img {
                    width: 100%;
                    height: 100%;
                    object-fit: cover;
                }
                .lvgl-touch-overlay {
                    position: absolute;
                    top: 0;
                    left: 0;
                    width: 100%;
                    height: 100%;
                    cursor: pointer;
                }
                .controls-panel {
                    display: flex;
                    flex-direction: column;
                    gap: 15px;
                }
                .control-group {
                    background: #f9f9f9;
                    padding: 10px;
                    border-radius: 5px;
                    border: 1px solid #eee;
                }
                button {
                    background-color: #4CAF50;
                    border: none;
                    color: white;
                    padding: 8px 16px;
                    text-align: center;
                    text-decoration: none;
                    display: inline-block;
                    font-size: 14px;
                    margin: 4px 2px;
                    cursor: pointer;
                    border-radius: 4px;
                }
                button:hover {
                    background-color: #45a049;
                }
                button:disabled {
                    background-color: #cccccc;
                    cursor: not-allowed;
                }
                select, input {
                    padding: 8px;
                    margin: 5px;
                    border-radius: 4px;
                    border: 1px solid #ddd;
                }
                .status-indicator {
                    display: inline-block;
                    width: 10px;
                    height: 10px;
                    border-radius: 50%;
                    margin-right: 5px;
                }
                .status-active {
                    background-color: #4CAF50;
                }
                .status-inactive {
                    background-color: #f44336;
                }
                .tank-indicator {
                    width: 60px;
                    height: 120px;
                    border: 2px solid #333;
                    border-radius: 5px;
                    position: relative;
                    margin: 10px auto;
                }
                .tank-level {
                    position: absolute;
                    bottom: 0;
                    left: 0;
                    width: 100%;
                    background-color: #2196F3;
                    transition: height 0.5s;
                }
            </style>
        </head>
        <body>
            <h1>ESP32-S3 LVGL Desinfektionseinheit Simulator</h1>
            
            <div class="container">
                <div class="panel display-panel">
                    <h2>Touchscreen-Display (4,3 Zoll, 800x480)</h2>
                    <div class="lvgl-display">
                        <img id="lvgl-screen" src="" alt="LVGL Screen">
                        <div class="lvgl-touch-overlay" id="touch-overlay"></div>
                    </div>
                    
                    <div>
                        <button id="refresh-screen-btn">Bildschirm aktualisieren</button>
                    </div>
                </div>
                
                <div class="panel controls-panel">
                    <h2>Hardware-Simulation</h2>
                    
                    <!-- Navigation Shortcuts -->
                    <div class="control-group">
                        <h3>Bildschirm-Navigation</h3>
                        <button data-screen="main">Hauptbildschirm</button>
                        <button data-screen="program">Programmauswahl</button>
                        <button data-screen="settings">Einstellungen</button>
                        <button data-screen="running">Laufendes Programm</button>
                        <button data-screen="completed">Abgeschlossen</button>
                        <button data-screen="error">Fehlerbildschirm</button>
                    </div>
                    
                    <!-- Tank Simulation -->
                    <div class="control-group">
                        <h3>Tank-Simulation</h3>
                        <div class="tank-indicator">
                            <div class="tank-level" id="tank-level" style="height: 100%;"></div>
                        </div>
                        <button id="tank-ok-btn">Tank voll</button>
                        <button id="tank-empty-btn">Tank leer</button>
                    </div>
                    
                    <!-- Program Control -->
                    <div class="control-group">
                        <h3>Programm-Steuerung</h3>
                        <div>
                            <label for="program-select">Programm:</label>
                            <select id="program-select">
                                <option value="1">Programm 1 (7 Tage)</option>
                                <option value="2" selected>Programm 2 (14 Tage)</option>
                                <option value="3">Programm 3 (21 Tage)</option>
                                <option value="4">Programm 4 (Individuell)</option>
                            </select>
                        </div>
                        
                        <div id="custom-days-container">
                            <label for="custom-days">Eigene Tage:</label>
                            <input type="number" id="custom-days" min="1" max="99" value="7">
                            <button id="set-custom-days-btn">Tage festlegen</button>
                        </div>
                        
                        <div style="margin-top: 10px;">
                            <button id="start-program-btn">Programm starten</button>
                            <button id="stop-program-btn">Programm stoppen</button>
                        </div>
                    </div>
                    
                    <!-- Status Display -->
                    <div class="control-group">
                        <h3>Systemstatus</h3>
                        <p>
                            <span class="status-indicator" id="program-status-indicator"></span>
                            Programm: <span id="program-status-text">-</span>
                        </p>
                        <p>
                            <span class="status-indicator" id="motor-status-indicator"></span>
                            Motor: <span id="motor-status-text">-</span>
                        </p>
                        <p>
                            <span class="status-indicator" id="tank-status-indicator"></span>
                            Tank: <span id="tank-status-text">-</span>
                        </p>
                        <p>
                            Fortschritt: <span id="progress-text">-</span>
                        </p>
                        <p>
                            Restzeit: <span id="remaining-time-text">-</span>
                        </p>
                    </div>
                </div>
            </div>
            
            <script>
                // DOM-Elemente
                const lvglScreen = document.getElementById('lvgl-screen');
                const touchOverlay = document.getElementById('touch-overlay');
                const tankLevel = document.getElementById('tank-level');
                const programSelect = document.getElementById('program-select');
                const customDaysInput = document.getElementById('custom-days');
                
                const programStatusIndicator = document.getElementById('program-status-indicator');
                const motorStatusIndicator = document.getElementById('motor-status-indicator');
                const tankStatusIndicator = document.getElementById('tank-status-indicator');
                const programStatusText = document.getElementById('program-status-text');
                const motorStatusText = document.getElementById('motor-status-text');
                const tankStatusText = document.getElementById('tank-status-text');
                const progressText = document.getElementById('progress-text');
                const remainingTimeText = document.getElementById('remaining-time-text');
                
                // Bildschirm aktualisieren
                function refreshScreen() {
                    fetch('/api/screen')
                        .then(response => response.json())
                        .then(data => {
                            lvglScreen.src = 'data:image/png;base64,' + data.image;
                        })
                        .catch(error => {
                            console.error('Fehler beim Laden des Bildschirms:', error);
                        });
                }
                
                // Aktion an den Server senden
                function sendAction(action, params = {}) {
                    const data = { action, ...params };
                    
                    fetch('/api/action', {
                        method: 'POST',
                        headers: {
                            'Content-Type': 'application/json'
                        },
                        body: JSON.stringify(data)
                    })
                    .then(response => response.json())
                    .then(data => {
                        console.log('Aktion erfolgreich gesendet:', data);
                        updateState();
                        refreshScreen();
                    })
                    .catch(error => {
                        console.error('Fehler beim Senden der Aktion:', error);
                    });
                }
                
                // Zustand aktualisieren
                function updateState() {
                    fetch('/api/state')
                        .then(response => response.json())
                        .then(state => {
                            // Statusanzeigen aktualisieren
                            programStatusIndicator.className = 'status-indicator';
                            motorStatusIndicator.className = 'status-indicator';
                            tankStatusIndicator.className = 'status-indicator';
                            
                            programStatusIndicator.classList.add(state.program_state === 'RUNNING' ? 'status-active' : 'status-inactive');
                            motorStatusIndicator.classList.add(state.motor_active ? 'status-active' : 'status-inactive');
                            tankStatusIndicator.classList.add(state.tank_level_ok ? 'status-active' : 'status-inactive');
                            
                            const programNames = ["Programm 1 (7 Tage)", "Programm 2 (14 Tage)", 
                                                "Programm 3 (21 Tage)", "Programm 4 (Individuell)"];
                            programStatusText.textContent = `${programNames[state.active_program-1]} - ${state.program_state}`;
                            motorStatusText.textContent = state.motor_active ? 'Aktiv' : 'Inaktiv';
                            tankStatusText.textContent = state.tank_level_ok ? 'OK' : 'Leer';
                            
                            // Tank-Anzeige aktualisieren
                            tankLevel.style.height = state.tank_level_ok ? '100%' : '10%';
                            tankLevel.style.backgroundColor = state.tank_level_ok ? '#2196F3' : '#f44336';
                            
                            // Fortschritt und Restzeit
                            progressText.textContent = `${state.progress_percent}%`;
                            remainingTimeText.textContent = state.remaining_time;
                            
                            // Formularwerte aktualisieren
                            programSelect.value = state.active_program;
                            customDaysInput.value = state.custom_days;
                        })
                        .catch(error => {
                            console.error('Fehler beim Aktualisieren des Zustands:', error);
                        });
                }
                
                // Touch-Simulation
                touchOverlay.addEventListener('click', function(event) {
                    const rect = touchOverlay.getBoundingClientRect();
                    const x = event.clientX - rect.left;
                    const y = event.clientY - rect.top;
                    
                    // X und Y-Koordinaten auf Prozent im Display umrechnen
                    const xPercent = (x / rect.width) * 100;
                    const yPercent = (y / rect.height) * 100;
                    
                    console.log(`Touch bei ${xPercent.toFixed(1)}%, ${yPercent.toFixed(1)}%`);
                    
                    // Hier könnte man basierend auf dem aktuellen Bildschirm und Position 
                    // bestimmte Aktionen auslösen, z.B. auf Buttons "klicken"
                });
                
                // Event-Listener für Bildschirm-Navigation
                document.querySelectorAll('button[data-screen]').forEach(button => {
                    button.addEventListener('click', () => {
                        const screen = button.getAttribute('data-screen');
                        sendAction('navigate', { screen });
                    });
                });
                
                // Button-Listener
                document.getElementById('refresh-screen-btn').addEventListener('click', refreshScreen);
                
                document.getElementById('tank-ok-btn').addEventListener('click', () => {
                    sendAction('set_tank_level', { level_ok: true });
                });
                
                document.getElementById('tank-empty-btn').addEventListener('click', () => {
                    sendAction('set_tank_level', { level_ok: false });
                });
                
                document.getElementById('set-custom-days-btn').addEventListener('click', () => {
                    const days = parseInt(customDaysInput.value);
                    if (days >= 1 && days <= 99) {
                        sendAction('set_custom_days', { days });
                    }
                });
                
                document.getElementById('start-program-btn').addEventListener('click', () => {
                    const program = parseInt(programSelect.value);
                    sendAction('start_program', { program });
                });
                
                document.getElementById('stop-program-btn').addEventListener('click', () => {
                    sendAction('stop_program');
                });
                
                // Regelmäßige Aktualisierung
                setInterval(updateState, 2000);
                setInterval(refreshScreen, 2000);
                
                // Initiale Aktualisierung
                updateState();
                refreshScreen();
            </script>
        </body>
        </html>
        """
        return html

# Thread für Statusprüfung starten
status_thread = threading.Thread(target=status_checker, daemon=True)
status_thread.start()

# Webserver starten
with socketserver.TCPServer(("", PORT), LVGLSimulationHandler) as httpd:
    print(f"ESP32-S3 LVGL Desinfektionseinheit Simulator läuft auf Port {PORT}")
    print(f"Öffnen Sie http://0.0.0.0:{PORT} im Browser")
    httpd.serve_forever()
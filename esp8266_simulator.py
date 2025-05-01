#!/usr/bin/env python3
"""
ESP8266 Desinfektionseinheit Simulator

Dieses Skript simuliert die Funktionalität der Desinfektionseinheit mit dem ESP8266
und stellt eine Weboberfläche bereit, um das Verhalten zu visualisieren.
"""

import http.server
import socketserver
import json
import time
import threading
import os
from http import HTTPStatus

# Grundeinstellungen
PORT = 5000
PROGRAM_NAMES = ["Programm 1", "Programm 2", "Programm 3", "Programm 4"]
PROGRAM_DURATIONS = [7, 14, 21, 0]  # Dauer in Tagen (0 = individuell)

# Simulierter Zustand des ESP8266
class ESP8266State:
    def __init__(self):
        self.active_program = "Programm 2"  # Standardprogramm (14 Tage)
        self.program_active = False
        self.start_time = 0
        self.program_duration = PROGRAM_DURATIONS[1] * 24 * 60 * 60  # in Sekunden
        self.custom_days = 7
        self.led_status = "normal"  # normal, problem, program_active
        self.motor_active = False
        self.tank_level_ok = True
        self.menu_state = "START_SCREEN"

    def get_program_index(self):
        return PROGRAM_NAMES.index(self.active_program) if self.active_program in PROGRAM_NAMES else 0

    def get_remaining_time(self):
        if not self.program_active or self.program_duration == 0:
            return "Bereit" if not self.program_active else "Individuell"
        
        elapsed_seconds = int(time.time() - self.start_time)
        if elapsed_seconds >= self.program_duration:
            return "Abgelaufen"
        
        remaining_seconds = self.program_duration - elapsed_seconds
        days = remaining_seconds // (24 * 60 * 60)
        hours = (remaining_seconds % (24 * 60 * 60)) // (60 * 60)
        minutes = (remaining_seconds % (60 * 60)) // 60
        
        result = ""
        if days > 0:
            result += f"{days}T "
        if hours > 0 or days > 0:
            result += f"{hours}Std "
        result += f"{minutes}Min"
        
        return result

    def get_state_dict(self):
        return {
            "active_program": self.active_program,
            "program_active": self.program_active,
            "program_duration": self.program_duration,
            "custom_days": self.custom_days,
            "led_status": self.led_status,
            "motor_active": self.motor_active,
            "tank_level_ok": self.tank_level_ok,
            "menu_state": self.menu_state,
            "remaining_time": self.get_remaining_time(),
            "progress_percent": self.get_progress_percent()
        }
    
    def get_progress_percent(self):
        if not self.program_active or self.program_duration == 0:
            return 0
        
        elapsed_seconds = int(time.time() - self.start_time)
        if elapsed_seconds >= self.program_duration:
            return 100
        
        return int((elapsed_seconds * 100) / self.program_duration)
    
    def start_program(self):
        self.program_active = True
        self.start_time = time.time()
        self.motor_active = True
        self.led_status = "program_active"
        self.menu_state = "PROGRAM_RUNNING"
    
    def stop_program(self):
        self.program_active = False
        self.motor_active = False
        self.led_status = "normal"
    
    def change_program(self, program_name):
        if program_name in PROGRAM_NAMES:
            self.active_program = program_name
            idx = PROGRAM_NAMES.index(program_name)
            self.program_duration = PROGRAM_DURATIONS[idx] * 24 * 60 * 60
            # Bei Programm 4 (individuell) die benutzerdefinierte Dauer verwenden
            if idx == 3:
                self.program_duration = self.custom_days * 24 * 60 * 60
    
    def set_custom_days(self, days):
        self.custom_days = days
        if self.active_program == "Programm 4":
            self.program_duration = days * 24 * 60 * 60
    
    def set_tank_level(self, level_ok):
        self.tank_level_ok = level_ok
        if not level_ok and self.program_active:
            self.stop_program()
            self.menu_state = "ERROR_TANK_LOW"
            self.led_status = "problem"

# Globale Zustandsinstanz
esp_state = ESP8266State()

# Automatische Statusüberprüfung in einem separaten Thread
def status_checker():
    while True:
        # Überprüfen, ob ein laufendes Programm beendet werden muss
        if esp_state.program_active and esp_state.program_duration > 0:
            elapsed_seconds = int(time.time() - esp_state.start_time)
            if elapsed_seconds >= esp_state.program_duration:
                esp_state.stop_program()
                esp_state.menu_state = "PROGRAM_COMPLETED"
        
        # Automatischer Start im START_SCREEN nach 30 Sekunden
        if not esp_state.program_active and esp_state.menu_state == "START_SCREEN":
            current_time = time.time()
            if hasattr(esp_state, 'auto_start_time'):
                if current_time - esp_state.auto_start_time >= 30:
                    esp_state.change_program("Programm 2")
                    esp_state.start_program()
            else:
                esp_state.auto_start_time = current_time
        
        time.sleep(1)

# HTTP-Handler für die Simulation
class SimulationHandler(http.server.SimpleHTTPRequestHandler):
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
            self.wfile.write(json.dumps(esp_state.get_state_dict()).encode("utf-8"))
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
                esp_state.start_program()
            elif action == "stop_program":
                esp_state.stop_program()
            elif action == "change_program":
                program_name = action_data.get("program", "")
                esp_state.change_program(program_name)
            elif action == "set_custom_days":
                days = int(action_data.get("days", 7))
                esp_state.set_custom_days(days)
            elif action == "set_tank_level":
                level_ok = action_data.get("level_ok", True)
                esp_state.set_tank_level(level_ok)
            elif action == "set_menu_state":
                menu_state = action_data.get("state", "MAIN_MENU")
                esp_state.menu_state = menu_state
            
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
            <title>ESP8266 Desinfektionseinheit Simulator</title>
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
                .display {
                    background: #000;
                    color: #fff;
                    padding: 10px;
                    border-radius: 5px;
                    font-family: 'Courier New', monospace;
                    height: 200px;
                    overflow: hidden;
                    margin-bottom: 15px;
                    position: relative;
                }
                .display-content {
                    position: absolute;
                    top: 10px;
                    left: 10px;
                    right: 10px;
                    bottom: 10px;
                }
                .led-panel {
                    display: flex;
                    gap: 10px;
                    margin-bottom: 15px;
                }
                .led {
                    width: 30px;
                    height: 30px;
                    border-radius: 50%;
                    border: 1px solid #ddd;
                }
                .led.red { background-color: #ff0000; opacity: 0.3; }
                .led.green { background-color: #00ff00; opacity: 0.3; }
                .led.blue { background-color: #0000ff; opacity: 0.3; }
                .led.active { opacity: 1; }
                .progress-container {
                    background-color: #e0e0e0;
                    border-radius: 5px;
                    height: 20px;
                    margin-bottom: 15px;
                    position: relative;
                }
                .progress-bar {
                    background-color: #4CAF50;
                    height: 100%;
                    border-radius: 5px;
                    width: 0%;
                    transition: width 1s;
                }
                .progress-text {
                    position: absolute;
                    top: 0;
                    left: 0;
                    right: 0;
                    text-align: center;
                    line-height: 20px;
                    color: #000;
                    font-weight: bold;
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
            </style>
        </head>
        <body>
            <h1>ESP8266 Desinfektionseinheit Simulator</h1>
            
            <div class="container">
                <div class="panel">
                    <h2>OLED Display</h2>
                    <div class="display">
                        <div class="display-content" id="display-content">
                            <!-- Simuliertes Display-Inhalte werden hier angezeigt -->
                        </div>
                    </div>
                    
                    <h2>LED Status</h2>
                    <div class="led-panel">
                        <div class="led red" id="led-red"></div>
                        <div class="led green" id="led-green"></div>
                        <div class="led blue" id="led-blue"></div>
                    </div>
                    
                    <h2>Fortschritt</h2>
                    <div class="progress-container">
                        <div class="progress-bar" id="progress-bar"></div>
                        <div class="progress-text" id="progress-text">0%</div>
                    </div>
                </div>
                
                <div class="panel">
                    <h2>Steuerung</h2>
                    
                    <div>
                        <label for="program-select">Programm:</label>
                        <select id="program-select">
                            <option value="Programm 1">Programm 1 (7 Tage)</option>
                            <option value="Programm 2" selected>Programm 2 (14 Tage)</option>
                            <option value="Programm 3">Programm 3 (21 Tage)</option>
                            <option value="Programm 4">Programm 4 (Individuell)</option>
                        </select>
                        <button id="change-program-btn">Programm ändern</button>
                    </div>
                    
                    <div id="custom-days-container" style="display: none;">
                        <label for="custom-days">Eigene Tage:</label>
                        <input type="number" id="custom-days" min="1" max="99" value="7">
                        <button id="set-custom-days-btn">Tage festlegen</button>
                    </div>
                    
                    <div style="margin-top: 15px;">
                        <button id="start-btn">Programm starten</button>
                        <button id="stop-btn">Programm stoppen</button>
                    </div>
                    
                    <div style="margin-top: 15px;">
                        <label for="menu-state-select">Menü-Status:</label>
                        <select id="menu-state-select">
                            <option value="START_SCREEN">Start-Bildschirm</option>
                            <option value="MAIN_MENU">Hauptmenü</option>
                            <option value="SETUP_14_DAYS_CONFIRM">14 Tage Bestätigung</option>
                            <option value="SETUP_21_DAYS_CONFIRM">21 Tage Bestätigung</option>
                            <option value="SETUP_NEW_DAYS_INIT">Individuelle Tage Initialisierung</option>
                            <option value="SETUP_NEW_DAYS_ADJUST">Individuelle Tage Anpassung</option>
                            <option value="SETUP_CONFIRMATION">Setup Bestätigung</option>
                            <option value="PROGRAM_RUNNING">Programm läuft</option>
                            <option value="PROGRAM_COMPLETED">Programm abgeschlossen</option>
                            <option value="ERROR_TANK_LOW">Fehler: Tank leer</option>
                        </select>
                        <button id="set-menu-state-btn">Status setzen</button>
                    </div>
                    
                    <div style="margin-top: 15px;">
                        <label>Tank-Füllstand:</label>
                        <button id="tank-ok-btn">Tank OK</button>
                        <button id="tank-low-btn">Tank leer</button>
                    </div>
                    
                    <h2>Status</h2>
                    <div>
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
                            Restzeit: <span id="remaining-time-text">-</span>
                        </p>
                    </div>
                </div>
            </div>
            
            <script>
                // Elemente aus dem DOM
                const displayContent = document.getElementById('display-content');
                const ledRed = document.getElementById('led-red');
                const ledGreen = document.getElementById('led-green');
                const ledBlue = document.getElementById('led-blue');
                const progressBar = document.getElementById('progress-bar');
                const progressText = document.getElementById('progress-text');
                const programSelect = document.getElementById('program-select');
                const customDaysContainer = document.getElementById('custom-days-container');
                const customDaysInput = document.getElementById('custom-days');
                const menuStateSelect = document.getElementById('menu-state-select');
                
                const programStatusIndicator = document.getElementById('program-status-indicator');
                const motorStatusIndicator = document.getElementById('motor-status-indicator');
                const tankStatusIndicator = document.getElementById('tank-status-indicator');
                const programStatusText = document.getElementById('program-status-text');
                const motorStatusText = document.getElementById('motor-status-text');
                const tankStatusText = document.getElementById('tank-status-text');
                const remainingTimeText = document.getElementById('remaining-time-text');
                
                // Button-Event-Listener
                document.getElementById('change-program-btn').addEventListener('click', () => {
                    const program = programSelect.value;
                    sendAction('change_program', { program });
                });
                
                document.getElementById('set-custom-days-btn').addEventListener('click', () => {
                    const days = parseInt(customDaysInput.value);
                    if (days >= 1 && days <= 99) {
                        sendAction('set_custom_days', { days });
                    }
                });
                
                document.getElementById('start-btn').addEventListener('click', () => {
                    sendAction('start_program');
                });
                
                document.getElementById('stop-btn').addEventListener('click', () => {
                    sendAction('stop_program');
                });
                
                document.getElementById('set-menu-state-btn').addEventListener('click', () => {
                    const state = menuStateSelect.value;
                    sendAction('set_menu_state', { state });
                });
                
                document.getElementById('tank-ok-btn').addEventListener('click', () => {
                    sendAction('set_tank_level', { level_ok: true });
                });
                
                document.getElementById('tank-low-btn').addEventListener('click', () => {
                    sendAction('set_tank_level', { level_ok: false });
                });
                
                // Programm-Auswahl Event-Listener
                programSelect.addEventListener('change', () => {
                    if (programSelect.value === 'Programm 4') {
                        customDaysContainer.style.display = 'block';
                    } else {
                        customDaysContainer.style.display = 'none';
                    }
                });
                
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
                        // Sofort aktualisieren
                        updateState();
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
                            updateDisplay(state);
                            updateLeds(state.led_status);
                            updateProgress(state.progress_percent);
                            updateStatusIndicators(state);
                            
                            // Programmauswahl aktualisieren
                            programSelect.value = state.active_program;
                            
                            // Custom Days Container anzeigen/verbergen
                            if (state.active_program === 'Programm 4') {
                                customDaysContainer.style.display = 'block';
                                customDaysInput.value = state.custom_days;
                            } else {
                                customDaysContainer.style.display = 'none';
                            }
                            
                            // Menü-Status aktualisieren
                            menuStateSelect.value = state.menu_state;
                        })
                        .catch(error => {
                            console.error('Fehler beim Aktualisieren des Zustands:', error);
                        });
                }
                
                // Display aktualisieren
                function updateDisplay(state) {
                    let content = '';
                    
                    switch (state.menu_state) {
                        case 'START_SCREEN':
                            content = `
                                <div style="font-size: 12px; font-weight: bold;">Desinfektionseinheit</div>
                                <div style="border-top: 1px solid white; margin: 5px 0;"></div>
                                <div style="font-size: 10px;">14 Tage Programm startet...</div>
                                <div style="margin-top: 10px; font-size: 10px;">
                                    Start in: ${Math.max(0, 30 - Math.floor((Date.now() - (window.autoStartTime || Date.now())) / 1000))} Sek.
                                </div>
                                <div style="margin-top: 5px; border: 1px solid white; height: 10px; width: 100%;">
                                    <div style="background: white; height: 8px; width: ${Math.max(0, 100 - (Date.now() - (window.autoStartTime || Date.now())) / 300)}%;"></div>
                                </div>
                                <div style="margin-top: 10px; font-size: 8px;">Taste drücken zum Abbrechen</div>
                            `;
                            // Auto-Start-Timer initialisieren, falls noch nicht gesetzt
                            if (!window.autoStartTime) {
                                window.autoStartTime = Date.now();
                            }
                            break;
                            
                        case 'MAIN_MENU':
                            content = `
                                <div style="font-size: 12px; font-weight: bold;">Hauptmenü</div>
                                <div style="border-top: 1px solid white; margin: 5px 0;"></div>
                                <div style="font-size: 10px;">> 14 Tage Programm</div>
                                <div style="font-size: 10px;">  21 Tage Programm</div>
                                <div style="font-size: 10px;">  Individuelles Programm</div>
                            `;
                            break;
                            
                        case 'SETUP_14_DAYS_CONFIRM':
                            content = `
                                <div style="font-size: 12px; font-weight: bold;">Programm wählen</div>
                                <div style="border-top: 1px solid white; margin: 5px 0;"></div>
                                <div style="font-size: 10px;">14 Tage Programm starten?</div>
                                <div style="margin-top: 15px; font-size: 8px;">+ Taste: Bestätigen</div>
                                <div style="font-size: 8px;">- Taste: Abbrechen</div>
                            `;
                            break;
                            
                        case 'SETUP_21_DAYS_CONFIRM':
                            content = `
                                <div style="font-size: 12px; font-weight: bold;">Programm wählen</div>
                                <div style="border-top: 1px solid white; margin: 5px 0;"></div>
                                <div style="font-size: 10px;">21 Tage Programm starten?</div>
                                <div style="margin-top: 15px; font-size: 8px;">+ Taste: Bestätigen</div>
                                <div style="font-size: 8px;">- Taste: Abbrechen</div>
                            `;
                            break;
                            
                        case 'SETUP_NEW_DAYS_INIT':
                            content = `
                                <div style="font-size: 12px; font-weight: bold;">Individuelle Einstellung</div>
                                <div style="border-top: 1px solid white; margin: 5px 0;"></div>
                                <div style="font-size: 10px;">Eigene Laufzeit einstellen?</div>
                                <div style="margin-top: 15px; font-size: 8px;">+ Taste: Fortfahren</div>
                                <div style="font-size: 8px;">- Taste: Abbrechen</div>
                            `;
                            break;
                            
                        case 'SETUP_NEW_DAYS_ADJUST':
                            content = `
                                <div style="font-size: 12px; font-weight: bold;">Laufzeit einstellen</div>
                                <div style="border-top: 1px solid white; margin: 5px 0;"></div>
                                <div style="font-size: 16px; text-align: center;">${state.custom_days} Tage</div>
                                <div style="margin-top: 10px; font-size: 8px;">+ Taste: Erhöhen</div>
                                <div style="font-size: 8px;">- Taste: Verringern</div>
                                <div style="font-size: 8px;">Langer Tastendruck: Bestätigen</div>
                            `;
                            break;
                            
                        case 'SETUP_CONFIRMATION':
                            content = `
                                <div style="font-size: 12px; font-weight: bold;">Bestätigung</div>
                                <div style="border-top: 1px solid white; margin: 5px 0;"></div>
                                <div style="font-size: 10px;">Programm mit ${state.custom_days} Tagen starten?</div>
                                <div style="margin-top: 15px; font-size: 8px;">+ Taste: Bestätigen</div>
                                <div style="font-size: 8px;">- Taste: Abbrechen</div>
                            `;
                            break;
                            
                        case 'PROGRAM_RUNNING':
                            content = `
                                <div style="font-size: 10px;">Programm aktiv:</div>
                                <div style="font-size: 12px; font-weight: bold;">${state.active_program}</div>
                                <div style="font-size: 10px; margin-top: 5px;">Verbleibende Zeit:</div>
                                <div style="font-size: 10px;">${state.remaining_time}</div>
                                <div style="margin-top: 5px; border: 1px solid white; height: 10px; width: 100%;">
                                    <div style="background: white; height: 8px; width: ${state.progress_percent}%;"></div>
                                </div>
                                <div style="font-size: 8px; margin-top: 5px;">${state.progress_percent}% abgeschlossen</div>
                            `;
                            break;
                            
                        case 'PROGRAM_COMPLETED':
                            content = `
                                <div style="font-size: 12px; font-weight: bold;">Programm abgeschlossen</div>
                                <div style="border-top: 1px solid white; margin: 5px 0;"></div>
                                <div style="font-size: 10px;">Desinfektion erfolgreich</div>
                                <div style="font-size: 10px;">abgeschlossen!</div>
                                <div style="margin-top: 15px; font-size: 8px;">Taste drücken für Neustart</div>
                            `;
                            break;
                            
                        case 'ERROR_TANK_LOW':
                            content = `
                                <div style="font-size: 12px; font-weight: bold;">Fehlermeldung</div>
                                <div style="border-top: 1px solid white; margin: 5px 0;"></div>
                                <div style="font-size: 10px;">Füllstand im</div>
                                <div style="font-size: 10px;">Tank zu gering!</div>
                                <div style="border-top: 1px solid white; margin: 5px 0;"></div>
                                <div style="font-size: 10px;">Neustarten für</div>
                                <div style="font-size: 10px;">Inbetriebnahme</div>
                            `;
                            break;
                            
                        default:
                            content = `
                                <div style="font-size: 12px; font-weight: bold;">Unbekannter Status</div>
                                <div style="font-size: 10px;">${state.menu_state}</div>
                            `;
                    }
                    
                    displayContent.innerHTML = content;
                }
                
                // LEDs aktualisieren
                function updateLeds(status) {
                    ledRed.classList.remove('active');
                    ledGreen.classList.remove('active');
                    ledBlue.classList.remove('active');
                    
                    switch (status) {
                        case 'normal':
                            ledGreen.classList.add('active');
                            break;
                        case 'problem':
                            ledRed.classList.add('active');
                            break;
                        case 'program_active':
                            ledBlue.classList.add('active');
                            break;
                    }
                }
                
                // Fortschrittsbalken aktualisieren
                function updateProgress(percent) {
                    progressBar.style.width = `${percent}%`;
                    progressText.textContent = `${percent}%`;
                }
                
                // Status-Indikatoren aktualisieren
                function updateStatusIndicators(state) {
                    // Programm-Status
                    programStatusIndicator.className = 'status-indicator';
                    programStatusIndicator.classList.add(state.program_active ? 'status-active' : 'status-inactive');
                    programStatusText.textContent = `${state.active_program} (${state.program_active ? 'Aktiv' : 'Inaktiv'})`;
                    
                    // Motor-Status
                    motorStatusIndicator.className = 'status-indicator';
                    motorStatusIndicator.classList.add(state.motor_active ? 'status-active' : 'status-inactive');
                    motorStatusText.textContent = state.motor_active ? 'Aktiv' : 'Inaktiv';
                    
                    // Tank-Status
                    tankStatusIndicator.className = 'status-indicator';
                    tankStatusIndicator.classList.add(state.tank_level_ok ? 'status-active' : 'status-inactive');
                    tankStatusText.textContent = state.tank_level_ok ? 'OK' : 'Leer';
                    
                    // Restzeit
                    remainingTimeText.textContent = state.remaining_time;
                }
                
                // Regelmäßige Aktualisierung
                setInterval(updateState, 1000);
                
                // Initiale Aktualisierung
                updateState();
            </script>
        </body>
        </html>
        """
        return html

# Thread für Statusprüfung starten
status_thread = threading.Thread(target=status_checker, daemon=True)
status_thread.start()

# Webserver starten
with socketserver.TCPServer(("", PORT), SimulationHandler) as httpd:
    print(f"ESP8266 Desinfektionseinheit Simulator läuft auf Port {PORT}")
    print(f"Öffnen Sie http://localhost:{PORT} im Browser")
    httpd.serve_forever()
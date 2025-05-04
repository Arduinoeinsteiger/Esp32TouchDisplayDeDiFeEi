#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <vector>

// WiFi-Konfiguration
#define WIFI_AP_SSID "SwissAirDry-Setup"
#define WIFI_AP_PASSWORD "swissairdry"
#define WIFI_HOSTNAME "desinfektion"
#define WIFI_CONFIG_PORTAL_TIMEOUT 180  // Timeout in Sekunden
#define DNS_PORT 53

// Struktur zum Speichern von WLAN-Netzwerken
struct WiFiNetwork {
    String ssid;
    int32_t rssi;
    uint8_t encType;
    String displayEncType;
};

class WiFiManager {
private:
    DNSServer dnsServer;
    Preferences preferences;
    
    String ssid;
    String password;
    
    unsigned long lastWiFiCheck = 0;
    bool connected = false;
    bool configMode = false;
    
    // Verschiedene Callback-Funktionen
    std::function<void(bool)> connectionCallback = nullptr;
    std::function<void()> configModeCallback = nullptr;
    
    // Speichert WLAN-Credentials
    void saveWiFiCredentials(const String &ssid, const String &password) {
        preferences.begin("wifi", false);
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);
        preferences.end();
    }
    
    // Lädt WLAN-Credentials
    bool loadWiFiCredentials() {
        preferences.begin("wifi", true);
        ssid = preferences.getString("ssid", "");
        password = preferences.getString("password", "");
        preferences.end();
        
        return (ssid.length() > 0);
    }
    
    // Startet den Access Point Modus
    void startAccessPoint() {
        WiFi.mode(WIFI_AP);
        Serial.println("Starte Access Point: " + String(WIFI_AP_SSID));
        
        WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
        
        Serial.print("AP IP Adresse: ");
        Serial.println(WiFi.softAPIP());
        
        // DNS Server für Captive Portal starten
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
        
        configMode = true;
        
        if (configModeCallback) {
            configModeCallback();
        }
    }
    
    // Verbindet mit gespeichertem WLAN
    bool connectToStoredWiFi() {
        if (!loadWiFiCredentials() || ssid.length() == 0) {
            Serial.println("Keine gespeicherten WLAN-Credentials gefunden");
            return false;
        }
        
        Serial.println("Verbinde mit gespeichertem WLAN: " + ssid);
        
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());
        
        // Bis zu 10 Sekunden auf Verbindung warten
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nVerbunden mit WLAN!");
            Serial.print("IP Adresse: ");
            Serial.println(WiFi.localIP());
            
            // mDNS starten
            if (MDNS.begin(WIFI_HOSTNAME)) {
                Serial.println("mDNS gestartet. Hostname: " + String(WIFI_HOSTNAME) + ".local");
            }
            
            connected = true;
            
            if (connectionCallback) {
                connectionCallback(true);
            }
            
            return true;
        } else {
            Serial.println("\nVerbindung mit WLAN fehlgeschlagen");
            
            if (connectionCallback) {
                connectionCallback(false);
            }
            
            return false;
        }
    }

public:
    WiFiManager() : connected(false), configMode(false) {
        // Konstruktor
    }
    
    // Initialisiert den WiFi Manager
    void begin() {
        // ESP Hostname setzen
        WiFi.setHostname(WIFI_HOSTNAME);
        
        // Verbindung mit gespeichertem WLAN versuchen
        if (!connectToStoredWiFi()) {
            // Wenn keine Verbindung möglich, Access Point starten
            startAccessPoint();
        }
    }
    
    // Hauptschleife
    void loop() {
        // Wenn im Access Point Modus, DNS-Server bedienen
        if (configMode) {
            dnsServer.processNextRequest();
        } 
        // Wenn im Client-Modus, WLAN-Verbindung überwachen
        else {
            unsigned long currentMillis = millis();
            
            // Alle 10 Sekunden WLAN-Status prüfen
            if (currentMillis - lastWiFiCheck >= 10000) {
                lastWiFiCheck = currentMillis;
                
                if (WiFi.status() != WL_CONNECTED) {
                    if (connected) {
                        Serial.println("WLAN-Verbindung verloren. Versuche Wiederverbindung...");
                        connected = false;
                        
                        if (connectionCallback) {
                            connectionCallback(false);
                        }
                    }
                    
                    // Versuche Wiederverbindung
                    WiFi.reconnect();
                } 
                else if (!connected) {
                    Serial.println("WLAN-Verbindung wiederhergestellt!");
                    connected = true;
                    
                    if (connectionCallback) {
                        connectionCallback(true);
                    }
                }
            }
        }
    }
    
    // Verbindet mit einem neuen WLAN-Netzwerk
    bool connect(const String &ssid, const String &password) {
        // WLAN-Credentials speichern
        saveWiFiCredentials(ssid, password);
        
        // Access Point beenden, falls aktiv
        if (configMode) {
            WiFi.softAPdisconnect(true);
            dnsServer.stop();
            configMode = false;
        }
        
        return connectToStoredWiFi();
    }
    
    // Setzt die WLAN-Konfiguration zurück
    void reset() {
        preferences.begin("wifi", false);
        preferences.clear();
        preferences.end();
        
        Serial.println("WLAN-Konfiguration zurückgesetzt");
        
        // Access Point starten
        startAccessPoint();
    }
    
    // Scannt nach verfügbaren WLAN-Netzwerken
    std::vector<WiFiNetwork> scanNetworks() {
        std::vector<WiFiNetwork> networks;
        
        Serial.println("Scanne WLAN-Netzwerke...");
        int numNetworks = WiFi.scanNetworks();
        
        if (numNetworks == 0) {
            Serial.println("Keine Netzwerke gefunden");
        } else {
            Serial.print(numNetworks);
            Serial.println(" Netzwerke gefunden");
            
            for (int i = 0; i < numNetworks; i++) {
                WiFiNetwork network;
                network.ssid = WiFi.SSID(i);
                network.rssi = WiFi.RSSI(i);
                network.encType = WiFi.encryptionType(i);
                
                // Verschlüsselungstyp als String
                switch (network.encType) {
                    case WIFI_AUTH_OPEN:
                        network.displayEncType = "Offen";
                        break;
                    case WIFI_AUTH_WEP:
                        network.displayEncType = "WEP";
                        break;
                    case WIFI_AUTH_WPA_PSK:
                        network.displayEncType = "WPA";
                        break;
                    case WIFI_AUTH_WPA2_PSK:
                        network.displayEncType = "WPA2";
                        break;
                    case WIFI_AUTH_WPA_WPA2_PSK:
                        network.displayEncType = "WPA/WPA2";
                        break;
                    case WIFI_AUTH_WPA2_ENTERPRISE:
                        network.displayEncType = "WPA2-Enterprise";
                        break;
                    default:
                        network.displayEncType = "Unbekannt";
                }
                
                networks.push_back(network);
            }
            
            // Netzwerke nach Signalstärke sortieren
            std::sort(networks.begin(), networks.end(), [](const WiFiNetwork &a, const WiFiNetwork &b) {
                return a.rssi > b.rssi;
            });
        }
        
        // Scan beenden und Ressourcen freigeben
        WiFi.scanDelete();
        
        return networks;
    }
    
    // Prüft, ob eine WLAN-Verbindung besteht
    bool isConnected() {
        return connected;
    }
    
    // Prüft, ob der Konfigurationsmodus aktiv ist
    bool isInConfigMode() {
        return configMode;
    }
    
    // Liefert die aktuelle IP-Adresse
    IPAddress getIP() {
        return configMode ? WiFi.softAPIP() : WiFi.localIP();
    }
    
    // Liefert die SSID des verbundenen Netzwerks
    String getSSID() {
        return ssid;
    }
    
    // Setzt den Callback für WLAN-Verbindungsstatus
    void setConnectionCallback(std::function<void(bool)> callback) {
        connectionCallback = callback;
    }
    
    // Setzt den Callback für Konfigurationsmodus
    void setConfigModeCallback(std::function<void()> callback) {
        configModeCallback = callback;
    }
};

#endif // WIFI_MANAGER_H
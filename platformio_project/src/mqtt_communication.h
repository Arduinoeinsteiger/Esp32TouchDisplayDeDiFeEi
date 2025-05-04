#ifndef MQTT_COMMUNICATION_H
#define MQTT_COMMUNICATION_H

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// MQTT-Verbindungseinstellungen
#define MQTT_SERVER "mqtt.swissairdry.local"  // MQTT-Server Adresse (ändern Sie dies nach Bedarf)
#define MQTT_PORT 1883                         // Standard MQTT-Port
#define MQTT_CLIENT_ID "desinfektion_"         // Basis-Client-ID (wird mit ESP-ID erweitert)
#define MQTT_USERNAME "desinfektion"           // MQTT-Benutzername (falls erforderlich)
#define MQTT_PASSWORD "sicher123"              // MQTT-Passwort (falls erforderlich)

// MQTT-Topics (Kanäle)
#define MQTT_TOPIC_STATUS "swissairdry/desinfektion/status"    // Status-Updates vom Gerät
#define MQTT_TOPIC_COMMAND "swissairdry/desinfektion/command"  // Befehle an das Gerät
#define MQTT_TOPIC_TELEMETRY "swissairdry/desinfektion/telemetry" // Telemetriedaten vom Gerät

// Maximale Puffergröße für JSON-Daten
#define JSON_BUFFER_SIZE 512

// MQTT-Callbacks
typedef void (*CommandCallback)(const String &command, const JsonObject &payload);

class MQTTCommunication {
private:
    WiFiClient espClient;
    PubSubClient mqttClient;
    String clientId;
    bool connected;
    unsigned long lastReconnectAttempt;
    
    CommandCallback commandCallback;
    
    // MQTT-Callback-Funktion für eingehende Nachrichten
    static void mqttCallback(char* topic, byte* payload, unsigned int length, void* instance) {
        if (instance != nullptr) {
            ((MQTTCommunication*)instance)->handleCallback(topic, payload, length);
        }
    }
    
    // Verarbeitet eingehende MQTT-Nachrichten
    void handleCallback(char* topic, byte* payload, unsigned int length) {
        // Nachricht in einen String umwandeln
        char message[length + 1];
        memcpy(message, payload, length);
        message[length] = '\0';
        
        String topicStr = String(topic);
        String payloadStr = String(message);
        
        Serial.print("Nachricht empfangen [");
        Serial.print(topicStr);
        Serial.print("]: ");
        Serial.println(payloadStr);
        
        // Befehle verarbeiten
        if (topicStr.equals(MQTT_TOPIC_COMMAND)) {
            DynamicJsonDocument doc(JSON_BUFFER_SIZE);
            DeserializationError error = deserializeJson(doc, payloadStr);
            
            if (error) {
                Serial.print("deserializeJson() fehlgeschlagen: ");
                Serial.println(error.f_str());
                return;
            }
            
            if (doc.containsKey("command") && commandCallback) {
                String command = doc["command"].as<String>();
                JsonObject payload = doc.as<JsonObject>();
                commandCallback(command, payload);
            }
        }
    }
    
    // Verbindung zum MQTT-Server herstellen
    bool connect() {
        Serial.print("Verbinde mit MQTT-Server als ");
        Serial.print(clientId);
        Serial.println("...");
        
        // Verbindung zum MQTT-Server herstellen
        if (mqttClient.connect(clientId.c_str(), MQTT_USERNAME, MQTT_PASSWORD)) {
            Serial.println("Verbunden mit MQTT-Server");
            
            // Topics abonnieren
            mqttClient.subscribe(MQTT_TOPIC_COMMAND);
            
            // Gerät-Online-Status veröffentlichen
            publishStatus("online");
            
            return true;
        } else {
            Serial.print("Verbindung fehlgeschlagen, rc=");
            Serial.println(mqttClient.state());
            return false;
        }
    }

public:
    MQTTCommunication() : mqttClient(espClient), connected(false), lastReconnectAttempt(0), commandCallback(nullptr) {
        // Client-ID mit ESP-ID erweitern
        clientId = String(MQTT_CLIENT_ID) + String(ESP.getEfuseMac(), HEX);
    }
    
    // Initialisierung der MQTT-Verbindung
    void begin() {
        mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
        mqttClient.setCallback([this](char* topic, byte* payload, unsigned int length) {
            this->handleCallback(topic, payload, length);
        });
        
        lastReconnectAttempt = 0;
    }
    
    // Verbindung regelmäßig prüfen und ggf. wiederherstellen
    void loop() {
        if (!mqttClient.connected()) {
            connected = false;
            unsigned long now = millis();
            if (now - lastReconnectAttempt > 5000) {
                lastReconnectAttempt = now;
                if (connect()) {
                    connected = true;
                    lastReconnectAttempt = 0;
                }
            }
        } else {
            connected = true;
            mqttClient.loop();
        }
    }
    
    // Registriert einen Callback für eingehende Befehle
    void setCommandCallback(CommandCallback callback) {
        commandCallback = callback;
    }
    
    // Veröffentlicht den Status des Geräts
    bool publishStatus(const String &status) {
        DynamicJsonDocument doc(JSON_BUFFER_SIZE);
        doc["status"] = status;
        doc["device_id"] = clientId;
        doc["timestamp"] = millis();
        
        String jsonStr;
        serializeJson(doc, jsonStr);
        
        return mqttClient.publish(MQTT_TOPIC_STATUS, jsonStr.c_str());
    }
    
    // Veröffentlicht detaillierte Statusinformationen
    bool publishDetailedStatus(const String &status, const JsonObject &details) {
        DynamicJsonDocument doc(JSON_BUFFER_SIZE);
        doc["status"] = status;
        doc["device_id"] = clientId;
        doc["timestamp"] = millis();
        
        // Details hinzufügen
        for (JsonPair p : details) {
            doc[p.key().c_str()] = p.value();
        }
        
        String jsonStr;
        serializeJson(doc, jsonStr);
        
        return mqttClient.publish(MQTT_TOPIC_STATUS, jsonStr.c_str());
    }
    
    // Veröffentlicht Telemetriedaten
    bool publishTelemetry(const JsonObject &data) {
        DynamicJsonDocument doc(JSON_BUFFER_SIZE);
        doc["device_id"] = clientId;
        doc["timestamp"] = millis();
        
        // Telemetriedaten hinzufügen
        for (JsonPair p : data) {
            doc[p.key().c_str()] = p.value();
        }
        
        String jsonStr;
        serializeJson(doc, jsonStr);
        
        return mqttClient.publish(MQTT_TOPIC_TELEMETRY, jsonStr.c_str());
    }
    
    // Prüft, ob eine Verbindung zum MQTT-Server besteht
    bool isConnected() {
        return connected;
    }
};

#endif // MQTT_COMMUNICATION_H
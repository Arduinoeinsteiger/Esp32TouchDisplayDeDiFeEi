#ifndef REST_API_H
#define REST_API_H

#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <vector>
#include <functional>

// Standard API-Port
#define API_PORT 80

// JSON-Puffergröße
#define API_JSON_BUFFER_SIZE 1024

// API-Endpunkt-Handler-Typ
typedef std::function<void(WebServer&, JsonDocument&)> APIEndpointHandler;

// API-Endpunkt-Definition
struct APIEndpoint {
    const char* path;
    const char* method;
    APIEndpointHandler handler;
};

class RESTAPI {
private:
    WebServer server;
    std::vector<APIEndpoint> endpoints;
    
    // Sendet eine erfolgreiche JSON-Antwort
    void sendJsonResponse(WebServer &server, int code, JsonDocument &doc) {
        String response;
        serializeJson(doc, response);
        
        server.send(code, "application/json", response);
    }
    
    // Sendet eine Fehlerantwort
    void sendErrorResponse(WebServer &server, int code, const String &message) {
        DynamicJsonDocument doc(128);
        doc["error"] = true;
        doc["message"] = message;
        
        String response;
        serializeJson(doc, response);
        
        server.send(code, "application/json", response);
    }
    
    // Verarbeitet JSON-Anfragen
    bool handleJsonRequest(WebServer &server, JsonDocument &doc) {
        // Prüfen, ob Inhalt verfügbar
        if (server.hasArg("plain") == false) {
            // Bei GET-Anfragen ist ein leerer Body erlaubt
            if (server.method() == HTTP_GET) {
                return true;
            }
            
            sendErrorResponse(server, 400, "No content provided");
            return false;
        }
        
        // JSON parsen
        String content = server.arg("plain");
        DeserializationError error = deserializeJson(doc, content);
        
        if (error) {
            sendErrorResponse(server, 400, String("JSON parsing failed: ") + error.c_str());
            return false;
        }
        
        return true;
    }

public:
    RESTAPI() : server(API_PORT) {
        // Konstruktor
    }
    
    // Initialisiert den API-Server
    void begin() {
        // Root-Handler für eine einfache Begrüßungsnachricht
        server.on("/", HTTP_GET, [this]() {
            DynamicJsonDocument doc(128);
            doc["message"] = "Desinfektionseinheit API";
            doc["version"] = "1.0";
            
            String response;
            serializeJson(doc, response);
            
            server.send(200, "application/json", response);
        });
        
        // Gesundheitsstatus-Endpunkt
        server.on("/health", HTTP_GET, [this]() {
            DynamicJsonDocument doc(128);
            doc["status"] = "ok";
            doc["timestamp"] = millis();
            
            String response;
            serializeJson(doc, response);
            
            server.send(200, "application/json", response);
        });
        
        // Not-Found-Handler
        server.onNotFound([this]() {
            sendErrorResponse(server, 404, "Endpoint not found");
        });
        
        // Registrierte Endpunkte einrichten
        for (const auto& endpoint : endpoints) {
            server.on(endpoint.path, [this, endpoint]() {
                DynamicJsonDocument doc(API_JSON_BUFFER_SIZE);
                
                // JSON-Anfrage verarbeiten
                if (handleJsonRequest(server, doc)) {
                    // Handler aufrufen
                    endpoint.handler(server, doc);
                }
            });
        }
        
        // Server starten
        server.begin();
        Serial.println("REST API Server gestartet auf Port " + String(API_PORT));
    }
    
    // Hauptschleife für den Server
    void loop() {
        server.handleClient();
    }
    
    // Registriert einen neuen API-Endpunkt
    void registerEndpoint(const char* path, const char* method, APIEndpointHandler handler) {
        APIEndpoint endpoint = {path, method, handler};
        endpoints.push_back(endpoint);
    }
    
    // Führt einen HTTP GET-Request durch
    bool get(const String &url, String &response) {
        HTTPClient http;
        http.begin(url);
        
        int httpCode = http.GET();
        
        if (httpCode > 0) {
            response = http.getString();
            http.end();
            return httpCode == HTTP_CODE_OK;
        } else {
            Serial.printf("HTTP GET request failed, error: %s\n", http.errorToString(httpCode).c_str());
            http.end();
            return false;
        }
    }
    
    // Führt einen HTTP POST-Request mit JSON-Daten durch
    bool post(const String &url, const JsonDocument &doc, String &response) {
        HTTPClient http;
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        
        String requestBody;
        serializeJson(doc, requestBody);
        
        int httpCode = http.POST(requestBody);
        
        if (httpCode > 0) {
            response = http.getString();
            http.end();
            return httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_CREATED;
        } else {
            Serial.printf("HTTP POST request failed, error: %s\n", http.errorToString(httpCode).c_str());
            http.end();
            return false;
        }
    }
    
    // Führt einen HTTP PUT-Request mit JSON-Daten durch
    bool put(const String &url, const JsonDocument &doc, String &response) {
        HTTPClient http;
        http.begin(url);
        http.addHeader("Content-Type", "application/json");
        
        String requestBody;
        serializeJson(doc, requestBody);
        
        int httpCode = http.PUT(requestBody);
        
        if (httpCode > 0) {
            response = http.getString();
            http.end();
            return httpCode == HTTP_CODE_OK;
        } else {
            Serial.printf("HTTP PUT request failed, error: %s\n", http.errorToString(httpCode).c_str());
            http.end();
            return false;
        }
    }
    
    // Führt einen HTTP DELETE-Request durch
    bool del(const String &url, String &response) {
        HTTPClient http;
        http.begin(url);
        
        int httpCode = http.sendRequest("DELETE");
        
        if (httpCode > 0) {
            response = http.getString();
            http.end();
            return httpCode == HTTP_CODE_OK;
        } else {
            Serial.printf("HTTP DELETE request failed, error: %s\n", http.errorToString(httpCode).c_str());
            http.end();
            return false;
        }
    }
};

#endif // REST_API_H
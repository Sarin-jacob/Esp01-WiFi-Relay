#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Preferences.h> // Include Preferences library
#include <time.h>
#include <DNSServer.h>

#define RELAY_PIN 0

const char* Rname = "sprinkler";
ESP8266WebServer server(80);

Preferences preferences; // Preferences object
DNSServer dnsServer;


String homeSSID = "";
String homePassword = "";
// bool isConfigured = false;

int startTime = 0; // Start time in seconds from midnight
int endTime = 0;   // End time in seconds from midnight
bool relayState = false;
bool manualOverride = false;
bool softap = false;

unsigned long lastSyncTime = 0; // To track the last synchronization time
const unsigned long syncInterval = 1800000; // 30 minutes in milliseconds

String css =  "<style>"
              "body, h1, p, form, button, input {"
              "margin: 0; padding: 0; box-sizing: border-box; font-family: 'Arial', sans-serif; }"
              "body { background-color: #f9f9f9; color: #333; display: flex; flex-direction: column;"
              "align-items: center; justify-content: center; min-height: 100vh; padding: 20px; }"
              "h1 { font-size: 2rem; margin-bottom: 10px; color: #555; }"
              "p { font-size: 1rem; margin-bottom: 20px; }"
              "b { color: #007bff; }"
              "form { display: flex; flex-direction: column; gap: 10px; margin-bottom: 20px; width: 100%; }"
              "input[type='time'] { padding: 8px; font-size: 1rem; border: 1px solid #ccc; border-radius: 4px;"
              "width: 100%; max-width: 300px; }"
              "input[type='submit'] { padding: 10px; font-size: 1rem; background-color: #007bff; color: white;"
              "border: none; border-radius: 4px; cursor: pointer; transition: background-color 0.3s; }"
              "input[type='submit']:hover { background-color: #0056b3; }"
              "input[type='text'],input[type='password'] { padding: 8px; font-size: 1rem; border: 1px solid #ccc;"
              "border-radius: 4px; width: 100%; max-width: 300px; transition: border-color 0.3s, box-shadow 0.3s;}"
              "input[type='text']:focus, input[type='password']:focus { border-color: #007bff; box-shadow: 0 0 5px rgba(0, 123, 255, 0.5); outline: none;}"
              "button { padding: 10px 20px; font-size: 1rem; color: white; background-color: #28a745;"
              "border: none; border-radius: 4px; cursor: pointer; transition: background-color 0.3s, transform 0.1s; }"
              "button:hover { background-color: #218838; } button:active { transform: scale(0.98); }"
              ".toggle-switch { position: relative; display: inline-block; width: 50px; height: 24px; margin-bottom: 20px; float: inline-end; margin-right: 10px;}"
              ".toggle-switch input { opacity: 0; width: 0; height: 0; }"
              ".toggle-slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0;"
              "background-color: #ccc; transition: 0.4s; border-radius: 24px; }"
              ".toggle-slider:before { position: absolute; content: ''; height: 18px; width: 18px; left: 3px; bottom: 3px;"
              "background-color: white; transition: 0.4s; border-radius: 50%; }"
              "input:checked + .toggle-slider { background-color: #28a745; }"
              "input:checked + .toggle-slider:before { transform: translateX(26px); }"
              ".button-row { display: flex; gap: 10px; width: 100%; max-width: 300px; margin: 10px; justify-content: center;}"
              "</style>";



void saveTimePreferences() {
    preferences.begin("sprinkler", false); // Open namespace
    preferences.putInt("startTime", startTime);
    preferences.putInt("endTime", endTime);
    preferences.end();
    Serial.printf("Saved startTime: %d, endTime: %d\n", startTime, endTime);
}

void saveOveridePref(){
  preferences.begin("sprinkler", false); // Open namespace
    preferences.putBool("manualOverride", manualOverride);
    preferences.end();
}
void loadPreferences() {
    preferences.begin("sprinkler", true); // Open namespace in read-only mode
    startTime = preferences.getInt("startTime", 0); // Default to 0
    endTime = preferences.getInt("endTime", 0);     // Default to 0
    manualOverride = preferences.getBool("manualOverride",false);
    preferences.end();
    Serial.printf("Loaded startTime: %d, endTime: %d\n", startTime, endTime);
}

void syncTime() {
    configTime(19800, 0, "pool.ntp.org", "time.nist.gov"); // Sync time with NTP server
    while (!time(nullptr)) {
        delay(500);
        Serial.println("Syncing time...");
    }
    Serial.println("Time synchronized");
}

void setupAPMode() {
    softap=true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(Rname);
    dnsServer.start(53, "*", WiFi.softAPIP());
    Serial.println("AP Mode: Hosting reset page at http://"+ String(Rname) +".local/rst");
    server.on("/", []() {
      server.sendHeader("Location", "/rst", true); // Redirect to /rst
      server.send(302, "text/plain", "Redirecting to WiFi Configuration page...");
    });

  server.onNotFound([]() {
    Serial.print("NOT_FOUND path: ");
    Serial.println(server.uri());
    server.send(200, "text/html",
    "<html><head><meta http-equiv='refresh' content='0; url=/' /></head>"
    "<body><p>Redirecting...</p></body></html>");
  });


}

void handleResetPage() {
    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + css + "</head><body>"
                  "<div><h1>Configure WiFi</h1>"
                  "<form action='/connect' method='POST'>"
                  "SSID: <input type='text' name='ssid'><br>"
                  "Password: <input type='password' name='password'><br>"
                  "<input type='submit' value='Connect'>"
                  "</form></div></body></html>";
    server.send(200, "text/html", html);
}

void handleConnectPage() {
    if (server.method() == HTTP_POST) {
        homeSSID = server.arg("ssid");
        homePassword = server.arg("password");

        WiFi.begin(homeSSID.c_str(), homePassword.c_str());
        WiFi.setAutoReconnect(true);
        WiFi.persistent(true);

        int retryCount = 0;
        while (WiFi.status() != WL_CONNECTED && retryCount < 20) {
            delay(500);
            Serial.print(".");
            retryCount++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            WiFi.softAPdisconnect(true);
            softap=false;
            syncTime(); // Sync time after connecting
            server.on("/", []() {
              server.sendHeader("Location", "/con", true); // Redirect to /rst
              server.send(302, "text/plain", "Redirecting to Sprinkler Control page...");
            });
            Serial.println("Connected to WiFi and time synced.");
        } else {
            Serial.println("Failed to connect. Restarting AP Mode.");
            setupAPMode();

        }
        server.sendHeader("Location", "/con");
        server.send(302, "text/plain", "Redirecting...");
    }
}

void handleSetTime() {
    if (server.method() == HTTP_POST) {
        String start = server.arg("start");
        String end = server.arg("end");

        int startHour, startMinute, endHour, endMinute;
        if (sscanf(start.c_str(), "%d:%d", &startHour, &startMinute) == 2 &&
            sscanf(end.c_str(), "%d:%d", &endHour, &endMinute) == 2) {
            startTime = startHour * 3600 + startMinute * 60;
            endTime = endHour * 3600 + endMinute * 60;
            saveTimePreferences(); // Save times to preferences
            // Serial.printf("Start Time: %d, End Time: %d\n", startTime, endTime);
            manualOverride = false;
            server.sendHeader("Location", "/con");
            server.send(302, "text/plain", "Redirecting...");
        } else {
            server.send(400, "text/plain", "Invalid time format");
        }
    }
}

void handleControlPage() {
    char startBuffer[6];
    char endBuffer[6];
    snprintf(startBuffer, sizeof(startBuffer), "%02d:%02d", startTime / 3600, (startTime % 3600) / 60);
    snprintf(endBuffer, sizeof(endBuffer), "%02d:%02d", endTime / 3600, (endTime % 3600) / 60);

    String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>" + css + "</head><body>"
                  "<div class='conto'>"
                  "<h1>Relay Control</h1>"
                  "<label class='toggle-switch'>"
                  "<input type='checkbox' onchange=\"location.href='/toggle'\" " + String(manualOverride ? "checked" : "") + ">"
                  "<span class='toggle-slider'></span>"
                  "</label>"
                  "<p>Manual Override: <b>" + String(manualOverride ? "ON" : "OFF") + "</b></p>"
                  "<form action='/settime' method='POST'>"
                  "Start Time (HH:MM): <input type='time' name='start' value='" + String(startBuffer) + "'><br>"
                  "End Time (HH:MM): <input type='time' name='end' value='" + String(endBuffer) + "'><br>"
                  // "End Time (HH:MM): <input type='time' name='end' value='" + String(endBuffer) + "' step='3600' min='00:00' max='23:59' pattern='[0-2][0-9]:[0-5][0-9]'><br>"
                  "<input type='submit' value='Set Time'>"
                  "</form><br>"
                  "<div class='button-row'>"
                  "<button onclick=\"location.href='/manual?state=on'\">Turn Relay ON</button>"
                  "<button onclick=\"location.href='/manual?state=off'\">Turn Relay OFF</button>"
                  "</div>"
                  "<div class='button-row'>"
                  "<button onclick=\"location.href='/restart'\">Restart</button>"
                  "<button onclick=\"location.href='/rst'\">Change WiFi</button>"
                  "</div></div>"
                  "</body></html>";
    server.send(200, "text/html", html);
}


void handleToggleOverride() {
    manualOverride = !manualOverride;
    server.sendHeader("Location", "/con");
    server.send(302, "text/plain", "Redirecting...");
    saveOveridePref();
}

void handleRestart() {
    ESP.restart();
}

void handleManualControl() {
    String state = server.arg("state");
    manualOverride = true;
    if (state == "on") {
        digitalWrite(RELAY_PIN, LOW);
        relayState = true;
    } else if (state == "off") {
        digitalWrite(RELAY_PIN, HIGH);
        relayState = false;
    }
    server.sendHeader("Location", "/con");
    server.send(302, "text/plain", "Redirecting...");
}


void setup() {
    Serial.begin(115200);
    pinMode(RELAY_PIN, OUTPUT);
    digitalWrite(RELAY_PIN, HIGH);
    loadPreferences(); // Load start and end times

    WiFi.mode(WIFI_STA);
    WiFi.persistent(true);
    WiFi.begin();

    int retryCount = 0;
    while (WiFi.status() != WL_CONNECTED && retryCount < 10) {
        delay(500);
        Serial.print(".");
        retryCount++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        setupAPMode();
    } else {
        Serial.print("Connected to WiFi : ");
        syncTime(); // Initial time synchronization
        // isConfigured=true;
        Serial.println(WiFi.localIP());
        server.on("/", []() {
          server.sendHeader("Location", "/con", true); // Redirect to /rst
          server.send(302, "text/plain", "Redirecting to Sprinkler Control page...");
        });
    }
    

    if (!MDNS.begin(Rname)) {
        Serial.println("Error starting mDNS");
    }
    MDNS.addService("http", "tcp", 80);
    server.on("/favicon.ico",[](){server.send(204,"text/plain","")})
    server.on("/rst", handleResetPage);
    server.on("/connect", handleConnectPage);
    server.on("/settime", handleSetTime);
    server.on("/manual", handleManualControl);
    server.on("/toggle", handleToggleOverride);
    server.on("/restart", handleRestart);
    server.on("/con", handleControlPage);

    server.begin();
}

void loop() {
    MDNS.update();
    server.handleClient();
    if(softap)dnsServer.processNextRequest();

    if (!manualOverride && WiFi.status() == WL_CONNECTED) {
        time_t now = time(nullptr);
        struct tm* currentTime = localtime(&now);
        int currentSeconds = currentTime->tm_hour * 3600 + currentTime->tm_min * 60;

    
    if (startTime <= endTime) {
        // Standard case: Same day range
        if (currentSeconds >= startTime && currentSeconds < endTime) {
            digitalWrite(RELAY_PIN, LOW);
        } else {
            digitalWrite(RELAY_PIN, HIGH);
        }
    } else {
        // Crosses midnight: Split logic
        if (currentSeconds >= startTime || currentSeconds < endTime) {
            digitalWrite(RELAY_PIN, LOW);
        } else {
            digitalWrite(RELAY_PIN, HIGH);
        }
    }
    
    }

    if (millis() - lastSyncTime > syncInterval && WiFi.status() == WL_CONNECTED) {
        syncTime();
        lastSyncTime = millis();
    }
}

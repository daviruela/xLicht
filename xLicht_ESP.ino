#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <FastLED.h>
#include <DNSServer.h>
#include <ArduinoOTA.h>

// --- HARDWARE ---
#define LED_PIN     5
#define MAX_LEDS    150 
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

// --- CONFIGURATION ---
// EDIT THE VALUES BELOW BEFORE UPLOADING
// ---------------------
const char* ssid = "YOUR_WIFI_SSID";          // The Hotspot Name (or your Router SSID)
const char* password = "YOUR_WIFI_PASS";      // The Hotspot Password (or your Router Password)
const char* app_pass = "admin";               // Password to login to the Web App on your phone
const char* ota_pass = "otapass";             // Password to upload code via Arduino IDE
// ---------------------

// --- GLOBALS ---
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer dnsServer;
CRGB leds[MAX_LEDS];
TaskHandle_t TaskLED;

// --- DATABASE (Increased size for 200+ games) ---
DynamicJsonDocument gamesDB(32768); 

struct GameProfile {
  String name = "System Boot";
  String effect = "SPIN";
  uint32_t colors[4] = {0x00FF00, 0, 0, 0};
};

// State
String currentID = "WAITING";
GameProfile currentProfile; 
GameProfile activeProfile;  
volatile bool profileUpdatePending = false; // Added 'volatile' for core safety

int activeLEDs = 16;
int globalBrightness = 150;
uint32_t defaultColor = 0x00FF00; 

// --- HTML APP ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>xLicht</title>
  <style>
    body { font-family: -apple-system, sans-serif; background: #121212; color: #fff; text-align: center; padding: 20px; margin: 0; }
    .card { background: #1e1e1e; border-radius: 12px; padding: 15px; margin-bottom: 20px; }
    select, button, input { width: 100%; padding: 12px; margin: 5px 0; border-radius: 8px; border: none; font-size: 1rem; box-sizing: border-box; }
    button { background: #107c10; color: white; font-weight: bold; cursor: pointer; }
    button.del { background: #d00; width: auto; padding: 8px 12px; font-size: 0.9rem; margin: 0; }
    button.load { background: #444; width: auto; padding: 8px 12px; font-size: 0.9rem; margin: 0 10px 0 0; }
    input[type=color] { height: 50px; background: none; padding: 0; }
    input[type=text], input[type=password], input[type=number] { background: #333; color: white; }
    input[type=range] { width: 100%; margin: 10px 0; }
    #loginOverlay { position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: #121212; z-index: 999; display: flex; flex-direction: column; justify-content: center; align-items: center; }
    .hidden { display: none !important; }
    
    #libList { text-align: left; max-height: 300px; overflow-y: auto; }
    .lib-item { display: flex; justify-content: space-between; align-items: center; background: #2a2a2a; padding: 10px; margin-bottom: 5px; border-radius: 6px; }
    .lib-info { flex-grow: 1; }
    .lib-id { font-size: 0.8rem; color: #888; font-family: monospace; }
  </style>
</head>
<body>

  <div id="loginOverlay">
    <h1>Security Check</h1>
    <div style="width: 80%; max-width: 300px;">
        <input type="password" id="passInput" placeholder="Enter App Password">
        <button onclick="checkPass()">Unlock</button>
        <p id="errorMsg" style="color: red; display: none;">Wrong Password</p>
    </div>
  </div>

  <div id="mainApp" class="hidden">
      <h1>xLicht</h1>
      
      <div class="card">
        <h2 id="gameName">Waiting...</h2>
        <p style="color:#888" id="gameID">---</p>
        
        <label>Game Name</label>
        <input type="text" id="editName" placeholder="Enter Game Name">

        <label>Effect:</label>
        <select id="eff" onchange="preview()">
            <option value="SOLID">Solid</option>
            <option value="SPIN">Spinning</option>
            <option value="BREATH">Breathing</option>
            <option value="RAINBOW">Rainbow Cycle</option>
            <option value="CHRISTMAS">Christmas Theme</option>
        </select>

        <label>Primary Color:</label>
        <input type="color" id="c1" onchange="preview()">
        
        <button onclick="saveGame()">Save Profile</button>
      </div>

      <div class="card">
        <h3>Game Library</h3>
        <button onclick="fetchLib()" style="background:#444; margin-bottom:15px;">Refresh Library</button>
        <div id="libList">Loading...</div>
      </div>

      <div class="card">
        <h3>Config</h3>
        <label>Brightness</label>
        <input type="range" id="brightInput" min="0" max="255" value="150" oninput="updateBrightness()" onchange="saveBrightness()">
        
        <label>System Default Color</label>
        <input type="color" id="defColor" onchange="saveDefaultColor()">

        <label>LED Count</label>
        <input type="number" id="ledCountInput" value="16" min="1" max="150">
        <button onclick="updateConfig()" style="background:#444">Update Length</button>
      </div>
  </div>

<script>
  var gateway = `ws://${window.location.hostname}/ws`;
  var websocket;
  
  document.getElementById('passInput').addEventListener("keypress", function(event) {
    if (event.key === "Enter") {
      event.preventDefault();
      checkPass();
    }
  });
  
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden && websocket && websocket.readyState === WebSocket.OPEN) {
      websocket.send(JSON.stringify({cmd: "REFRESH"}));
    }
  });
  
  function checkPass() {
    if(document.getElementById('passInput').value === "admin") {
        document.getElementById('loginOverlay').classList.add('hidden');
        document.getElementById('mainApp').classList.remove('hidden');
        init();
    } else {
        document.getElementById('errorMsg').style.display = 'block';
    }
  }

  function init() {
    websocket = new WebSocket(gateway);
    websocket.onopen = () => { fetchLib(); };
    websocket.onmessage = (e) => {
        var d = JSON.parse(e.data);
        if(d.cmd === "STATUS") {
            document.getElementById('gameName').innerText = d.name;
            if(document.activeElement.id !== "editName") document.getElementById('editName').value = d.name;
            
            document.getElementById('gameID').innerText = d.id;
            document.getElementById('eff').value = d.eff;
            document.getElementById('c1').value = d.c1;
            document.getElementById('ledCountInput').value = d.leds;
            document.getElementById('brightInput').value = d.bright;
            document.getElementById('defColor').value = d.defCol;
        } else if (d.cmd === "LIB") {
            renderLib(d.games);
        }
    };
    websocket.onclose = () => { setTimeout(init, 2000); };
  }

  function renderLib(games) {
    var html = "";
    if(games.length === 0) html = "<p>No saved games yet.</p>";
    else {
        games.forEach(g => {
            html += `<div class="lib-item">
                <div class="lib-info">
                    <div><b>${g.name}</b></div>
                    <div class="lib-id">${g.id}</div>
                </div>
                <div>
                    <button class="load" onclick="loadGame('${g.id}')">Edit</button>
                    <button class="del" onclick="delGame('${g.id}')">X</button>
                </div>
            </div>`;
        });
    }
    document.getElementById('libList').innerHTML = html;
  }

  function fetchLib() { websocket.send(JSON.stringify({cmd:"LIST_GAMES"})); }
  function loadGame(id) { websocket.send(JSON.stringify({cmd:"LOAD_GAME", id: id})); }
  function delGame(id) { 
      if(confirm("Delete profile for " + id + "?")) {
          websocket.send(JSON.stringify({cmd:"DELETE_GAME", id: id})); 
      }
  }

  function preview() {
    websocket.send(JSON.stringify({
        cmd: "PREVIEW",
        eff: document.getElementById('eff').value,
        c1: document.getElementById('c1').value
    }));
  }

  function saveGame() {
    websocket.send(JSON.stringify({
        cmd: "SAVE",
        id: document.getElementById('gameID').innerText,
        name: document.getElementById('editName').value,
        eff: document.getElementById('eff').value,
        c1: document.getElementById('c1').value
    }));
    setTimeout(fetchLib, 500); 
    alert("Saved!");
  }

  function updateConfig() {
    var count = document.getElementById('ledCountInput').value;
    websocket.send(JSON.stringify({cmd: "CONFIG", leds: parseInt(count)}));
    alert("Length Updated!");
  }

  function updateBrightness() {
    var b = document.getElementById('brightInput').value;
    websocket.send(JSON.stringify({cmd: "SET_BRIGHT", val: parseInt(b)}));
  }
  
  function saveBrightness() {
    websocket.send(JSON.stringify({cmd: "SAVE_BRIGHT"}));
  }

  function saveDefaultColor() {
     var c = document.getElementById('defColor').value;
     websocket.send(JSON.stringify({cmd: "SET_DEF_COLOR", val: c}));
  }
</script>
</body>
</html>
)rawliteral";

// --- LED RENDER HELPER ---
void renderEffect(GameProfile& profile, uint8_t& hue) {
    if (profile.effect == "RAINBOW") {
        fill_rainbow(leds, activeLEDs, hue++, 7);
    }
    else if (profile.effect == "SOLID") {
        for(int i=0; i<activeLEDs; i++) leds[i] = CRGB(profile.colors[0]);
    }
    else if (profile.effect == "SPIN") {
        int pos = (millis() / 50) % activeLEDs;
        leds[pos] = CRGB(profile.colors[0]);
        // Trail
        for(int i=1; i<4; i++) {
            int trail = pos - i;
            if(trail < 0) trail += activeLEDs;
            leds[trail] = CRGB(profile.colors[0]);
            leds[trail].fadeToBlackBy(i*60);
        }
    }
    else if (profile.effect == "BREATH") {
        for(int i=0; i<activeLEDs; i++) leds[i] = CRGB(profile.colors[0]);
    }
    else if (profile.effect == "CHRISTMAS") {
        int offset = millis() / 150;
        for(int i=0; i<activeLEDs; i++) {
            // Pattern: Red, Red, White, Green, Green, White
            int pos = (i + offset) % 6;
            if(pos < 2) leds[i] = CRGB::Red;
            else if(pos < 3) leds[i] = CRGB::White;
            else if(pos < 5) leds[i] = CRGB::Green;
            else leds[i] = CRGB::White;
        }
    }
}

// --- LED ENGINE ---
void LEDTask(void * parameter) {
  // 1. Wait for system to stabilize (7 seconds)
  vTaskDelay(7000 / portTICK_PERIOD_MS);

  // --- BOOT ANIMATION (Smoothed) ---
  FastLED.setBrightness(50);
  FastLED.clear();
  
  int wipeDelay = 2500 / activeLEDs; 
  if(wipeDelay < 5) wipeDelay = 5;

  // 1. Wipe
  for(int i=0; i<activeLEDs; i++) {
      leds[i] = CRGB(defaultColor);
      FastLED.show();
      vTaskDelay(wipeDelay / portTICK_PERIOD_MS); 
  }
  
  // 2. Breath Up
  for(int b=50; b<255; b+=5) {
      FastLED.setBrightness(b);
      FastLED.show();
      vTaskDelay(25 / portTICK_PERIOD_MS);
  }
  
  // 3. Breath Down
  for(int b=255; b>=globalBrightness; b-=5) {
      if (b < 0) break;
      FastLED.setBrightness(b);
      FastLED.show();
      vTaskDelay(25 / portTICK_PERIOD_MS);
  }
  
  FastLED.setBrightness(globalBrightness);
  FastLED.show();
  
  // -----------------------

  activeProfile = currentProfile;
  static uint8_t hue = 0;

  for(;;) {
    // 0. Transition Logic (2 Seconds Total: 1s OUT, 1s IN)
    if (profileUpdatePending) {
        unsigned long startTrans = millis();
        int startB = FastLED.getBrightness();
        
        // FADE OUT (1000ms)
        while(millis() - startTrans < 1000) {
             float prog = (float)(millis() - startTrans) / 1000.0;
             int curB = startB * (1.0 - prog);
             if(curB < 0) curB = 0;
             
             renderEffect(activeProfile, hue);
             
             // Handle Breath mixing
             if(activeProfile.effect == "BREATH") {
                 float bVal = (exp(sin(millis()/2000.0*PI)) - 0.36787944)*108.0;
                 int breathB = map(bVal, 0, 255, 5, globalBrightness);
                 FastLED.setBrightness(breathB * (1.0 - prog));
             } else {
                 FastLED.setBrightness(curB);
             }
             
             FastLED.show();
             vTaskDelay(15 / portTICK_PERIOD_MS);
        }
        
        // Swap
        activeProfile = currentProfile;
        profileUpdatePending = false;
        
        // FADE IN (1000ms)
        startTrans = millis();
        while(millis() - startTrans < 1000) {
             float prog = (float)(millis() - startTrans) / 1000.0;
             int targetB = globalBrightness;
             
             renderEffect(activeProfile, hue);
             
             if(activeProfile.effect == "BREATH") {
                 float bVal = (exp(sin(millis()/2000.0*PI)) - 0.36787944)*108.0;
                 int breathB = map(bVal, 0, 255, 5, globalBrightness);
                 FastLED.setBrightness(breathB * prog);
             } else {
                 FastLED.setBrightness(targetB * prog);
             }
             
             FastLED.show();
             vTaskDelay(15 / portTICK_PERIOD_MS);
        }
    }

    if (globalBrightness == 0) {
        FastLED.clear();
        FastLED.show();
        vTaskDelay(200 / portTICK_PERIOD_MS);
        continue;
    }

    renderEffect(activeProfile, hue);

    if (activeProfile.effect == "BREATH") {
        float breath = (exp(sin(millis()/2000.0*PI)) - 0.36787944)*108.0;
        int maxB = globalBrightness;
        int minB = maxB / 5;
        if (minB < 5 && maxB > 5) minB = 5; 
        FastLED.setBrightness(map(breath, 0, 255, minB, maxB));
    } else {
        FastLED.setBrightness(globalBrightness);
    }
    
    FastLED.show();
    vTaskDelay(15 / portTICK_PERIOD_MS);
  }
}

// --- LOGIC ---
String intToHex(uint32_t rgb) { char hex[8]; sprintf(hex, "#%06X", rgb); return String(hex); }
uint32_t hexToInt(String hex) { return strtol(&hex[1], NULL, 16); }

void sendStatus() {
    StaticJsonDocument<512> doc;
    doc["cmd"] = "STATUS";
    doc["id"] = currentID;
    doc["name"] = currentProfile.name;
    doc["eff"] = currentProfile.effect;
    doc["c1"] = intToHex(currentProfile.colors[0]);
    doc["leds"] = activeLEDs;
    doc["bright"] = globalBrightness;
    doc["defCol"] = intToHex(defaultColor);
    String out; serializeJson(doc, out); ws.textAll(out);
}

void sendLibrary(AsyncWebSocketClient *client) {
    DynamicJsonDocument listDoc(16384); // Large doc for full list
    listDoc["cmd"] = "LIB";
    JsonArray games = listDoc.createNestedArray("games");
    JsonObject root = gamesDB.as<JsonObject>();
    for (JsonPair kv : root) {
        JsonObject g = games.createNestedObject();
        g["id"] = kv.key().c_str();
        g["name"] = kv.value()["name"];
    }
    String out; serializeJson(listDoc, out); 
    if (client) client->text(out); // Send to requester
    else ws.textAll(out); // Broadcast if no specific client
}

void loadProfile(String id) {
    id.toUpperCase();
    id.trim();
    if(id.startsWith("0X")) id = id.substring(2);
    
    currentID = id;
    
    // Check if we have a saved profile for this ID
    if(gamesDB.containsKey(id)) {
        currentProfile.name = gamesDB[id]["name"].as<String>();
        currentProfile.effect = gamesDB[id]["eff"].as<String>();
        currentProfile.colors[0] = gamesDB[id]["c1"];
    } else {
        // Defaults for known IDs if not yet saved
        if(id == "4D5307E6") { currentProfile.name = "Halo 3"; currentProfile.colors[0] = 0x0000FF; }
        else if(id == "534207D3") { currentProfile.name = "Wolfenstein: TNO"; currentProfile.colors[0] = 0xFF0000; }
        else if(id == "41560817") { currentProfile.name = "CoD: MW2"; currentProfile.colors[0] = 0x00FF00; }
        else if(id == "FFFE07D1") { currentProfile.name = "Aurora Dashboard"; currentProfile.colors[0] = defaultColor; currentProfile.effect = "BREATH"; }
        else { 
            currentProfile.name = "Unknown Game"; 
            currentProfile.colors[0] = defaultColor; 
            currentProfile.effect = "SOLID";
        }
    }
    Serial.print("[APP] Loaded Profile: "); Serial.println(currentProfile.name);
    
    // Trigger LED transition
    profileUpdatePending = true;
    
    sendStatus();
}

void processLine(String line) {
    line.trim();
    if(line.length() > 256) return; 
    
    // 1. Title ID Check
    int idx = line.indexOf("current TitleId");
    if (idx != -1) {
        String id = line.substring(idx + 16, idx + 16 + 8);
        if (id.length() == 8 && id != currentID) {
            if (id == "00000000") {
               if (currentID != "FFFE07D1") {
                  Serial.println("[PARSER] Return to Dash (ID 0)");
                  loadProfile("FFFE07D1");
               }
            } else {
               Serial.print("[PARSER] Found Game ID: "); Serial.println(id);
               loadProfile(id);
            }
        }
    }
    
    // 2. Dashboard Logic (Stricter)
    // Only switch to Aurora if we see "Launcher Path:" AND a dash xex
    if (line.indexOf("Launcher Path:") != -1 && (line.indexOf("Aurora.xex") != -1 || line.indexOf("DashLaunch") != -1)) {
        if (currentID != "FFFE07D1") {
            Serial.println("[PARSER] Dashboard Detected (Log Path)");
            loadProfile("FFFE07D1");
        }
    }
}

// --- CONFIG PERSISTENCE ---
void loadConfig() {
  if (LittleFS.exists("/config.json")) {
    File f = LittleFS.open("/config.json", "r");
    StaticJsonDocument<256> doc; deserializeJson(doc, f);
    if(doc.containsKey("leds")) activeLEDs = doc["leds"];
    if(doc.containsKey("bright")) globalBrightness = doc["bright"];
    if(doc.containsKey("defCol")) defaultColor = doc["defCol"];
    f.close();
  }
}
void saveConfig() {
  StaticJsonDocument<256> doc;
  doc["leds"] = activeLEDs;
  doc["bright"] = globalBrightness;
  doc["defCol"] = defaultColor;
  File f = LittleFS.open("/config.json", "w");
  serializeJson(doc, f);
  f.close();
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if(type == WS_EVT_CONNECT) sendStatus();
    if(type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len) {
            data[len] = 0;
            StaticJsonDocument<512> doc; deserializeJson(doc, (char*)data);
            String cmd = doc["cmd"];
            
            if(cmd == "PREVIEW") { currentProfile.effect = doc["eff"].as<String>(); currentProfile.colors[0] = hexToInt(doc["c1"]); profileUpdatePending=true; }
            else if(cmd == "REFRESH") { sendStatus(); }
            else if(cmd == "SAVE") {
                String id = doc["id"];
                gamesDB[id]["name"] = doc["name"]; // Save Custom Name
                gamesDB[id]["eff"] = doc["eff"];
                gamesDB[id]["c1"] = hexToInt(doc["c1"]);
                File f = LittleFS.open("/games.json", "w"); serializeJson(gamesDB, f); f.close();
            }
            else if(cmd == "CONFIG") { activeLEDs = doc["leds"]; saveConfig(); sendStatus(); }
            else if(cmd == "SET_BRIGHT") { globalBrightness = doc["val"]; }
            else if(cmd == "SAVE_BRIGHT") { saveConfig(); }
            else if(cmd == "LIST_GAMES") { sendLibrary(client); }
            else if(cmd == "DELETE_GAME") {
                String id = doc["id"];
                gamesDB.remove(id);
                File f = LittleFS.open("/games.json", "w"); serializeJson(gamesDB, f); f.close();
                // Send updated list
                sendLibrary(NULL); // Broadcast update to all
            }
            else if(cmd == "LOAD_GAME") {
                loadProfile(doc["id"].as<String>());
            }
            else if(cmd == "SET_DEF_COLOR") {
                defaultColor = hexToInt(doc["val"]);
                saveConfig();
            }
        }
    }
}

// --- SETUP & LOOP ---
void setup() {
  Serial.begin(115200); // USB Debug
  Serial.println("\n[BOOT] xLicht Starting...");
  
  // Increase buffer for reliable Xbox parsing
  Serial2.setRxBufferSize(2048);
  // Use Serial2 (RX=16, TX=17) for Xbox communication
  Serial2.begin(115200, SERIAL_8N1, 16, 17);
  
  if(!LittleFS.begin(true)) { Serial.println("[ERR] FS Fail"); return; }
  
  // Load Database
  if(LittleFS.exists("/games.json")) { 
      File f = LittleFS.open("/games.json", "r"); 
      DeserializationError error = deserializeJson(gamesDB, f);
      f.close();
      if(error) Serial.println("[ERR] JSON DB Corrupt, resetting");
      else Serial.println("[OK] Games Database Loaded");
  }
  
  loadConfig();

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, MAX_LEDS);
  // SAFETY LIMIT: 850mA for Motherboard Tap (Safe for 18 LEDs)
  FastLED.setMaxPowerInVoltsAndMilliamps(5, 850); 
  
  WiFi.softAP(ssid, password);
  Serial.print("[WIFI] AP Started IP: "); Serial.println(WiFi.softAPIP());
  
  // Captive Portal
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());
  
  ArduinoOTA.setHostname("xLicht-ESP32");
  ArduinoOTA.setPassword(ota_pass);
  ArduinoOTA.onStart([]() { if(TaskLED!=NULL) vTaskDelete(TaskLED); FastLED.clear(); FastLED.show(); });
  ArduinoOTA.begin();

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request){ request->send_P(200, "text/html", index_html); });
  server.onNotFound([](AsyncWebServerRequest *request) { request->redirect("http://" + WiFi.softAPIP().toString() + "/"); });
  server.begin();

  currentProfile.name = "System Boot";
  currentProfile.effect = "SOLID"; 
  currentProfile.colors[0] = defaultColor;
  activeProfile = currentProfile;

  xTaskCreatePinnedToCore(LEDTask, "LEDTask", 4096, NULL, 1, &TaskLED, 1);
}

String serialBuffer = "";

void loop() {
  ArduinoOTA.handle();
  dnsServer.processNextRequest();
  ws.cleanupClients();

  // SERIAL PARSING LOOP
  while (Serial2.available()) {
    char c = Serial2.read();
    
    // ECHO TO PC (Debug Sniffer Mode)
    Serial.write(c);
    
    if (c == '\n') {
      processLine(serialBuffer);
      serialBuffer = "";
    } else if (c >= 32 && c <= 126) {
      serialBuffer += c;
    }
  }
}

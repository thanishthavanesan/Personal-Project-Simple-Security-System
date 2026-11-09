// ESP32 Smart Security & Access Control System
// Components: RC522 RFID, HC-SR501 PIR, SG90 Servo, OLED, Active Buzzer, RGB LED, Relay


//------------Library Imports-----------
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>

// ---------- Pin Definitions ----------
#define RST_PIN     27
#define SS_PIN      5
#define SERVO_PIN   13
#define PIR_PIN     14
#define BUZZER_PIN  25
#define RGB_RED     15
#define RGB_GREEN   2
#define RGB_BLUE    26
#define RELAY_PIN   4

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1                             //no reset pin so do not use one

// ---------- WiFi Credentials ----------
const char* ssid = "WIFI";                       //Changes according to user
const char* password = "PASSWORD";

// ---------- Whitelisted RFID UIDs ----------
String authorizedUIDs[] = {
  "35 66 33 07",   // keytag                   //Sample card #
  "55 A4 00 07"    // white card
};
const int numAuthorized = 2;

// ---------- Objects ----------
MFRC522 rfid(SS_PIN, RST_PIN);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Servo lockServo;
WebServer server(80);

// ---------- State ----------
bool systemArmed = false;
String lastUID = "None";
String lastEvent = "System idle";
unsigned long lastEventTime = 0;

// Simple in-memory log (circular buffer)
const int LOG_SIZE = 10;
String logEntries[LOG_SIZE];
int logIndex = 0;

void addLog(String entry) {
  logEntries[logIndex] = entry;
  logIndex = (logIndex + 1) % LOG_SIZE;
  lastEvent = entry;
  lastEventTime = millis();
}

// ---------- Setup ----------
void setup() {
  Serial.begin(115200);

  // Pin modes
  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RELAY_PIN, LOW);

  // SPI / RFID first
  SPI.begin();
  rfid.PCD_Init();
  delay(50);

  // RGB LED
  setupRGB();
  setRGB(0, 255, 0); // green = disarmed on boot

  // Servo - explicitly use timer 3 to avoid conflict with RGB (timers 0,1,2)
  ESP32PWM::allocateTimer(3);
  lockServo.setPeriodHertz(50);
  lockServo.attach(SERVO_PIN, 500, 2400);
  delay(100);
  lockServo.write(90); // 90 = unlocked/open

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
  }
  display.clearDisplay();
  showStatus("System Ready", "Disarmed");

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  setupWebServer();
  server.begin();

  addLog("System booted");
}

// ---------- Main Loop ----------
void loop() {
  server.handleClient();
  handleRFID();
  handlePIR();
}

// ---------- RFID Handling ----------
void handleRFID() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }
  // Convert the RFID UID bytes into a readable format like: 35 66 33 07
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uidStr += " ";
  }
  // Make the UID uppercase so it matches the authorized list
  uidStr.toUpperCase();
  lastUID = uidStr;
  Serial.println("Scanned UID: " + uidStr);

  // Check if the scanned UID is in the list of authorized cards
  bool valid = isAuthorized(uidStr);

  if (valid) {
    if (systemArmed) {
      // Authorized card while armed = disarm the system
      systemArmed = false;
      lockServo.write(90); // unlock
      setRGB(0, 255, 0);  // green = disarmed
showStatus("System Disarmed", "Door Unlocked");
      addLog("Disarmed by: " + uidStr);
    } else {
      // Authorized card while disarmed = arm the system
      systemArmed = true;
      lockServo.write(0);  // lock
      setRGB(0, 0, 255);   // blue = armed
      showStatus("System Armed", "Door Locked");
      addLog("Armed by: " + uidStr);
    }
    // Unauthorized card = flash red LED, sound buzzer, and record the attempt 
  } else {
    setRGB(255, 0, 0);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    showStatus("Access Denied", "UID: " + uidStr);
    addLog("Access denied: " + uidStr);
    delay(500);
    setRGB(0, 0, 0);
    if (systemArmed) showStatus("System Armed", "Monitoring...");
    else showStatus("System Ready", "Disarmed");
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

bool isAuthorized(String uid) {
  // Compare the scanned UID with every authorized UID
  for (int i = 0; i < numAuthorized; i++) {
    if (authorizedUIDs[i] == uid) 
      return true;
  }
  // No match was found
  return false;
}

// ---------- PIR Handling ----------
void handlePIR() {
  // Only trigger the alarm when the system is armed AND motion is detected
  if (systemArmed && digitalRead(PIR_PIN) == HIGH) {
    triggerAlarm();
  }
}

void triggerAlarm() {
  addLog("ALARM: motion detected");
  digitalWrite(RELAY_PIN, HIGH);
  lockServo.detach(); // detach to prevent jitter during alarm

  unsigned long start = millis();
  bool cancelled = false;

  while (!cancelled) {
    // Calculate how many seconds the alarm has been active
    unsigned long elapsed = (millis() - start) / 1000;
    // Create the timer text shown on the OLED
    String timeStr = "Active: " + String(elapsed) + "s";

    // Update OLED with elapsed time
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("!! INTRUDER ALERT !!");
    display.setCursor(0, 16);
    display.println("Motion detected!");
    display.setCursor(0, 32);
    display.println(timeStr);
    display.setCursor(0, 48);
    display.println("Scan card to cancel");
    display.display();

    // Turn Buzzer and flash RED LED ON
    setRGB(255, 0, 0);
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);

    //Turn Buzzer and LED OFF
    setRGB(0, 0, 0);
    digitalWrite(BUZZER_PIN, LOW);
    delay(200);

    server.handleClient();

    // Check for valid RFID scan to cancel
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      String uidStr = "";
      for (byte i = 0; i < rfid.uid.size; i++) {
        if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
        uidStr += String(rfid.uid.uidByte[i], HEX);
        if (i < rfid.uid.size - 1) uidStr += " ";
      }
      uidStr.toUpperCase();
      if (isAuthorized(uidStr)) {
        unsigned long duration = (millis() - start) / 1000;
        addLog("Alarm cancelled after " + String(duration) + "s by: " + uidStr);
        cancelled = true;
      }
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }
  }

  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  lockServo.attach(SERVO_PIN, 500, 2400);
  delay(100);
  lockServo.write(0); // ensure still locked
  setRGB(0, 0, 255);
  showStatus("System Armed", "Door Locked");
}

// ---------- RGB LED Setup (LEDC PWM) ----------
void setupRGB() {
  ledcAttach(RGB_RED, 5000, 8);
  ledcAttach(RGB_GREEN, 5000, 8);
  ledcAttach(RGB_BLUE, 5000, 8);
}

// ---------- RGB LED Initilization ----------
void setRGB(int r, int g, int b) {
  ledcWrite(RGB_RED, r);
  ledcWrite(RGB_GREEN, g);
  ledcWrite(RGB_BLUE, b);
}

// ---------- OLED Display Helper ----------
void showStatus(String line1, String line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(line1);
  display.setCursor(0, 16);
  display.println(line2);
  display.display();
}

// ---------- Web Server ----------
void setupWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", getDashboardHTML());
  });

  server.on("/api/status", HTTP_GET, []() {
    StaticJsonDocument<256> doc;
    doc["armed"] = systemArmed;
    doc["lastUID"] = lastUID;
    doc["lastEvent"] = lastEvent;
    doc["uptime"] = millis() / 1000;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.on("/api/log", HTTP_GET, []() {
    StaticJsonDocument<1024> doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < LOG_SIZE; i++) {
      int idx = (logIndex + i) % LOG_SIZE;
      if (logEntries[idx].length() > 0) {
        arr.add(logEntries[idx]);
      }
    }
    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
  });

  server.on("/api/toggle", HTTP_POST, []() {
    systemArmed = !systemArmed;
    if (systemArmed) {
      lockServo.write(0);   // lock
      setRGB(0, 0, 255);    // blue
      showStatus("System Armed", "Door Locked");
      addLog("Armed via dashboard");
    } else {
      lockServo.write(90);  // unlock
      setRGB(0, 255, 0);    // green
      showStatus("System Disarmed", "Door Unlocked");
      addLog("Disarmed via dashboard");
    }
    server.send(200, "application/json", "{\"armed\":" + String(systemArmed ? "true" : "false") + "}");
  });
}


String getDashboardHTML() {
  return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Security Dashboard</title>
  <style>
    body { font-family: Arial, sans-serif; background:#111; color:#eee; text-align:center; padding:20px; }
    .card { background:#1e1e1e; border-radius:10px; padding:20px; margin:10px auto; max-width:400px; }
    button { padding:12px 24px; font-size:16px; border-radius:8px; border:none; cursor:pointer; }
    .armed { background:#c0392b; color:white; }
    .disarmed { background:#27ae60; color:white; }
    table { width:100%; text-align:left; font-size:14px; }
  </style>
</head>
<body>
  <h2>ESP32 Security Dashboard</h2>
  <div class="card">
    <p>Status: <b id="status">--</b></p>
    <p>Last UID: <span id="uid">--</span></p>
    <p>Last Event: <span id="event">--</span></p>
    <button id="toggleBtn" onclick="toggleArm()">Loading...</button>
  </div>
  <div class="card">
    <h3>Event Log</h3>
    <table id="log"></table>
  </div>

<script>
async function refresh() {
  const res = await fetch('/api/status');
  const data = await res.json();
  document.getElementById('status').textContent = data.armed ? 'ARMED' : 'DISARMED';
  document.getElementById('uid').textContent = data.lastUID;
  document.getElementById('event').textContent = data.lastEvent;
  const btn = document.getElementById('toggleBtn');
  btn.textContent = data.armed ? 'Disarm' : 'Arm';
  btn.className = data.armed ? 'armed' : 'disarmed';

  const logRes = await fetch('/api/log');
  const logs = await logRes.json();
  document.getElementById('log').innerHTML = logs.map(l => `<tr><td>${l}</td></tr>`).join('');
}

async function toggleArm() {
  await fetch('/api/toggle', { method: 'POST' });
  refresh();
}

refresh();
setInterval(refresh, 2000);
</script>
</body>
</html>
)rawliteral";
}

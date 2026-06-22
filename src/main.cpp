#include <Arduino.h>
#include <AALeC-V3.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include "secrets.h"

// ── MQTT settings ─────────────────────────────
const char* MQTT_BROKER   = "broker.hivemq.com";
const int   MQTT_PORT     = 1883;
const char* TOPIC_DATA    = "bms/blesson/data";
const char* TOPIC_ALERT   = "bms/blesson/alert";
const char* TOPIC_COMMAND = "bms/blesson/command";
const char* TOPIC_STATUS  = "bms/blesson/status";

// ── Anomaly thresholds ────────────────────────
const float MAX_VOLTAGE = 4.2;
const float MIN_VOLTAGE = 3.0;
const float MAX_TEMP    = 45.0;

// ── Relay pin ─────────────────────────────────
// D1 (GPIO5) stays HIGH at boot — safe for active-LOW relay
// ZS-1 relay ACTIVE LOW + LED wired to NC terminal:
// HIGH = relay OFF = NC closed = LED ON  = load running  = NORMAL
// LOW  = relay ON  = NC open   = LED OFF = load cut off  = FAULT
#define RELAY_PIN    D1
#define RELAY_NORMAL HIGH
#define RELAY_FAULT  LOW

// ── Timing ────────────────────────────────────
unsigned long lastPublish      = 0;
unsigned long lastTelegram     = 0;
const unsigned long INTERVAL          = 5000;
const unsigned long TELEGRAM_COOLDOWN = 30000;

// ── State ─────────────────────────────────────
bool   relayTriggered = false;
String faultType      = "";

// ── Objects ───────────────────────────────────
WiFiClient       espClient;
WiFiClientSecure secureClient;
PubSubClient     mqtt(espClient);

// ─────────────────────────────────────────────
void sendTelegram(String message) {
  secureClient.setInsecure();
  HTTPClient http;
  String url = "https://api.telegram.org/bot";
  url += BOT_TOKEN;
  url += "/sendMessage?chat_id=";
  url += CHAT_ID;
  url += "&text=";
  url += message;
  http.begin(secureClient, url);
  int httpCode = http.GET();
  if (httpCode > 0) {
    Serial.println("Telegram OK (code: " + String(httpCode) + ")");
  } else {
    Serial.println("Telegram FAILED (code: " + String(httpCode) + ")");
  }
  http.end();
}

// ─────────────────────────────────────────────
float readVoltageNow() {
  return (analogRead(A0) / 1023.0) * 4.2;
}

bool conditionIsUnsafe(float voltage, float temperature) {
  return (voltage > MAX_VOLTAGE) ||
         (voltage < MIN_VOLTAGE) ||
         (temperature > MAX_TEMP);
}

// ─────────────────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Command received: ");
  Serial.println(message);

  StaticJsonDocument<100> cmd;
  if (deserializeJson(cmd, message)) {
    Serial.println("JSON parse error");
    return;
  }

  String command = cmd["command"].as<String>();

  // ── ACKNOWLEDGE ───────────────────────────
  if (command == "ACKNOWLEDGE") {
    float v = readVoltageNow();
    float t = aalec.get_temp();

    // ignore bad temp reading during ACK check
    if (t > 100.0) t = 30.0;

    if (conditionIsUnsafe(v, t)) {
      Serial.println("ACK REJECTED — still unsafe");
      Serial.printf("  Voltage: %.2fV | Temp: %.1fC\n", v, t);

      StaticJsonDocument<200> reject;
      reject["status"]      = "ACK_REJECTED";
      reject["reason"]      = "Voltage/temperature still out of range";
      reject["voltage"]     = String(v, 2);
      reject["temperature"] = String(t, 1);
      char buf[200];
      serializeJson(reject, buf);
      mqtt.publish(TOPIC_STATUS, buf);

      sendTelegram("ACK%20REJECTED%21%0ACondition%20still%20unsafe%0AVoltage%3A%20"
        + String(v, 2) + "V%0ATemp%3A%20" + String(t, 1) + "C");

      aalec.play(300, 800);

    } else {
      // Safe — relay off → NC closed → LED ON → load restored
      digitalWrite(RELAY_PIN, RELAY_NORMAL);
      relayTriggered = false;
      faultType      = "";
      aalec.set_rgb_strip(0, 0, 255, 0);
      aalec.play(500, 200);

      mqtt.publish(TOPIC_STATUS,
        "{\"status\":\"RELAY_CLOSED\",\"fault\":\"CLEARED\"}");
      Serial.println("ACK ACCEPTED — load RESTORED — LED on");

      sendTelegram("FAULT%20CLEARED%21%0ALoad%20restored%0ASystem%20back%20to%20normal");
    }
  }

  // ── RELAY_OPEN ────────────────────────────
  if (command == "RELAY_OPEN") {
    digitalWrite(RELAY_PIN, RELAY_FAULT);  // NC open → LED off
    relayTriggered = true;
    aalec.set_rgb_strip(0, 255, 0, 0);
    mqtt.publish(TOPIC_STATUS, "{\"status\":\"RELAY_OPEN\"}");
    Serial.println("Relay opened manually — load DISCONNECTED — LED off");
  }

  // ── RELAY_CLOSE ───────────────────────────
  if (command == "RELAY_CLOSE") {
    float v = readVoltageNow();
    float t = aalec.get_temp();
    if (t > 100.0) t = 30.0;  // ignore bad reading
    if (conditionIsUnsafe(v, t)) {
      Serial.println("Manual close rejected — unsafe");
      mqtt.publish(TOPIC_STATUS,
        "{\"status\":\"ACK_REJECTED\","
        "\"reason\":\"Manual close blocked - unsafe\"}");
    } else {
      digitalWrite(RELAY_PIN, RELAY_NORMAL);  // NC closed → LED on
      relayTriggered = false;
      aalec.set_rgb_strip(0, 0, 255, 0);
      mqtt.publish(TOPIC_STATUS, "{\"status\":\"RELAY_CLOSED\"}");
      Serial.println("Relay closed manually — load CONNECTED — LED on");
    }
  }

  // ── BEEP ──────────────────────────────────
  if (command == "BEEP") {
    aalec.play(1000, 200);
    delay(300);
    aalec.play(1000, 200);
    Serial.println("Buzzer triggered");
  }

  // ── LED control ───────────────────────────
  if (command == "LED_RED")   aalec.set_rgb_strip(0, 255, 0, 0);
  if (command == "LED_GREEN") aalec.set_rgb_strip(0, 0, 255, 0);
  if (command == "LED_BLUE")  aalec.set_rgb_strip(0, 0, 0, 255);
}

// ─────────────────────────────────────────────
void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("WiFi connected. IP: ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Connecting to MQTT...");
    if (mqtt.connect("BMS_Blesson")) {
      Serial.println("connected");
      mqtt.subscribe(TOPIC_COMMAND);
      mqtt.publish(TOPIC_STATUS, "{\"status\":\"ONLINE\"}");
    } else {
      Serial.print("failed rc=");
      Serial.print(mqtt.state());
      Serial.println(" retrying in 3s");
      delay(3000);
    }
  }
}

// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // Step 1 — relay safe immediately — NC closed — LED ON at boot
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_NORMAL);

  // Step 2 — init board
  delay(2000);
  aalec.init();
  Serial.println("AALeC ready");

  // Step 3 — relay safe again after aalec.init()
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_NORMAL);
  Serial.println("Relay NORMAL — NC closed — LED ON — load running");

  // Step 4 — warm up sensor — discard bad readings
  Serial.println("Warming up sensor...");
  delay(500); aalec.get_temp();
  delay(500); aalec.get_temp();
  delay(500); aalec.get_temp();
  delay(500); aalec.get_temp();
  delay(500);
  float checkTemp = aalec.get_temp();
  Serial.println("Sensor ready. Temp: " + String(checkTemp, 1) + "C");

  // Step 5 — connect
  connectWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  connectMQTT();

  sendTelegram("BMS%20Monitor%20Online%21%0ALoad%20connected%0AWatching%20your%20battery%2024%2F7.");
  Serial.println("System ready — LED ON — load running");
}

// ─────────────────────────────────────────────
void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  unsigned long now = millis();

  if (now - lastPublish >= INTERVAL) {
    lastPublish = now;

    float temperature = aalec.get_temp();
    float voltage     = readVoltageNow();

    // ── Ignore impossible temperature reading ──
    // Sensor returns ~180C on first read after boot
    // Skip this entire cycle and wait for next one
    if (temperature > 100.0) {
      Serial.println("Bad temp reading ignored (" +
        String(temperature, 1) + "C) — waiting for sensor...");
      return;
    }

    float soc = ((voltage - MIN_VOLTAGE) /
                 (MAX_VOLTAGE - MIN_VOLTAGE)) * 100.0;
    if (soc > 100.0) soc = 100.0;
    if (soc < 0.0)   soc = 0.0;

    Serial.printf("Voltage: %.2fV | Temp: %.1fC | SoC: %.0f%% | Load: %s\n",
      voltage, temperature, soc,
      relayTriggered ? "DISCONNECTED(LED off)" : "CONNECTED(LED on)");

    // ── Publish ───────────────────────────────
    StaticJsonDocument<200> doc;
    doc["voltage"]     = String(voltage, 2);
    doc["temperature"] = String(temperature, 1);
    doc["soc"]         = String(soc, 0);
    doc["relay"]       = relayTriggered ? "OPEN" : "CLOSED";

    char payload[200];
    serializeJson(doc, payload);
    mqtt.publish(TOPIC_DATA, payload);
    Serial.print("Published: ");
    Serial.println(payload);

    // ── Anomaly detection ─────────────────────
    bool telegramReady = (now - lastTelegram >= TELEGRAM_COOLDOWN);

    if (!relayTriggered) {

      if (voltage > MAX_VOLTAGE) {
        digitalWrite(RELAY_PIN, RELAY_FAULT);  // NC open → LED off
        relayTriggered = true;
        faultType      = "OVERVOLTAGE";
        aalec.set_rgb_strip(0, 255, 0, 0);
        aalec.play(1000, 500);

        StaticJsonDocument<150> alert;
        alert["type"]   = "OVERVOLTAGE";
        alert["value"]  = String(voltage, 2);
        alert["relay"]  = "OPEN";
        alert["action"] = "Load disconnected - ACK required";
        char alertBuf[200];
        serializeJson(alert, alertBuf);
        mqtt.publish(TOPIC_ALERT, alertBuf);
        Serial.println("*** OVERVOLTAGE — load DISCONNECTED — LED off ***");

        if (telegramReady) {
          sendTelegram("OVERVOLTAGE%20ALERT%21%0AVoltage%3A%20"
            + String(voltage, 2) + "V%0ATemp%3A%20"
            + String(temperature, 1) + "C%0ALoad%3A%20DISCONNECTED%0AACK%20required");
          lastTelegram = now;
        }

      } else if (voltage < MIN_VOLTAGE) {
        digitalWrite(RELAY_PIN, RELAY_FAULT);  // NC open → LED off
        relayTriggered = true;
        faultType      = "UNDERVOLTAGE";
        aalec.set_rgb_strip(0, 255, 0, 0);
        aalec.play(800, 500);

        StaticJsonDocument<150> alert;
        alert["type"]   = "UNDERVOLTAGE";
        alert["value"]  = String(voltage, 2);
        alert["relay"]  = "OPEN";
        alert["action"] = "Load disconnected - ACK required";
        char alertBuf[200];
        serializeJson(alert, alertBuf);
        mqtt.publish(TOPIC_ALERT, alertBuf);
        Serial.println("*** UNDERVOLTAGE — load DISCONNECTED — LED off ***");

        if (telegramReady) {
          sendTelegram("UNDERVOLTAGE%20ALERT%21%0AVoltage%3A%20"
            + String(voltage, 2) + "V%0ATemp%3A%20"
            + String(temperature, 1) + "C%0ALoad%3A%20DISCONNECTED%0AACK%20required");
          lastTelegram = now;
        }

      } else if (temperature > MAX_TEMP) {
        digitalWrite(RELAY_PIN, RELAY_FAULT);  // NC open → LED off
        relayTriggered = true;
        faultType      = "OVERTEMPERATURE";
        aalec.set_rgb_strip(0, 255, 0, 0);
        aalec.play(1200, 500);

        StaticJsonDocument<150> alert;
        alert["type"]   = "OVERTEMPERATURE";
        alert["value"]  = String(temperature, 1);
        alert["relay"]  = "OPEN";
        alert["action"] = "Load disconnected - ACK required";
        char alertBuf[200];
        serializeJson(alert, alertBuf);
        mqtt.publish(TOPIC_ALERT, alertBuf);
        Serial.println("*** OVERTEMPERATURE — load DISCONNECTED — LED off ***");

        if (telegramReady) {
          sendTelegram("OVERTEMPERATURE%20ALERT%21%0ATemp%3A%20"
            + String(temperature, 1) + "C%0AVoltage%3A%20"
            + String(voltage, 2) + "V%0ALoad%3A%20DISCONNECTED%0AACK%20required");
          lastTelegram = now;
        }

      } else {
        // Normal — relay off → NC closed → LED ON → load running
        aalec.set_rgb_strip(0, 0, 255, 0);
        digitalWrite(RELAY_PIN, RELAY_NORMAL);
      }
    }
  }
}
# IoT Battery Cell Monitor with Anomaly Detection and Closed-Loop Relay Protection

A distributed IoT monitoring and protective-control system inspired by production EV
Battery Management System (BMS) architectures. The system senses a simulated battery
cell, publishes live telemetry over MQTT, detects voltage and temperature anomalies,
physically disconnects a load via relay on fault, and exposes a two-way web dashboard
for remote supervision and safety-validated recovery.

**Author:** Blesson Biju — Hochschule Aalen, Internet of Things Project Module

---

## 1. Overview

| | |
|---|---|
| **Controller** | AALeC V3 board (ESP8266 / NodeMCU) |
| **Sensing** | Onboard potentiometer (simulated cell voltage), onboard BME280/680 (temperature) |
| **Actuation** | 5V single-channel relay (optocoupler), LED + resistor as simulated load |
| **Communication** | MQTT over WiFi, broker: `broker.hivemq.com` |
| **Supervisory interface** | Browser dashboard (`bms_dashboard.html`), connects via MQTT over WebSocket |

The firmware runs a continuous sense → publish → evaluate → (act) loop. On an
anomaly, the relay opens immediately and the fault is latched — it will **not**
self-clear. Recovery requires an explicit `ACKNOWLEDGE` command from the dashboard,
and even then the firmware independently re-measures the cell before closing the
relay, rejecting the request if the unsafe condition is still present.

---

## 2. Repository Structure

```
.
├── main.cpp              # ESP8266 firmware (PlatformIO project source)
├── platformio.ini         # Build configuration and library dependencies
├── bms_dashboard.html     # Standalone web dashboard (open directly in a browser)
├── photos/
│   ├── wiring.jpeg        # Physical wiring of relay, LED load, and AALeC board
│   └── dashboard.png      # Dashboard captured live during a fault condition
└── README.md              # This file
```

---

## 3. Hardware

| Component | Purpose |
|---|---|
| AALeC V3 (ESP8266) | Main controller — WiFi, RGB LEDs, OLED, buzzer, onboard sensors |
| Onboard potentiometer (A0) | Simulates scaled cell-voltage signal, 0–1023 → 3.0–4.2 V |
| Onboard BME280/680 | Temperature sensing via I2C (`aalec.get_temp()`) |
| 5V relay module (active-LOW, optocoupler) | Physically disconnects the load on fault |
| LED + current-limiting resistor | Simulated load; visually indicates connected/disconnected state |

### Pin Assignments

| Signal | Pin | Interface |
|---|---|---|
| Cell voltage (potentiometer) | `A0` | Analog (ADC) |
| Temperature (BME280/680) | `SDA` / `SCL` | I2C |
| Relay control | `D5` (GPIO14) | Digital output |
| RGB status indicator | onboard | NeoPixel |
| Buzzer (local alert) | `PIN_BEEPER` | PWM tone |

> **Note:** `D5` / GPIO14 coincides with the AALeC rotary-encoder track on this
> board revision (`PIN_ENCODER_TRACK_2`). No conflict was observed during testing,
> but this should be re-verified before reusing this pin assignment on a different
> board revision or if encoder functionality is required simultaneously.

### Wiring

See `photos/wiring.jpeg`.

```
Relay control side:
  AALeC 5V   → Relay +DC
  AALeC GND  → Relay −DC
  AALeC D5   → Relay IN

Relay load side (simulated cell load):
  AALeC 3.3V → LED (+)
  LED (−)    → resistor → Relay COM
  Relay NC   → AALeC GND
```

This wiring uses the relay's **NC** (normally closed) contact, so the LED is **ON**
in the normal/closed state and turns **OFF** when the relay opens — representing the
cell load being disconnected.

---

## 4. Software Requirements

- [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE](https://platformio.org/) extension
- ESP8266 board package (`espressif8266`), installed automatically by PlatformIO from `platformio.ini`
- A modern browser (Chrome, Edge, Firefox) for the dashboard — no installation required

### Libraries (declared in `platformio.ini`)

| Library | Purpose |
|---|---|
| `AALeC-V3` | Board driver: sensors, RGB LEDs, OLED, buzzer (installed from GitHub source) |
| `PubSubClient` | MQTT publish/subscribe client |
| `ArduinoJson` | Construction and parsing of JSON payloads |
| `ESP8266WiFi` | Bundled with the ESP8266 board package |

---

## 5. Setup and Configuration

1. **Clone or copy this repository** into a folder of your choice.

2. **Open the folder in VS Code** with the PlatformIO extension installed. PlatformIO
   will detect `platformio.ini` automatically.

3. **Set your own credentials** at the top of `main.cpp` — these are intentionally
   *not* committed with real values:
   ```cpp
   const char* WIFI_SSID     = "YourWiFiName";
   const char* WIFI_PASSWORD = "YourWiFiPassword";
   ```

4. **Set a unique MQTT topic prefix.** The public HiveMQ broker is shared by anyone
   in the world; replace `blesson` with your own identifier to avoid topic
   collisions:
   ```cpp
   const char* TOPIC_DATA    = "bms/<your-name>/data";
   const char* TOPIC_ALERT   = "bms/<your-name>/alert";
   const char* TOPIC_COMMAND = "bms/<your-name>/command";
   const char* TOPIC_STATUS  = "bms/<your-name>/status";
   ```

5. **Wire the hardware** as described in Section 3 and shown in `photos/wiring.jpeg`.

6. **Build and upload:**
   - PlatformIO → Build
   - PlatformIO → Upload (via USB)
   - If `esptool` reports *"Timed out waiting for packet header"*, close any open
     Serial Monitor first, then retry and manually pulse the board's RST button
     the moment "Connecting..." appears.

7. **Open the Serial Monitor** (115200 baud) to confirm WiFi and MQTT connect
   successfully and telemetry is being published.

8. **Open `bms_dashboard.html`** directly in a browser (double-click the file, or
   serve it from anywhere) on a device connected to the internet. It will connect
   to HiveMQ automatically — no server setup required. Make sure the topic prefix
   inside the HTML file (`bms/<your-name>/...`) matches what you set in `main.cpp`.

---

## 6. MQTT Topic Design

| Topic | Direction | Payload |
|---|---|---|
| `bms/<name>/data` | Device → Dashboard | `voltage`, `temperature`, `soc`, `relay` |
| `bms/<name>/alert` | Device → Dashboard | fault `type`, measured `value`, required `action` |
| `bms/<name>/command` | Dashboard → Device | `ACKNOWLEDGE`, `BEEP`, `LED_RED`/`GREEN`/`BLUE`, `RELAY_OPEN`/`RELAY_CLOSE` |
| `bms/<name>/status` | Device → Dashboard | `ONLINE`, `RELAY_OPEN`, `RELAY_CLOSED`, `ACK_REJECTED` (with reason) |

Broker: `broker.hivemq.com` — port `1883` (device, raw MQTT) / port `8884` (browser,
MQTT over WebSocket with TLS). This is a public, unauthenticated broker; it is
suitable for development and demonstration but not for production or sensitive data.

---

## 7. Operating Logic

### Anomaly thresholds (Li-ion chemistry limits)

| Condition | Threshold |
|---|---|
| Overvoltage | > 4.2 V |
| Undervoltage | < 3.0 V |
| Overtemperature | > 45 °C |

### Fault and recovery sequence

1. An anomaly is detected → relay opens (load disconnected) → RGB LED turns red →
   buzzer sounds → alert published to `bms/<name>/alert`.
2. The fault is **latched**: it will not clear on its own, even if the reading
   returns to a safe range on a later loop iteration.
3. An operator sends `ACKNOWLEDGE` from the dashboard.
4. The firmware **re-samples voltage and temperature at that exact moment**.
   - If still unsafe → the request is **rejected**, the reason and current readings
     are published to `bms/<name>/status` as `ACK_REJECTED`, and the relay remains
     open.
   - If genuinely safe → the relay closes, the RGB LED returns to green, and
     `RELAY_CLOSED` / `fault: CLEARED` is published.

This re-validation is a deliberate design choice: acknowledgement silences the
alert but never overrides the hardware safety interlock by itself.

---

## 8. Known Issues and Limitations

- **D5/GPIO14 pin sharing** with the AALeC rotary-encoder track (see Section 3).
- **Public, unauthenticated MQTT broker** — suitable for coursework/demo only.
- **No deep-sleep power saving** in this configuration; the board remains fully
  powered to keep the relay logic and dashboard connection continuously responsive.
  (A deep-sleep variant with RTC-memory-persisted state was prototyped separately
  but is not part of this submission.)
- **Single simulated cell** — the architecture is designed to generalise to
  multiple cells/nodes (distinct MQTT topic prefixes per device) but only one
  node is implemented here.

---

## 9. Credits

- Board and library: AALeC V3, Hochschule Aalen (`informatik-aalen/AALeC-V3`)
- MQTT broker: HiveMQ public test broker
- MQTT JavaScript client: Eclipse Paho

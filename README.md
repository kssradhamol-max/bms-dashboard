# IoT Battery Cell Monitor with Anomaly Detection and Closed-Loop Relay Protection

A distributed IoT monitoring and protective-control system inspired by production EV
Battery Management System (BMS) architectures. The system senses a simulated battery
cell, publishes live telemetry over MQTT, detects voltage and temperature anomalies,
physically disconnects a load via relay on fault, and exposes a two-way web dashboard
for remote supervision and safety-validated recovery. Critical alerts are also pushed
to a Telegram chat.

**Author:** Blesson Biju,Sradhamol Keeramparambil Shajeevan — Hochschule Aalen, Internet of Things Project Module

---

## 1. Overview

| | |
|---|---|
| **Controller** | AALeC V3 board (ESP8266 / NodeMCU) |
| **Sensing** | Onboard potentiometer (simulated cell voltage), onboard BME280/680 (temperature) |
| **Actuation** | 5V relay module (optocoupler), LED + resistor as simulated load |
| **Communication** | MQTT over WiFi, broker: `broker.hivemq.com` |
| **Notifications** | Telegram Bot API (push alerts on fault, ACK result, boot) |
| **Supervisory interface** | Browser dashboard (`bms_dashboard.html`), connects via MQTT over WebSocket |

The firmware runs a continuous sense → publish → evaluate → (act) loop. On an
anomaly, the relay opens immediately and the fault is **latched** — it will not
clear on its own, even if the reading later returns to a safe range. Recovery
requires an explicit `ACKNOWLEDGE` command from the dashboard, and even then the
firmware independently re-measures the cell at that exact moment before closing
the relay, rejecting the request if the unsafe condition is still present.

---

## 2. Repository Structure

```
.
├── main.cpp              # ESP8266 firmware (PlatformIO project source)
├── platformio.ini         # Build configuration and library dependencies
├── bms_dashboard.html     # Standalone web dashboard (open directly in a browser)
├── secrets.h.example      # Template for credentials — copy to secrets.h (gitignored)
├── .gitignore
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
| 5V relay module (optocoupler) | Physically disconnects the load on fault |
| LED + current-limiting resistor | Simulated load; visually indicates connected/disconnected state |

### Pin Assignments

| Signal | Pin | Interface |
|---|---|---|
| Cell voltage (potentiometer) | `A0` | Analog (ADC) |
| Temperature (BME280/680) | `SDA` / `SCL` | I2C |
| Relay control | `D5` (GPIO14) | Digital output |
| RGB status indicator | onboard | NeoPixel |
| Buzzer (local alert) | `PIN_BEEPER` | PWM tone |

> **Note:** `D1`/`D2` (GPIO5/GPIO4) are this board's I2C bus and must not be used
> for the relay. `D0` is reserved for the deep-sleep wake circuit (GPIO16→RST),
> not used in this configuration. `D5` (GPIO14) was confirmed free of conflicts
> with any AALeC peripheral and is used for relay control.

### Wiring

See `photos/wiring.jpeg`.

```
Relay control side:
  AALeC 5V   → Relay +DC
  AALeC GND  → Relay −DC
  AALeC D5   → Relay IN

Relay load side (simulated cell load, using the NC contact):
  AALeC 3.3V → LED (+)
  LED (−)    → resistor → Relay COM
  Relay NC   → AALeC GND
```

This wiring uses the relay's **NC** (normally closed) contact, so the LED is **ON**
in the relay's resting/de-energized state and turns **OFF** when the relay is
energized to open the fault — representing the cell's load being disconnected.

> **Relay polarity:** this specific relay module is **active-LOW** — `LOW` on `IN`
> energizes the coil (opens NC), `HIGH` de-energizes it (closes NC). This was
> confirmed empirically with a standalone test sketch, not assumed from the
> module's marketing description, since "high/low level trigger" boards can vary.

---

## 4. Software Requirements

- [VS Code](https://code.visualstudio.com/) with the [PlatformIO IDE](https://platformio.org/) extension
- ESP8266 board package (`espressif8266`), installed automatically by PlatformIO from `platformio.ini`
- A modern browser for the dashboard — no installation required
- A Telegram account, if you want push notifications (see Section 5)

### Libraries (declared in `platformio.ini`)

| Library | Purpose |
|---|---|
| `AALeC-V3` | Board driver: sensors, RGB LEDs, OLED, buzzer (installed from GitHub source) |
| `PubSubClient` | MQTT publish/subscribe client |
| `ArduinoJson` | Construction and parsing of JSON payloads |
| `ESP8266WiFi` | Bundled with the ESP8266 board package |
| `ESP8266HTTPClient` / `WiFiClientSecure` | Bundled; used for the Telegram Bot API HTTPS calls |

---

## 5. Setup and Configuration

1. **Clone this repository.**

2. **Open the folder in VS Code** with the PlatformIO extension installed.

3. **Create `secrets.h`** by copying `secrets.h.example` and filling in your own values:
   ```cpp
   #define WIFI_SSID     "YourWiFiName"
   #define WIFI_PASSWORD "YourWiFiPassword"
   #define BOT_TOKEN     "YourTelegramBotToken"
   #define CHAT_ID       "YourTelegramChatId"
   ```
   `secrets.h` is listed in `.gitignore` and is never committed.

   To get a Telegram bot token: message **@BotFather** on Telegram and follow the
   prompts to create a bot. To get your chat ID: message **@userinfobot**.

4. **Set a unique MQTT topic prefix** in `main.cpp`. The public HiveMQ broker is
   shared by anyone in the world; using a distinctive prefix avoids collisions:
   ```cpp
   const char* TOPIC_DATA    = "bms/<your-name>/data";
   const char* TOPIC_ALERT   = "bms/<your-name>/alert";
   const char* TOPIC_COMMAND = "bms/<your-name>/command";
   const char* TOPIC_STATUS  = "bms/<your-name>/status";
   ```
   Update the matching topic strings inside `bms_dashboard.html` to the same prefix.

5. **Wire the hardware** as described in Section 3 and shown in `photos/wiring.jpeg`.

6. **Build and upload:**
   - PlatformIO → Build
   - PlatformIO → Upload (via USB)
   - If `esptool` reports *"Timed out waiting for packet header"*, close any open
     Serial Monitor first, then retry and manually pulse the board's RST button
     the moment "Connecting..." appears.

7. **Open Serial Monitor** (115200 baud) to confirm WiFi/MQTT connect and telemetry
   is publishing.

8. **Open `bms_dashboard.html`** directly in a browser (double-click the file) on
   any device with internet access. It connects to HiveMQ automatically — the
   device and the dashboard do **not** need to be on the same network.

---

## 6. MQTT Topic Design

| Topic | Direction | Payload |
|---|---|---|
| `bms/<name>/data` | Device → Dashboard | `voltage`, `temperature`, `soc`, `relay` |
| `bms/<name>/alert` | Device → Dashboard | fault `type`, measured `value`, required `action` |
| `bms/<name>/command` | Dashboard → Device | `ACKNOWLEDGE`, `BEEP`, `LED_RED`/`GREEN`/`BLUE`, `RELAY_OPEN`/`RELAY_CLOSE` |
| `bms/<name>/status` | Device → Dashboard | `ONLINE`, `RELAY_OPEN`, `RELAY_CLOSED`, `ACK_REJECTED` (with reason) |

Broker: `broker.hivemq.com` — port `1883` (device, raw MQTT) / port `8884` (browser,
MQTT over WebSocket with TLS). This is a public, unauthenticated broker, suitable
for development and demonstration but not for production or sensitive data. Because
both the device and the dashboard connect outward to this broker independently,
**they never need to share a network** — only the topic prefix needs to match.

---

## 7. Operating Logic

### Anomaly thresholds (Li-ion chemistry limits)

| Condition | Threshold |
|---|---|
| Overvoltage | > 4.2 V |
| Undervoltage | < 3.0 V |
| Overtemperature | > 45 °C |

These are not arbitrary: 4.2V and 3.0V correspond to the safe electrochemical
window of LiCoO2/NMC-family lithium-ion cells; 45°C is a conservative margin
below the range where internal side-reaction rates begin accelerating sharply.

### Fault and recovery sequence

1. An anomaly is detected → relay opens (load disconnected) → RGB LED turns red →
   buzzer sounds → alert published to `bms/<name>/alert` → a Telegram message is
   queued (rate-limited to once per 30 seconds per fault type).
2. The fault is **latched**: it will not clear on its own.
3. An operator sends `ACKNOWLEDGE` from the dashboard.
4. The firmware **re-samples voltage and temperature at that exact moment**.
   - Still unsafe → the request is **rejected**; the reason and current readings
     are published to `bms/<name>/status` as `ACK_REJECTED`; relay stays open.
   - Genuinely safe → relay closes, RGB LED returns to green, `RELAY_CLOSED` /
     `fault: CLEARED` is published.

Acknowledgement silences the alert; it never overrides the hardware safety
interlock by itself.

### Telegram notifications

`sendTelegram()` performs a blocking HTTPS request. Calling it directly from
inside the MQTT command callback was found to risk starving the ESP8266's
watchdog timer and triggering an unexpected reset. All Telegram sends are
therefore **queued** (`queueTelegram()`) and the actual network call is made
from the end of `loop()`, after the sensor-reading block — never from inside
`mqttCallback()` itself.

---

## 8. Known Issues and Limitations

- **Intermittent I2C read failures on temperature.** Serial Monitor occasionally
  logs `I2C error code _burstRead error: N` and the temperature reading falls
  back to `0`. Voltage-based anomaly detection and relay protection are
  unaffected, since they do not depend on this reading. Root cause not fully
  isolated at time of writing — candidates include physical disturbance of the
  I2C wiring during relay assembly, or bus timing sensitivity; under active
  investigation.
- **State of Charge (SoC) is a simplified linear approximation** — the
  percentage represents where voltage sits between `MIN_VOLTAGE` and
  `MAX_VOLTAGE`, not a true energy measurement. Real cells have a non-linear
  discharge curve; a production system would use coulomb counting or a
  chemistry-specific voltage lookup table.
- **No temporal filtering on anomaly detection.** A single noisy reading can
  trigger a fault or be checked during an ACK. A multi-sample filtering
  approach was prototyped but found to introduce watchdog-reset risk under
  certain I2C retry conditions within the available development time; the
  simpler single-reading check was retained for reliability. Documented here
  as a known direction for future improvement.
- **Public, unauthenticated MQTT broker** — suitable for coursework/demo only.
- **No deep-sleep power saving** in the final configuration. A deep-sleep
  variant (GPIO16→RST wake circuit, RTC-memory state) was prototyped and
  partially debugged separately, but was not carried into the final firmware:
  deep sleep requires the device to be unreachable most of the time, which is
  fundamentally incompatible with the two-way, immediately-responsive
  supervisory control (`ACKNOWLEDGE`, manual relay/LED commands) implemented
  here.
- **Single simulated cell** — the topic-prefix design generalizes to multiple
  nodes, but only one node is implemented in this submission.

---

## 9. Credits

- Board and library: AALeC V3, Hochschule Aalen (`informatik-aalen/AALeC-V3`)
- MQTT broker: HiveMQ public test broker
- MQTT JavaScript client: Eclipse Paho
- Notifications: Telegram Bot API

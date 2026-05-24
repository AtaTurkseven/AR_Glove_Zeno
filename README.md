# AR_Glove_Zeno

**AR_Glove_Zeno** is a wearable gesture-control prototype for Zeno AR glasses.

It uses an **MPU6050 mounted on the index finger** to detect hand/finger motion, sends motion data wirelessly using **ESP-NOW**, and displays an animated radial action menu on an **SSD1306 OLED receiver**.

The goal is **not** to build a slow glove keyboard. The goal is to create a fast AR-style controller for actions like:

- Capture
- Ask Zeno
- Back
- Close
- Select
- Navigate

This project is an early input-system prototype for a future Zeno AR glasses interface.

---

## Concept

Typing with a glove would be slow and awkward. Instead, the glove acts as a **gesture-based action controller**.

```txt
double-circle gesture  -> open radial menu
tilt hand/finger       -> select an option
dip gesture            -> confirm selected option
OLED receiver          -> shows animated menu feedback
Serial output          -> sends action events to Zeno/laptop
```

Example action output:

```json
{"action":"CAPTURE"}
{"action":"ASK"}
{"action":"BACK"}
{"action":"CLOSE"}
```

Later, these actions can be connected to Zeno, a camera system, or an AR HUD.

---

## Architecture

```txt
[Glove ESP32 + MPU6050]
        |
        | ESP-NOW motion packet
        v
[Receiver ESP32 + SSD1306 OLED]
        |
        | Serial JSON actions
        v
[Zeno / Laptop / Future AR System]
```

---

## Hardware

### Glove / Sender Side

- ESP32 development board
- MPU6050 IMU
- Battery or portable power source
- Wearable glove or index-finger mount

### Receiver Side

- ESP32 development board
- 128×64 SSD1306 OLED display
- USB connection to laptop for debugging and action output

---

## Wiring

### MPU6050 to Sender ESP32

```txt
MPU6050 VCC  -> ESP32 3.3V
MPU6050 GND  -> ESP32 GND
MPU6050 SDA  -> ESP32 GPIO21
MPU6050 SCL  -> ESP32 GPIO22
```

### SSD1306 OLED to Receiver ESP32

```txt
OLED VCC  -> ESP32 3.3V
OLED GND  -> ESP32 GND
OLED SDA  -> ESP32 GPIO21
OLED SCL  -> ESP32 GPIO22
```

---

## Features

- ESP-NOW wireless motion transmission
- MPU6050 motion tracking
- OLED HUD feedback
- Animated radial menu
- Double-circle menu activation gesture
- Tilt-based option selection
- Dip-based confirm gesture
- Serial JSON action output
- Zeno-ready action bridge

---

## Current Menu System

The receiver displays a 4-option radial menu:

```txt
        ASK

BACK          CAP

       CLOSE
```

### Controls

```txt
Draw 2 circles  -> open menu
Tilt up         -> select ASK
Tilt right      -> select CAPTURE
Tilt left       -> select BACK
Tilt down       -> select CLOSE
Dip gesture     -> confirm selected action
```

---

## ESP-NOW Packet Format

The sender transmits this packet structure:

```cpp
typedef struct MotionPacket {
  uint32_t seq;
  uint32_t timestamp_ms;

  float ax;
  float ay;
  float az;

  float gx;
  float gy;
  float gz;

  float pitch;
  float roll;
  float yaw;
} MotionPacket;
```

The receiver expects the exact same structure.

---

## Arduino Requirements

Install these libraries:

```txt
Adafruit MPU6050
Adafruit Unified Sensor
Adafruit SSD1306
Adafruit GFX
```

Recommended board package:

```txt
ESP32 Arduino Core 3.3.8
```

The receiver code currently uses the ESP32 Arduino Core 3.x ESP-NOW callback style:

```cpp
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
```

For ESP32 Arduino Core 2.x, change it to:

```cpp
void onDataRecv(const uint8_t *mac, const uint8_t *data, int len)
```

---

## Why BLE Is Not Used Right Now

BLE worked in simple tests, but combining all of this on one ESP32 caused instability:

```txt
BLE HID + ESP-NOW + OLED + gesture UI
```

For now, the stable architecture is:

```txt
ESP-NOW       -> glove motion transport
OLED          -> HUD feedback
Serial JSON   -> Zeno/laptop action bridge
```

BLE may return later after the core interaction system is stable.

---

## Gesture Tuning

Important receiver-side constants:

```cpp
const float CIRCLE_GYRO_THRESHOLD = 32.0f;
const float DOUBLE_CIRCLE_TARGET_DEG = 620.0f;
const float SELECT_DEADZONE_DEG = 9.0f;
const float CONFIRM_AY_THRESHOLD = 8.0f;
```

### If the menu is hard to open

Lower:

```cpp
DOUBLE_CIRCLE_TARGET_DEG
```

Example:

```cpp
450.0f
```

### If the menu opens randomly

Increase:

```cpp
DOUBLE_CIRCLE_TARGET_DEG
```

Example:

```cpp
750.0f
```

### If selection changes too easily

Increase:

```cpp
SELECT_DEADZONE_DEG
```

Example:

```cpp
13.0f
```

### If confirm triggers accidentally

Increase:

```cpp
CONFIRM_AY_THRESHOLD
```

Example:

```cpp
10.5f
```

---

## Zeno Integration Example

Receiver output:

```json
{"action":"CAPTURE"}
```

Zeno/laptop can interpret this as:

```txt
1. Take camera frame
2. Analyze image
3. Show or speak the answer
```

Possible action mapping:

```txt
CAPTURE -> take image
ASK     -> ask Zeno about current context
BACK    -> close current panel
CLOSE   -> hide menu
```

---

## Roadmap

### Version 1

- Stable ESP-NOW motion transmission
- OLED animated radial menu
- Gesture-based menu open/select/confirm
- Serial JSON action output

### Version 2

- Python/Zeno bridge reads serial actions
- `CAPTURE` triggers webcam/camera snapshot
- `ASK` sends current context to Zeno
- `BACK` / `CLOSE` controls Zeno HUD state

### Version 3

- Add action profiles:
  - Builder mode
  - Camera mode
  - Coding mode
  - Electronics mode

### Version 4

- Improve gesture detection
- Add calibration screen
- Add battery/status display
- Add haptic feedback
- Revisit optional BLE HID support

---

## Design Philosophy

This project is not trying to replace a keyboard.

The glove is for fast AR control:

```txt
gesture = intent
voice   = language
Zeno    = intelligence
OLED    = feedback
```

That is the useful direction.

---

## Project Status

Prototype stage.

Current goal:

```txt
Make the glove feel reliable for 5 minutes of real use.
```

Before adding more AI, displays, or complex features, the core gesture system must be stable and comfortable.

---

## Name

**AR_Glove_Zeno**  
A wearable gesture controller for Zeno AR glasses.

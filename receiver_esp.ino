/*
  AR_Glove_Zeno - Receiver ESP32

  Hardware:
  - ESP32
  - SSD1306 128x64 OLED on I2C

  Function:
  - Receives MPU6050 motion data over ESP-NOW
  - Detects a two-circle gesture to open a radial menu
  - Uses tilt to select actions
  - Uses a dip gesture to confirm
  - Shows animated OLED UI
  - Prints JSON actions over Serial for Zeno/laptop integration

  Target:
  - ESP32 Arduino Core 3.3.8
*/

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_now.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <math.h>

// =============================
// OLED pins
// =============================
#define I2C_SDA 21
#define I2C_SCL 22

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =============================
// ESP-NOW settings
// Must match sender channel
// =============================
#define ESPNOW_CHANNEL 1

// =============================
// ESP-NOW packet
// Must match sender exactly
// =============================
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

MotionPacket latestPacket;
MotionPacket workingPacket;

volatile bool hasNewPacket = false;
volatile uint32_t rxCounter = 0;

portMUX_TYPE packetMux = portMUX_INITIALIZER_UNLOCKED;

uint32_t packetCount = 0;
unsigned long lastPacketMs = 0;

// =============================
// UI states
// =============================
enum UIState {
  IDLE,
  MENU_OPENING,
  MENU_ACTIVE,
  ACTION_FLASH,
  MENU_CLOSING
};

UIState uiState = IDLE;
unsigned long stateStartMs = 0;

// =============================
// Gesture detection tuning
// =============================
// Lower this if the menu is hard to open.
const float CIRCLE_GYRO_THRESHOLD = 32.0f;

// Two full circles is 720 deg. 620 makes it easier to trigger.
const float DOUBLE_CIRCLE_TARGET_DEG = 620.0f;

const unsigned long MIN_CIRCLE_TIME_MS = 450;
const unsigned long MAX_CIRCLE_TIME_MS = 2500;
const unsigned long GESTURE_IDLE_RESET_MS = 280;

bool circleTracking = false;
float prevCircleAngle = 0.0f;
float circleAccumDeg = 0.0f;
unsigned long circleStartMs = 0;
unsigned long lastCircleMotionMs = 0;
int circleSamples = 0;

// =============================
// Menu selection tuning
// =============================
float neutralPitch = 0.0f;
float neutralRoll = 0.0f;

const float SELECT_DEADZONE_DEG = 9.0f;
const float CONFIRM_AY_THRESHOLD = 8.0f;
const unsigned long CONFIRM_COOLDOWN_MS = 850;

unsigned long lastConfirmMs = 0;

int selectedIndex = 0;
String lastAction = "NONE";

// If selection feels reversed, flip these.
const int SELECT_X_INVERT = 1;
const int SELECT_Y_INVERT = -1;

// =============================
// Animation timing
// =============================
const unsigned long OPEN_ANIM_MS = 350;
const unsigned long CLOSE_ANIM_MS = 250;
const unsigned long ACTION_FLASH_MS = 600;

// =============================
// Menu items
// =============================
struct MenuItem {
  const char* label;
  const char* action;
  int x;
  int y;
};

MenuItem menuItems[] = {
  {"ASK",   "ASK",     64, 15},
  {"CAP",   "CAPTURE", 104, 36},
  {"BACK",  "BACK",    24, 36},
  {"CLOSE", "CLOSE",   64, 56}
};

const int MENU_COUNT = sizeof(menuItems) / sizeof(menuItems[0]);

// =============================
// ESP-NOW receive callback
// ESP32 Arduino Core 3.3.x style
// =============================
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(MotionPacket)) {
    return;
  }

  portENTER_CRITICAL_ISR(&packetMux);
  memcpy(&latestPacket, data, sizeof(MotionPacket));
  hasNewPacket = true;
  rxCounter++;
  portEXIT_CRITICAL_ISR(&packetMux);
}

// =============================
// Helpers
// =============================
float normalizeAngleDeg(float a) {
  while (a > 180.0f) a -= 360.0f;
  while (a < -180.0f) a += 360.0f;
  return a;
}

float clamp01(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

float easeOutBack(float t) {
  t = clamp01(t);
  float c1 = 1.70158f;
  float c3 = c1 + 1.0f;
  return 1.0f + c3 * pow(t - 1.0f, 3) + c1 * pow(t - 1.0f, 2);
}

void resetCircleDetector() {
  circleTracking = false;
  prevCircleAngle = 0.0f;
  circleAccumDeg = 0.0f;
  circleStartMs = 0;
  lastCircleMotionMs = 0;
  circleSamples = 0;
}

// =============================
// Menu state control
// =============================
void openMenu() {
  uiState = MENU_OPENING;
  stateStartMs = millis();

  neutralPitch = workingPacket.pitch;
  neutralRoll = workingPacket.roll;

  selectedIndex = 0;
  lastAction = "MENU";

  resetCircleDetector();

  Serial.println("{\"event\":\"menu_open\"}");
}

void closeMenu() {
  uiState = MENU_CLOSING;
  stateStartMs = millis();
  Serial.println("{\"event\":\"menu_close\"}");
}

void sendAction(const char* action) {
  lastAction = action;

  Serial.print("{\"action\":\"");
  Serial.print(action);
  Serial.println("\"}");

  uiState = ACTION_FLASH;
  stateStartMs = millis();
}

// =============================
// Double-circle gesture detection
// =============================
void updateCircleGesture() {
  if (uiState != IDLE) return;

  unsigned long now = millis();

  // Use gx/gz gyro vector direction as the gesture signature.
  float gx = workingPacket.gx;
  float gz = workingPacket.gz;

  float mag = sqrt(gx * gx + gz * gz);

  if (mag > CIRCLE_GYRO_THRESHOLD) {
    float angle = atan2(gx, gz) * 57.2958f;

    if (!circleTracking) {
      circleTracking = true;
      prevCircleAngle = angle;
      circleAccumDeg = 0.0f;
      circleStartMs = now;
      lastCircleMotionMs = now;
      circleSamples = 0;
      return;
    }

    float delta = normalizeAngleDeg(angle - prevCircleAngle);
    prevCircleAngle = angle;

    // Ignore insane jumps caused by noise.
    if (abs(delta) < 120.0f) {
      circleAccumDeg += delta;
      circleSamples++;
    }

    lastCircleMotionMs = now;

    unsigned long duration = now - circleStartMs;

    if (
      abs(circleAccumDeg) >= DOUBLE_CIRCLE_TARGET_DEG &&
      duration >= MIN_CIRCLE_TIME_MS &&
      duration <= MAX_CIRCLE_TIME_MS &&
      circleSamples > 12
    ) {
      openMenu();
      return;
    }

    if (duration > MAX_CIRCLE_TIME_MS) {
      resetCircleDetector();
      return;
    }
  } else {
    if (circleTracking && now - lastCircleMotionMs > GESTURE_IDLE_RESET_MS) {
      resetCircleDetector();
    }
  }
}

// =============================
// Menu selection
// =============================
void updateMenuSelection() {
  if (uiState != MENU_ACTIVE) return;

  float rollDelta = (workingPacket.roll - neutralRoll) * SELECT_X_INVERT;
  float pitchDelta = (workingPacket.pitch - neutralPitch) * SELECT_Y_INVERT;

  float absX = abs(rollDelta);
  float absY = abs(pitchDelta);

  if (absX < SELECT_DEADZONE_DEG && absY < SELECT_DEADZONE_DEG) {
    return;
  }

  if (absX > absY) {
    if (rollDelta > 0) {
      selectedIndex = 1; // CAP right
    } else {
      selectedIndex = 2; // BACK left
    }
  } else {
    if (pitchDelta > 0) {
      selectedIndex = 0; // ASK up
    } else {
      selectedIndex = 3; // CLOSE down
    }
  }
}

void updateConfirmGesture() {
  if (uiState != MENU_ACTIVE) return;

  unsigned long now = millis();

  if (now - lastConfirmMs < CONFIRM_COOLDOWN_MS) {
    return;
  }

  // Crude confirm: finger/hand dips toward Y gravity side.
  // If it triggers too easily, increase CONFIRM_AY_THRESHOLD.
  if (workingPacket.ay > CONFIRM_AY_THRESHOLD) {
    lastConfirmMs = now;

    const char* action = menuItems[selectedIndex].action;
    sendAction(action);
  }
}

// =============================
// Drawing helpers
// =============================
void drawCenteredText(const char* text, int x, int y, uint8_t size, bool inverted) {
  int16_t x1, y1;
  uint16_t w, h;

  display.setTextSize(size);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);

  int tx = x - w / 2;
  int ty = y - h / 2;

  if (inverted) {
    display.fillRect(tx - 3, ty - 2, w + 6, h + 4, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
  } else {
    display.setTextColor(SSD1306_WHITE);
  }

  display.setCursor(tx, ty);
  display.print(text);
  display.setTextColor(SSD1306_WHITE);
}

void drawIdle() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Zeno Glove HUD");

  display.setCursor(94, 0);
  display.print("RX:");
  display.print(packetCount % 1000);

  display.setCursor(0, 12);
  if (millis() - lastPacketMs < 500) {
    display.print("ESP-NOW: LIVE");
  } else {
    display.print("ESP-NOW: NO DATA");
  }

  display.setCursor(0, 25);
  display.print("Draw 2 circles");

  display.setCursor(0, 36);
  display.print("to open menu");

  int progress = (int)(abs(circleAccumDeg) / DOUBLE_CIRCLE_TARGET_DEG * 100.0f);
  if (progress > 100) progress = 100;

  display.drawRect(0, 54, 128, 8, SSD1306_WHITE);
  display.fillRect(2, 56, map(progress, 0, 100, 0, 124), 4, SSD1306_WHITE);

  display.display();
}

void drawRadialMenu(float progress) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  int cx = 64;
  int cy = 36;

  float p = easeOutBack(progress);

  int ringR = 4 + (int)(18 * p);

  display.drawCircle(cx, cy, ringR, SSD1306_WHITE);
  display.drawCircle(cx, cy, ringR + 2, SSD1306_WHITE);

  display.fillCircle(cx, cy, 2, SSD1306_WHITE);

  for (int i = 0; i < MENU_COUNT; i++) {
    int targetX = menuItems[i].x;
    int targetY = menuItems[i].y;

    int ix = cx + (int)((targetX - cx) * p);
    int iy = cy + (int)((targetY - cy) * p);

    display.drawLine(cx, cy, ix, iy, SSD1306_WHITE);

    bool selected = (uiState == MENU_ACTIVE && i == selectedIndex);
    drawCenteredText(menuItems[i].label, ix, iy, 1, selected);
  }

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("MENU");

  display.setCursor(74, 0);
  display.print("Act:");
  display.print(lastAction);

  display.display();
}

void drawActionFlash() {
  display.clearDisplay();

  unsigned long elapsed = millis() - stateStartMs;
  float t = clamp01((float)elapsed / ACTION_FLASH_MS);

  int r = 4 + (int)(40 * t);

  display.drawCircle(64, 32, r, SSD1306_WHITE);
  display.drawCircle(64, 32, r / 2, SSD1306_WHITE);

  drawCenteredText(lastAction.c_str(), 64, 30, 2, false);

  display.setTextSize(1);
  display.setCursor(24, 52);
  display.print("ACTION SENT");

  display.display();
}

void drawClosing() {
  unsigned long elapsed = millis() - stateStartMs;
  float t = clamp01((float)elapsed / CLOSE_ANIM_MS);

  drawRadialMenu(1.0f - t);
}

// =============================
// UI update
// =============================
void updateUIState() {
  unsigned long now = millis();

  if (uiState == MENU_OPENING) {
    if (now - stateStartMs >= OPEN_ANIM_MS) {
      uiState = MENU_ACTIVE;
      stateStartMs = now;
    }
  }

  if (uiState == ACTION_FLASH) {
    if (now - stateStartMs >= ACTION_FLASH_MS) {
      if (lastAction == "CLOSE" || lastAction == "BACK") {
        closeMenu();
      } else {
        uiState = MENU_ACTIVE;
        stateStartMs = now;
      }
    }
  }

  if (uiState == MENU_CLOSING) {
    if (now - stateStartMs >= CLOSE_ANIM_MS) {
      uiState = IDLE;
      stateStartMs = now;
      resetCircleDetector();
      lastAction = "NONE";
    }
  }
}

void drawUI() {
  if (uiState == IDLE) {
    drawIdle();
  } else if (uiState == MENU_OPENING) {
    float t = (float)(millis() - stateStartMs) / OPEN_ANIM_MS;
    drawRadialMenu(t);
  } else if (uiState == MENU_ACTIVE) {
    drawRadialMenu(1.0f);
  } else if (uiState == ACTION_FLASH) {
    drawActionFlash();
  } else if (uiState == MENU_CLOSING) {
    drawClosing();
  }
}

// =============================
// Setup OLED
// =============================
void setupOLED() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed at 0x3C");
    while (true) {
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Zeno Glove HUD");
  display.println("Waiting ESP-NOW...");
  display.display();
}

// =============================
// Setup ESP-NOW
// =============================
void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, false);
  WiFi.setSleep(false);

  esp_wifi_set_ps(WIFI_PS_NONE);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

  Serial.print("Receiver MAC Address: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed.");

    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("ESP-NOW FAILED");
    display.display();

    while (true) {
      delay(1000);
    }
  }

  esp_now_register_recv_cb(onDataRecv);

  Serial.println("ESP-NOW receiver ready.");
}

// =============================
// Arduino setup
// =============================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("=== AR_Glove_Zeno Receiver ESP32 ===");

  setupOLED();
  setupEspNow();

  resetCircleDetector();
  stateStartMs = millis();
}

// =============================
// Arduino loop
// =============================
void loop() {
  bool gotPacket = false;

  portENTER_CRITICAL(&packetMux);
  if (hasNewPacket) {
    memcpy(&workingPacket, &latestPacket, sizeof(MotionPacket));
    hasNewPacket = false;
    packetCount = rxCounter;
    lastPacketMs = millis();
    gotPacket = true;
  }
  portEXIT_CRITICAL(&packetMux);

  if (gotPacket) {
    updateCircleGesture();

    if (uiState == MENU_ACTIVE) {
      updateMenuSelection();
      updateConfirmGesture();
    }
  }

  updateUIState();

  static unsigned long lastDrawMs = 0;
  unsigned long now = millis();

  if (now - lastDrawMs >= 33) {
    lastDrawMs = now;
    drawUI();
  }
}

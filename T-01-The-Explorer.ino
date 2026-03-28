#include "ESP32_NOW.h"
#include "WiFi.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= OLED =================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define SDA_PIN 4
#define SCL_PIN 3

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================= ESPNOW =================
#define ESPNOW_WIFI_CHANNEL 6

// ================= SERVOS =================
#define SERVO_LEFT   5
#define SERVO_RIGHT  6

#define PWM_FREQ 50
#define PWM_RES  12

#define LEFT_NEUTRAL   1535
#define RIGHT_NEUTRAL  1505
#define MAX_OFFSET     200

typedef struct {
  int8_t throttle;
  int8_t turn;
  uint8_t flags;
} ControlPacket;

// ================= PWM =================
uint32_t usToDuty(int us) {
  float period = 1000000.0 / PWM_FREQ;
  return (us / period) * ((1 << PWM_RES) - 1);
}

void writeServos(int left_us, int right_us) {
  ledcWrite(SERVO_LEFT,  usToDuty(left_us));
  ledcWrite(SERVO_RIGHT, usToDuty(right_us));
}

// ================= DRIVE =================
int currentThrottle = 0;
int currentTurn = 0;

void drive(int throttle, int turn) {
  currentThrottle = throttle;
  currentTurn = turn;

  int left  = throttle + turn;
  int right = throttle - turn;

  left  = constrain(left,  -100, 100);
  right = constrain(right, -100, 100);

  int left_us  = LEFT_NEUTRAL  + map(left,  -100, 100, -MAX_OFFSET, MAX_OFFSET);
  int right_us = RIGHT_NEUTRAL + map(right, -100, 100,  MAX_OFFSET, -MAX_OFFSET);

  writeServos(left_us, right_us);
}

// ================= EYES =================
float curX = 0, curY = 0;
float targetX = 0, targetY = 0;

unsigned long lastMove = 0;
unsigned long lastBlink = 0;
bool blinking = false;

void drawEyes(float x, float y, bool blink) {
  display.clearDisplay();

  int eyeW = 36;
  int eyeH = blink ? 6 : 36;

  int leftX = 32;
  int rightX = 96;
  int eyeY = 32;

  display.fillRoundRect(leftX - eyeW/2 + x,
                        eyeY - eyeH/2 + y,
                        eyeW, eyeH, 10, SSD1306_WHITE);

  display.fillRoundRect(rightX - eyeW/2 + x,
                        eyeY - eyeH/2 + y,
                        eyeW, eyeH, 10, SSD1306_WHITE);

  if (!blink) {
    display.fillCircle(leftX + x, eyeY + y, 6, SSD1306_BLACK);
    display.fillCircle(rightX + x, eyeY + y, 6, SSD1306_BLACK);
  }

  display.display();
}

void updateEyes() {
  // Movement behavior based on motion
  if (millis() - lastMove > 1500) {
    if (abs(currentThrottle) > 20) {
      targetY = map(currentThrottle, -100, 100, 8, -8);
      targetX = 0;
    } else if (abs(currentTurn) > 20) {
      targetX = map(currentTurn, -100, 100, -10, 10);
      targetY = 0;
    } else {
      targetX = random(-8, 9);
      targetY = random(-5, 6);
    }
    lastMove = millis();
  }

  // Smooth movement
  curX += (targetX - curX) * 0.1;
  curY += (targetY - curY) * 0.1;

  // Blink
  if (millis() - lastBlink > 3000) {
    blinking = true;
    lastBlink = millis();
  }

  if (blinking) {
    drawEyes(curX, curY, true);
    blinking = false;
  } else {
    drawEyes(curX, curY, false);
  }
}

// ================= PEER =================
class ControllerPeer : public ESP_NOW_Peer {
public:
  ControllerPeer(const uint8_t *mac)
  : ESP_NOW_Peer(mac, ESPNOW_WIFI_CHANNEL, WIFI_IF_STA, nullptr) {}

  bool begin() {
    return add();
  }

  void onReceive(const uint8_t *data, size_t len, bool broadcast) override {
    if (len != sizeof(ControlPacket)) return;

    ControlPacket rx;
    memcpy(&rx, data, sizeof(rx));

    drive(rx.throttle, rx.turn);
  }
};

std::vector<ControllerPeer*> controllers;

// ================= NEW PEER =================
void onNewPeer(const esp_now_recv_info_t *info,
               const uint8_t *data,
               int len,
               void *arg) {

  if (memcmp(info->des_addr, ESP_NOW.BROADCAST_ADDR, 6) != 0) return;

  ControllerPeer *peer = new ControllerPeer(info->src_addr);

  if (!peer->begin()) {
    delete peer;
    return;
  }

  controllers.push_back(peer);
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  // OLED
  Wire.begin(SDA_PIN, SCL_PIN);
  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);

  // Servos
  ledcAttach(SERVO_LEFT,  PWM_FREQ, PWM_RES);
  ledcAttach(SERVO_RIGHT, PWM_FREQ, PWM_RES);

  writeServos(LEFT_NEUTRAL, RIGHT_NEUTRAL);

  // ESP-NOW
  WiFi.mode(WIFI_STA);
  WiFi.setChannel(ESPNOW_WIFI_CHANNEL);
  while (!WiFi.STA.started()) delay(10);

  if (!ESP_NOW.begin()) {
    ESP.restart();
  }

  ESP_NOW.onNewPeer(onNewPeer, nullptr);

  Serial.println("Robot ready");
}

// ================= LOOP =================
void loop() {
  updateEyes();   // non-blocking
}

// SPDX-License-Identifier: MIT
// fw-probe: the starter firmware every simulator image in this repository is
// smoke-tested with. Blink + button + UART:
//   - the UART prints "boot <mcu>" once at 115200 baud;
//   - the LED toggles every 250 ms (millis()) and each toggle prints
//     "led=<0|1> btn=<0|1> t=<ms>";
//   - the button (to ground, INPUT_PULLUP, debounced 20 ms) prints
//     "pressed" / "released".
// One source for every chip; the chip's own macros pick the pins.
#include <Arduino.h>

#if defined(CONFIG_IDF_TARGET_ESP32C3)
#define MCU_NAME "esp32c3"
static const int LED_PIN = 8, BTN_PIN = 9, TX_PIN = 21, RX_PIN = 20;
#define UART Serial0
#elif defined(CONFIG_IDF_TARGET_ESP32)
// A WROOM-32 as a DevKitC wires it: LED IO2, BOOT button IO0, UART0 on TXD0/RXD0.
#define MCU_NAME "esp32"
static const int LED_PIN = 2, BTN_PIN = 0, TX_PIN = 1, RX_PIN = 3;
#define UART Serial0
#else
#error "fw-probe: unknown MCU"
#endif

static uint32_t lastToggle = 0;
static int led = 0;
static int btnRaw = 0, btnStable = 0;  // 1 = pressed (pin LOW)
static uint32_t btnChangedAt = 0;

void setup() {
  UART.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  pinMode(BTN_PIN, INPUT_PULLUP);
  UART.println("boot " MCU_NAME);
  // The blink period starts here, not at reset: under QEMU an ESP32 reaches
  // setup() a while after reset, and counting from 0 would toggle in a burst.
  lastToggle = millis();
}

void loop() {
  uint32_t now = millis();
  int raw = digitalRead(BTN_PIN) == LOW ? 1 : 0;
  if (raw != btnRaw) {
    btnRaw = raw;
    btnChangedAt = now;
  }
  if (raw != btnStable && now - btnChangedAt >= 20) {
    btnStable = raw;
    UART.println(btnStable ? "pressed" : "released");
  }
  if (now - lastToggle >= 250) {
    lastToggle += 250;
    led ^= 1;
    digitalWrite(LED_PIN, led ? HIGH : LOW);
    char line[48];
    snprintf(line, sizeof line, "led=%d btn=%d t=%lu", led, btnStable, (unsigned long)now);
    UART.println(line);
  }
}

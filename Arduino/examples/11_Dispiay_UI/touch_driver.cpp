/* CST816D: protocol adapted from Hynitron hyn_cst8xxT.c (V2.27 bundle). */
#include "touch_driver.h"

#include "Arduino.h"
#include <Wire.h>
#include "display_config.h"
#include "touch_config.h"

namespace {
constexpr uint8_t kQueueSize = 16;
constexpr uint8_t kReportBytes = 15;  // 3-byte header + two 6-byte contacts.
constexpr uint32_t kResetLowMs = 11;
constexpr uint32_t kBootWaitMs = 120;  // CST816D datasheet requires >=100 ms.
enum class RecoveryState : uint8_t { Idle, ResetLow, BootWait };

TouchStatus g_status = {};
TouchSample g_latest = {};
TouchSample g_delivered = {};
TouchSample g_queue[kQueueSize];
uint8_t g_head = 0;
uint8_t g_count = 0;
uint8_t g_failures = 0;
bool g_bus_ready = false;
bool g_wait_all_up = false;
uint32_t g_last_read_ms = 0;
uint32_t g_recovery_ms = 0;
uint32_t g_last_range_log_ms = 0;
RecoveryState g_recovery = RecoveryState::Idle;
volatile bool g_irq_pending = false;
portMUX_TYPE g_irq_mux = portMUX_INITIALIZER_UNLOCKED;

static_assert(TOUCH_RAW_X_MAX > 0 && TOUCH_RAW_Y_MAX > 0, "Invalid TP range");

void IRAM_ATTR touch_irq() {
  portENTER_CRITICAL_ISR(&g_irq_mux);
  g_irq_pending = true;
  portEXIT_CRITICAL_ISR(&g_irq_mux);
}

bool take_irq() {
  portENTER_CRITICAL(&g_irq_mux);
  const bool pending = g_irq_pending;
  g_irq_pending = false;
  portEXIT_CRITICAL(&g_irq_mux);
  return pending;
}

bool read_registers(uint8_t reg, uint8_t *data, size_t length) {
  Wire.beginTransmission(TOUCH_I2C_ADDRESS);
  if (Wire.write(reg) != 1) {
    Wire.endTransmission(true);
    return false;
  }
  // The vendor ESP32 driver uses STOP between the register write and read.
  if (Wire.endTransmission(true) != 0) return false;
  const size_t received = Wire.requestFrom(
      TOUCH_I2C_ADDRESS, length, true);
  if (received != length) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    if (!Wire.available()) return false;
    const int value = Wire.read();
    if (value < 0) return false;
    data[i] = static_cast<uint8_t>(value);
  }
  return true;
}

bool write_register(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(TOUCH_I2C_ADDRESS);
  const size_t a = Wire.write(reg);
  const size_t b = Wire.write(value);
  const uint8_t result = Wire.endTransmission(true);
  return a == 1 && b == 1 && result == 0;
}

void enqueue(const TouchSample &sample) {
  // Coalesce moves, but retain the initial press coordinate of every gesture.
  if (sample.pressed && g_count >= 2) {
    const uint8_t tail = (g_head + g_count - 1) % kQueueSize;
    const uint8_t before = (g_head + g_count - 2) % kQueueSize;
    if (g_queue[tail].pressed && g_queue[before].pressed) {
      g_queue[tail] = sample;
      return;
    }
  }
  if (g_count == kQueueSize) {
    ++g_status.queue_overflows;
    g_head = 0;
    g_count = 0;
    // A saturated FIFO must never leave LVGL with a stuck pressed pointer.
    if (g_delivered.pressed) {
      TouchSample released = g_delivered;
      released.pressed = false;
      released.time_ms = sample.time_ms;
      g_queue[g_count++] = released;
    }
  }
  g_queue[(g_head + g_count) % kQueueSize] = sample;
  ++g_count;
}

void publish(const TouchSample &sample) {
  if (sample.pressed != g_latest.pressed ||
      (sample.pressed && (sample.x != g_latest.x || sample.y != g_latest.y))) {
    enqueue(sample);
  }
  g_latest = sample;
}

void release_touch(uint32_t now) {
  TouchSample released = g_latest;
  released.pressed = false;
  released.time_ms = now;
  publish(released);
}

bool map_coordinates(uint16_t raw_x, uint16_t raw_y,
                     uint16_t &screen_x, uint16_t &screen_y) {
  // Reject out-of-range data rather than creating phantom edge presses.
  if (raw_x > TOUCH_RAW_X_MAX || raw_y > TOUCH_RAW_Y_MAX) return false;
  uint32_t x = raw_x;
  uint32_t y = raw_y;
  uint32_t max_x = TOUCH_RAW_X_MAX;
  uint32_t max_y = TOUCH_RAW_Y_MAX;
  if (TOUCH_SWAP_XY) {
    const uint32_t t = x; x = y; y = t;
    const uint32_t m = max_x; max_x = max_y; max_y = m;
  }
  if (TOUCH_REVERSE_X) x = max_x - x;
  if (TOUCH_REVERSE_Y) y = max_y - y;
  screen_x = static_cast<uint16_t>(x * (DISPLAY_WIDTH - 1) / max_x);
  screen_y = static_cast<uint16_t>(y * (DISPLAY_HEIGHT - 1) / max_y);
  return true;
}

bool identify() {
  uint8_t info[4] = {};
  if (!read_registers(0xA7, info, sizeof(info))) return false;
  const bool all_zero = !(info[0] | info[1] | info[2] | info[3]);
  const bool all_ff = info[0] == 0xFF && info[1] == 0xFF &&
                      info[2] == 0xFF && info[3] == 0xFF;
  if (all_zero || all_ff) return false;
  // Do not hardcode a ChipID value: module firmware can vary.
  g_status.chip_id = info[0];
  g_status.project_id = info[1];
  g_status.firmware_version = info[2];
  g_status.factory_id = info[3];
  g_status.auto_sleep_disabled = false;
  if (TOUCH_TRY_DISABLE_AUTO_SLEEP) {
    uint8_t value = 0;
    g_status.auto_sleep_disabled = write_register(0xFE, 1) &&
        read_registers(0xFE, &value, 1) && value == 1;
  }
  g_failures = 0;
  g_wait_all_up = false;
  g_status.online = true;
  Serial.printf("[TP] addr=0x%02X chip=0x%02X project=0x%02X fw=0x%02X factory=0x%02X awake=%u\n",
                TOUCH_I2C_ADDRESS, info[0], info[1], info[2], info[3],
                g_status.auto_sleep_disabled ? 1U : 0U);
  return true;
}

void failed_read(uint32_t now, bool invalid) {
  if (invalid) ++g_status.invalid_frames;
  else ++g_status.io_errors;
  if (++g_failures < TOUCH_MAX_FAILURES) return;
  release_touch(now);
  g_status.online = false;
  g_recovery_ms = now;
  Serial.println("[TP] Read failed repeatedly; pointer released, retry in 2 s");
}

void recover(uint32_t now) {
  if (g_recovery == RecoveryState::Idle) {
    if (now - g_recovery_ms < TOUCH_RETRY_MS) return;
    digitalWrite(TOUCH_RST_GPIO, LOW);
    g_recovery = RecoveryState::ResetLow;
    g_recovery_ms = now;
    return;
  }
  if (g_recovery == RecoveryState::ResetLow) {
    if (now - g_recovery_ms < kResetLowMs) return;
    digitalWrite(TOUCH_RST_GPIO, HIGH);
    g_recovery = RecoveryState::BootWait;
    g_recovery_ms = now;
    return;
  }
  if (now - g_recovery_ms < kBootWaitMs) return;
  g_recovery = RecoveryState::Idle;
  g_recovery_ms = now;
  if (!g_bus_ready) {
    g_bus_ready = Wire.begin(TOUCH_SDA_GPIO, TOUCH_SCL_GPIO, TOUCH_I2C_HZ);
    Wire.setTimeOut(TOUCH_I2C_TIMEOUT_MS);
  }
  if (g_bus_ready && identify()) {
    g_last_read_ms = now - TOUCH_IDLE_POLL_MS;
    Serial.println("[TP] Recovered");
  }
}

}  // namespace

bool touch_init() {
  pinMode(TOUCH_RST_GPIO, OUTPUT);
  digitalWrite(TOUCH_RST_GPIO, HIGH);
  pinMode(TOUCH_INT_GPIO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TOUCH_INT_GPIO), touch_irq, FALLING);
  g_bus_ready = Wire.begin(TOUCH_SDA_GPIO, TOUCH_SCL_GPIO, TOUCH_I2C_HZ);
  Wire.setTimeOut(TOUCH_I2C_TIMEOUT_MS);
  digitalWrite(TOUCH_RST_GPIO, LOW);
  delay(kResetLowMs);
  digitalWrite(TOUCH_RST_GPIO, HIGH);
  delay(kBootWaitMs);
  g_status.online = false;
  for (uint8_t attempt = 0; g_bus_ready && attempt < 3; ++attempt) {
    if (identify()) return true;
    delay(10);
  }
  g_recovery_ms = millis();
  Serial.println("[TP] Not detected; display stays active, automatic retry enabled");
  return false;
}

void touch_poll() {
  const uint32_t now = millis();
  if (!g_status.online) { recover(now); return; }
  const bool irq = take_irq();
  const bool active_due = (g_latest.pressed || g_wait_all_up || g_failures != 0) &&
      now - g_last_read_ms >= TOUCH_ACTIVE_POLL_MS;
  const bool idle_due = !g_latest.pressed && g_status.auto_sleep_disabled &&
      now - g_last_read_ms >= TOUCH_IDLE_POLL_MS;
  if (!irq && !active_due && !idle_due) return;
  g_last_read_ms = now;
  uint8_t data[kReportBytes] = {};
  if (!read_registers(0x00, data, sizeof(data))) { failed_read(now, false); return; }
  const uint8_t fingers = data[2];
  if (fingers > 2) { failed_read(now, true); return; }
  ++g_status.frames;
  if (fingers == 0) {
    g_failures = 0;
    g_wait_all_up = false;
    release_touch(now);
    return;
  }
  if (g_wait_all_up) { g_failures = 0; return; }
  // Track the original contact until all fingers lift; no second-finger jump.
  // During an existing touch, find its ID in either record. A lift record can
  // precede the surviving contact even when FingerNum has already fallen to 1.
  const uint8_t records = g_latest.pressed ? 2 : fingers;
  for (uint8_t i = 0; i < records; ++i) {
    const uint8_t offset = 3 + i * 6;
    const uint8_t id = data[offset + 2] >> 4;
    const uint8_t event = data[offset] >> 6;
    if (i >= fingers) {
      bool empty = true;
      for (uint8_t j = 0; j < 6; ++j) empty = empty && data[offset + j] == 0;
      if (empty || id == 0x0F) continue;
    }
    if (id > 1 || event == 3) { failed_read(now, true); return; }
    if (g_latest.pressed && id != g_latest.contact_id) continue;
    if (event == 1) continue;  // Lift; retain last valid pressed coordinate.
    TouchSample sample = {};
    sample.raw_x = static_cast<uint16_t>(((data[offset] & 0x0F) << 8) | data[offset + 1]);
    sample.raw_y = static_cast<uint16_t>(((data[offset + 2] & 0x0F) << 8) | data[offset + 3]);
    if (sample.raw_x > g_status.observed_x_max) g_status.observed_x_max = sample.raw_x;
    if (sample.raw_y > g_status.observed_y_max) g_status.observed_y_max = sample.raw_y;
    if (!map_coordinates(sample.raw_x, sample.raw_y, sample.x, sample.y)) {
      // Keep the offending raw values visible for on-board range diagnosis.
      g_latest.raw_x = sample.raw_x;
      g_latest.raw_y = sample.raw_y;
      ++g_status.invalid_frames;
      g_failures = 0;
      // A mapping mismatch is not an I2C fault: keep diagnostics available.
      g_wait_all_up = true;
      release_touch(now);
      if (g_status.invalid_frames == 1 || now - g_last_range_log_ms >= 1000) {
        g_last_range_log_ms = now;
        Serial.printf("[TP] RAW (%u,%u) exceeds configured (%u,%u); check touch_config.h\n",
                      sample.raw_x, sample.raw_y, TOUCH_RAW_X_MAX, TOUCH_RAW_Y_MAX);
      }
      return;
    }
    sample.contact_id = id;
    sample.gesture = data[1];
    sample.time_ms = now;
    sample.pressed = true;
    g_failures = 0;
    publish(sample);
    return;
  }
  g_failures = 0;
  g_wait_all_up = true;
  release_touch(now);
}

const TouchStatus &touch_get_status() { return g_status; }
const TouchSample &touch_get_latest() { return g_latest; }
bool touch_has_pending_sample() { return g_count != 0; }
bool touch_pop_sample(TouchSample &sample) {
  if (g_count == 0) return false;
  sample = g_queue[g_head];
  g_head = (g_head + 1) % kQueueSize;
  --g_count;
  g_delivered = sample;
  return true;
}

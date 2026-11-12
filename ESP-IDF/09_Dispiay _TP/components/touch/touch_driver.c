/* CST816D port of Arduino/examples/10_Dispiay_TP using ESP-IDF 5.x APIs. */
#include "touch_driver.h"
#include "touch_config.h"
#include "display_config.h"

#include <inttypes.h>
#include <stddef.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TOUCH_QUEUE_SIZE 16
#define TOUCH_REPORT_BYTES 15
#define TOUCH_RESET_LOW_MS 11
#define TOUCH_BOOT_WAIT_MS 120

typedef enum { RECOVERY_IDLE, RECOVERY_RESET_LOW, RECOVERY_BOOT_WAIT } recovery_state_t;
static const char *TAG = "CST816D";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_device;
static bool s_owns_bus;
static bool s_initialized;
static touch_status_t s_status;
static touch_sample_t s_latest, s_delivered;
static touch_sample_t s_queue[TOUCH_QUEUE_SIZE];
static uint8_t s_head, s_count, s_failures;
static bool s_wait_all_up;
static uint32_t s_last_read_ms, s_recovery_ms, s_last_range_log_ms;
static recovery_state_t s_recovery;
static esp_err_t s_last_io_error;
static volatile bool s_irq_pending;
static portMUX_TYPE s_irq_mux = portMUX_INITIALIZER_UNLOCKED;

_Static_assert(TOUCH_RAW_X_MAX > 0 && TOUCH_RAW_Y_MAX > 0, "Invalid TP range");

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void touch_irq(void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&s_irq_mux);
    s_irq_pending = true;
    portEXIT_CRITICAL_ISR(&s_irq_mux);
}

static bool take_irq(void)
{
    portENTER_CRITICAL(&s_irq_mux);
    bool pending = s_irq_pending;
    s_irq_pending = false;
    portEXIT_CRITICAL(&s_irq_mux);
    return pending;
}

static bool read_registers(uint8_t reg, uint8_t *data, size_t length)
{
    /* Match the vendor/Arduino STOP between register write and data read. */
    s_last_io_error = i2c_master_transmit(s_device, &reg, 1, TOUCH_I2C_TIMEOUT_MS);
    if (s_last_io_error != ESP_OK) return false;
    s_last_io_error = i2c_master_receive(s_device, data, length, TOUCH_I2C_TIMEOUT_MS);
    return s_last_io_error == ESP_OK;
}

static bool write_register(uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = { reg, value };
    s_last_io_error = i2c_master_transmit(s_device, bytes, sizeof(bytes),
                                         TOUCH_I2C_TIMEOUT_MS);
    return s_last_io_error == ESP_OK;
}

static void enqueue(const touch_sample_t *sample)
{
    /* Keep the first press coordinate, coalesce subsequent move samples. */
    if (sample->pressed && s_count >= 2) {
        uint8_t tail = (s_head + s_count - 1) % TOUCH_QUEUE_SIZE;
        uint8_t before = (s_head + s_count - 2) % TOUCH_QUEUE_SIZE;
        if (s_queue[tail].pressed && s_queue[before].pressed) {
            s_queue[tail] = *sample;
            return;
        }
    }
    if (s_count == TOUCH_QUEUE_SIZE) {
        ++s_status.queue_overflows;
        s_head = 0;
        s_count = 0;
        if (s_delivered.pressed) {
            touch_sample_t released = s_delivered;
            released.pressed = false;
            released.time_ms = sample->time_ms;
            s_queue[s_count++] = released;
        }
    }
    s_queue[(s_head + s_count) % TOUCH_QUEUE_SIZE] = *sample;
    ++s_count;
}

static void publish(const touch_sample_t *sample)
{
    if (sample->pressed != s_latest.pressed ||
        (sample->pressed && (sample->x != s_latest.x || sample->y != s_latest.y))) {
        enqueue(sample);
    }
    s_latest = *sample;
}

static void release_touch(uint32_t now)
{
    touch_sample_t released = s_latest;
    released.pressed = false;
    released.time_ms = now;
    publish(&released);
}

static bool map_coordinates(uint16_t raw_x, uint16_t raw_y,
                            uint16_t *screen_x, uint16_t *screen_y)
{
    if (raw_x > TOUCH_RAW_X_MAX || raw_y > TOUCH_RAW_Y_MAX) return false;
    uint32_t x = raw_x, y = raw_y;
    uint32_t max_x = TOUCH_RAW_X_MAX, max_y = TOUCH_RAW_Y_MAX;
    if (TOUCH_SWAP_XY) {
        uint32_t t = x; x = y; y = t;
        uint32_t m = max_x; max_x = max_y; max_y = m;
    }
    if (TOUCH_REVERSE_X) x = max_x - x;
    if (TOUCH_REVERSE_Y) y = max_y - y;
    *screen_x = (uint16_t)(x * (DISPLAY_WIDTH - 1) / max_x);
    *screen_y = (uint16_t)(y * (DISPLAY_HEIGHT - 1) / max_y);
    return true;
}

static bool identify(void)
{
    uint8_t info[4] = {0};
    if (!read_registers(0xA7, info, sizeof(info))) return false;
    bool all_zero = !(info[0] | info[1] | info[2] | info[3]);
    bool all_ff = info[0] == 0xFF && info[1] == 0xFF &&
                  info[2] == 0xFF && info[3] == 0xFF;
    if (all_zero || all_ff) return false;
    s_status.chip_id = info[0];
    s_status.project_id = info[1];
    s_status.firmware_version = info[2];
    s_status.factory_id = info[3];
    s_status.auto_sleep_disabled = false;
    if (TOUCH_TRY_DISABLE_AUTO_SLEEP) {
        uint8_t value = 0;
        s_status.auto_sleep_disabled = write_register(0xFE, 1) &&
            read_registers(0xFE, &value, 1) && value == 1;
    }
    s_status.online = true;
    s_failures = 0;
    s_wait_all_up = false;
    ESP_LOGI(TAG, "addr=0x%02X chip=0x%02X project=0x%02X fw=0x%02X factory=0x%02X awake=%u",
             TOUCH_I2C_ADDRESS, info[0], info[1], info[2], info[3],
             s_status.auto_sleep_disabled ? 1U : 0U);
    return true;
}

static void failed_read(uint32_t now, bool invalid)
{
    if (invalid) ++s_status.invalid_frames;
    else ++s_status.io_errors;
    if (++s_failures < TOUCH_MAX_FAILURES) return;
    release_touch(now);
    s_status.online = false;
    s_recovery_ms = now;
    ESP_LOGW(TAG, "Repeated %s; pointer released, retry in 2s (%s)",
             invalid ? "invalid reports" : "I2C errors", esp_err_to_name(s_last_io_error));
}

static void recover(uint32_t now)
{
    if (s_recovery == RECOVERY_IDLE) {
        if (now - s_recovery_ms < TOUCH_RETRY_MS) return;
        gpio_set_level(TOUCH_RST_GPIO, 0);
        s_recovery = RECOVERY_RESET_LOW;
        s_recovery_ms = now;
        return;
    }
    if (s_recovery == RECOVERY_RESET_LOW) {
        if (now - s_recovery_ms < TOUCH_RESET_LOW_MS) return;
        gpio_set_level(TOUCH_RST_GPIO, 1);
        s_recovery = RECOVERY_BOOT_WAIT;
        s_recovery_ms = now;
        return;
    }
    if (now - s_recovery_ms < TOUCH_BOOT_WAIT_MS) return;
    s_recovery = RECOVERY_IDLE;
    s_recovery_ms = now;
    if (identify()) {
        s_last_read_ms = now - TOUCH_IDLE_POLL_MS;
        ESP_LOGI(TAG, "Touch recovered");
    }
}

esp_err_t touch_init(void)
{
    if (s_initialized) return ESP_OK;
    gpio_config_t reset = {
        .pin_bit_mask = 1ULL << TOUCH_RST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&reset);
    if (err != ESP_OK) return err;
    gpio_set_level(TOUCH_RST_GPIO, 1);
    gpio_config_t irq = {
        .pin_bit_mask = 1ULL << TOUCH_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    err = gpio_config(&irq);
    if (err != ESP_OK) return err;

    /* Bus ownership is explicit: querying an uninitialized bus logs an
     * ESP-IDF error even when the caller intends to create it next. */
#if TOUCH_USE_EXISTING_I2C_BUS
    err = i2c_master_get_bus_handle(TOUCH_I2C_PORT, &s_bus);
    if (err != ESP_OK) return err;
#else
    {
        const i2c_master_bus_config_t bus = {
            .i2c_port = TOUCH_I2C_PORT,
            .sda_io_num = TOUCH_SDA_GPIO,
            .scl_io_num = TOUCH_SCL_GPIO,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        err = i2c_new_master_bus(&bus, &s_bus);
        if (err != ESP_OK) return err;
        s_owns_bus = true;
    }
#endif
    const i2c_device_config_t device = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_I2C_ADDRESS,
        .scl_speed_hz = TOUCH_I2C_HZ,
    };
    err = i2c_master_bus_add_device(s_bus, &device, &s_device);
    if (err != ESP_OK) goto fail;
    err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) goto fail;
    err = gpio_isr_handler_add(TOUCH_INT_GPIO, touch_irq, NULL);
    if (err != ESP_OK) goto fail;
    s_initialized = true;
    gpio_set_level(TOUCH_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(TOUCH_RESET_LOW_MS) + 1);
    gpio_set_level(TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(TOUCH_BOOT_WAIT_MS) + 1);
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (identify()) return ESP_OK;
        vTaskDelay(pdMS_TO_TICKS(10) + 1);
    }
    s_recovery_ms = now_ms();
    ESP_LOGW(TAG, "TP not detected; display continues, automatic retry enabled");
    return ESP_OK;

fail:
    if (s_device != NULL) {
        i2c_master_bus_rm_device(s_device);
        s_device = NULL;
    }
    if (s_owns_bus) {
        i2c_del_master_bus(s_bus);
        s_owns_bus = false;
    }
    s_bus = NULL;
    return err;
}

void touch_poll(void)
{
    if (!s_initialized) return;
    uint32_t now = now_ms();
    if (!s_status.online) { recover(now); return; }
    bool irq = take_irq();
    bool active_due = (s_latest.pressed || s_wait_all_up || s_failures != 0) &&
                      now - s_last_read_ms >= TOUCH_ACTIVE_POLL_MS;
    bool idle_due = !s_latest.pressed && s_status.auto_sleep_disabled &&
                    now - s_last_read_ms >= TOUCH_IDLE_POLL_MS;
    if (!irq && !active_due && !idle_due) return;
    s_last_read_ms = now;
    uint8_t data[TOUCH_REPORT_BYTES] = {0};
    if (!read_registers(0x00, data, sizeof(data))) { failed_read(now, false); return; }
    uint8_t fingers = data[2];
    if (fingers > 2) { failed_read(now, true); return; }
    ++s_status.frames;
    if (fingers == 0) {
        s_failures = 0;
        s_wait_all_up = false;
        release_touch(now);
        return;
    }
    if (s_wait_all_up) { s_failures = 0; return; }
    uint8_t records = s_latest.pressed ? 2 : fingers;
    for (uint8_t i = 0; i < records; ++i) {
        uint8_t offset = 3 + i * 6;
        uint8_t id = data[offset + 2] >> 4;
        uint8_t event = data[offset] >> 6;
        if (i >= fingers) {
            bool empty = true;
            for (uint8_t j = 0; j < 6; ++j) empty = empty && data[offset + j] == 0;
            if (empty || id == 0x0F) continue;
        }
        if (id > 1 || event == 3) { failed_read(now, true); return; }
        if (s_latest.pressed && id != s_latest.contact_id) continue;
        if (event == 1) continue;
        touch_sample_t sample = {0};
        sample.raw_x = (uint16_t)(((data[offset] & 0x0F) << 8) | data[offset + 1]);
        sample.raw_y = (uint16_t)(((data[offset + 2] & 0x0F) << 8) | data[offset + 3]);
        if (sample.raw_x > s_status.observed_x_max) s_status.observed_x_max = sample.raw_x;
        if (sample.raw_y > s_status.observed_y_max) s_status.observed_y_max = sample.raw_y;
        if (!map_coordinates(sample.raw_x, sample.raw_y, &sample.x, &sample.y)) {
            s_latest.raw_x = sample.raw_x;
            s_latest.raw_y = sample.raw_y;
            ++s_status.invalid_frames;
            s_failures = 0;
            s_wait_all_up = true;
            release_touch(now);
            if (s_status.invalid_frames == 1 || now - s_last_range_log_ms >= 1000) {
                s_last_range_log_ms = now;
                ESP_LOGW(TAG, "RAW (%u,%u) exceeds configured (%u,%u)",
                         sample.raw_x, sample.raw_y, TOUCH_RAW_X_MAX, TOUCH_RAW_Y_MAX);
            }
            return;
        }
        sample.contact_id = id;
        sample.gesture = data[1];
        sample.time_ms = now;
        sample.pressed = true;
        s_failures = 0;
        publish(&sample);
        return;
    }
    s_failures = 0;
    s_wait_all_up = true;
    release_touch(now);
}

const touch_status_t *touch_get_status(void) { return &s_status; }
const touch_sample_t *touch_get_latest(void) { return &s_latest; }
bool touch_has_pending_sample(void) { return s_count != 0; }
bool touch_pop_sample(touch_sample_t *sample)
{
    if (sample == NULL || s_count == 0) return false;
    *sample = s_queue[s_head];
    s_head = (s_head + 1) % TOUCH_QUEUE_SIZE;
    --s_count;
    s_delivered = *sample;
    return true;
}

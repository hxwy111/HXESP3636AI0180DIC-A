#include "display_bus.h"
#include "display_config.h"

#include <stdbool.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"

static spi_device_handle_t s_display_spi;
/* A queued transaction and its pixel buffer stay alive until explicitly joined. */
static spi_transaction_ext_t s_pixel_transaction;
static bool s_pixels_pending;
/* 标记 SPI2 总线是否已经成功申请，释放时据此判断。 */
static bool s_spi_bus_ready;
/* 短参数使用 DMA 对齐的临时缓冲区，避免未对齐内存直接传输。 */
static uint8_t s_parameter_dma[64] __attribute__((aligned(4)));

/*
 * 发送一笔 NV3003A QSPI 事务。
 * 命令阶段和地址阶段保持单线，只有像素数据阶段使用四线 QSPI。
 */
static esp_err_t transmit_qspi(uint8_t opcode, uint32_t address,
                               const void *data, size_t length, bool quad_data)
{
    if (s_display_spi == NULL || (length != 0 && data == NULL)) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = display_bus_wait_pixels(true);
    if (err != ESP_OK) {
        return err;
    }
    err = spi_device_acquire_bus(s_display_spi, portMAX_DELAY);
    if (err != ESP_OK) {
        return err;
    }
    if (length != 0 && length <= sizeof(s_parameter_dma)) {
        memcpy(s_parameter_dma, data, length);
    }

    spi_transaction_ext_t transaction = {0};
    transaction.command_bits = 8;
    transaction.address_bits = 24;
    transaction.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
    transaction.base.cmd = opcode;
    transaction.base.addr = address & 0x00ffffffu;
    transaction.base.length = length * 8;
    if (quad_data && length != 0) {
        /* 命令和地址仍为单线，只有数据阶段切换为四线 QSPI。 */
        transaction.base.flags |= SPI_TRANS_MODE_QIO;
    }

    if (length != 0 && length <= sizeof(transaction.base.tx_data)) {
        transaction.base.flags |= SPI_TRANS_USE_TXDATA;
        memcpy(transaction.base.tx_data, s_parameter_dma, length);
    } else if (length != 0 && length <= sizeof(s_parameter_dma)) {
        transaction.base.tx_buffer = s_parameter_dma;
    } else if (length != 0) {
        transaction.base.tx_buffer = data;
    }


    gpio_set_level(DISPLAY_CS_GPIO, 0);
    err = spi_device_polling_transmit(s_display_spi, &transaction.base);
    gpio_set_level(DISPLAY_CS_GPIO, 1);
    spi_device_release_bus(s_display_spi);
    return err;
}

esp_err_t display_bus_init(void)
{
    if (s_display_spi != NULL) {
        return ESP_OK;
    }

    /* CS、RS/DC、RESET 都由软件控制为 GPIO 输出，QSPI 模式下 RS/DC 固定为低。 */
    const gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << DISPLAY_CS_GPIO) |
                        (1ULL << DISPLAY_DC_GPIO) |
                        (1ULL << DISPLAY_RESET_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&output_config);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(DISPLAY_CS_GPIO, 1);
    gpio_set_level(DISPLAY_DC_GPIO, 0); /* QSPI 模式下 DC/RS 不参与数据区分。 */
    gpio_set_level(DISPLAY_RESET_GPIO, 1);

    /* 配置 SCLK 和四根数据线 D0~D3，启用 SPI 主机和 QUAD 总线能力。 */
    const spi_bus_config_t bus_config = {
        .data0_io_num = DISPLAY_QSPI_D0_GPIO,
        .data1_io_num = DISPLAY_QSPI_D1_GPIO,
        .sclk_io_num = DISPLAY_SCLK_GPIO,
        .data2_io_num = DISPLAY_QSPI_D2_GPIO,
        .data3_io_num = DISPLAY_QSPI_D3_GPIO,
        .data4_io_num = -1,
        .data5_io_num = -1,
        .data6_io_num = -1,
        .data7_io_num = -1,
        .max_transfer_sz = DISPLAY_TRANSFER_MAX_BYTES,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD,
    };
    err = spi_bus_initialize(DISPLAY_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }
    s_spi_bus_ready = true;

    const spi_device_interface_config_t device_config = {
        .clock_speed_hz = DISPLAY_SPI_CLOCK_HZ,
        .mode = 0,
        /* CS 由软件控制，使命令、地址和数据处于同一片选窗口。 */
        .spics_io_num = -1,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };
    err = spi_bus_add_device(DISPLAY_SPI_HOST, &device_config, &s_display_spi);
    if (err != ESP_OK) {
        display_bus_deinit();
    }
    return err;
}

esp_err_t display_bus_write_command(uint8_t command, const void *data, size_t length)
{
    if (length != 0 && data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    /* NV3003A 寄存器写格式：02h + {00h, 寄存器地址, 00h} + 参数。 */
    return transmit_qspi(0x02, (uint32_t)command << 8, data, length, false);
}

/*
 * 在同一个 CS 有效窗口内发送完整像素流，可重复发送同一块数据。
 * 仅首个分包带 32h + 002C00h，后续分包省略命令和地址，继续四线数据阶段。
 * 同步传输结束后才释放总线，因此调用期间缓冲区必须保持有效且不可修改。
 */
static esp_err_t write_pixel_stream(const void *data, size_t length, size_t repeat_count)
{
    /* RGB565 必须由完整的两字节像素组成，也避免奇数长度导致分包无法前进。 */
    if (data == NULL || length == 0 || (length & 1u) != 0 || repeat_count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_display_spi == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = display_bus_wait_pixels(true);
    if (err != ESP_OK) {
        return err;
    }
    err = spi_device_acquire_bus(s_display_spi, portMAX_DELAY);
    if (err != ESP_OK) {
        return err;
    }

    bool first_chunk = true;
    gpio_set_level(DISPLAY_CS_GPIO, 0);
    for (size_t repeat = 0; repeat < repeat_count && err == ESP_OK; ++repeat) {
        const uint8_t *bytes = data;
        size_t remaining = length;
        while (remaining != 0) {
            const size_t chunk = remaining > DISPLAY_TRANSFER_MAX_BYTES
                                     ? DISPLAY_TRANSFER_MAX_BYTES : remaining;
            spi_transaction_ext_t transaction = {0};
            transaction.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR |
                                     SPI_TRANS_MODE_QIO;
            if (first_chunk) {
                transaction.command_bits = 8;
                transaction.address_bits = 24;
                transaction.base.cmd = 0x32;
                transaction.base.addr = 0x002c00;
            }
            /* 后续事务的 command_bits/address_bits 为零，不重复发送 2Ch。 */
            transaction.base.length = chunk * 8;
            if (chunk <= sizeof(transaction.base.tx_data)) {
                transaction.base.flags |= SPI_TRANS_USE_TXDATA;
                memcpy(transaction.base.tx_data, bytes, chunk);
            } else if (chunk <= sizeof(s_parameter_dma)) {
                memcpy(s_parameter_dma, bytes, chunk);
                transaction.base.tx_buffer = s_parameter_dma;
            } else {
                transaction.base.tx_buffer = bytes;
            }

            err = spi_device_polling_transmit(s_display_spi, &transaction.base);
            if (err != ESP_OK) {
                break;
            }
            first_chunk = false;
            bytes += chunk;
            remaining -= chunk;
        }
    }
    /* 即使中途传输失败，也必须恢复片选并释放总线。 */
    gpio_set_level(DISPLAY_CS_GPIO, 1);
    spi_device_release_bus(s_display_spi);
    return err;
}

esp_err_t display_bus_write_pixels(const void *data, size_t length)
{
    return write_pixel_stream(data, length, 1);
}

esp_err_t display_bus_write_pixels_repeated(const void *data, size_t length,
                                           size_t repeat_count)
{
    return write_pixel_stream(data, length, repeat_count);
}

esp_err_t display_bus_wait_pixels(bool wait)
{
    if (!s_pixels_pending) {
        return ESP_OK;
    }
    spi_transaction_t *completed = NULL;
    esp_err_t err = spi_device_get_trans_result(s_display_spi, &completed,
                                                wait ? portMAX_DELAY : 0);
    if (err != ESP_OK) {
        /* Keep CS, the transaction and the buffer owned until DMA completes. */
        return err;
    }
    s_pixels_pending = false;
    gpio_set_level(DISPLAY_CS_GPIO, 1);
    spi_device_release_bus(s_display_spi);
    return completed == &s_pixel_transaction.base ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t display_bus_write_pixels_async(const void *data, size_t length)
{
    if (data == NULL || length == 0 || (length & 1u) != 0 ||
        length > DISPLAY_TRANSFER_MAX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_display_spi == NULL || s_pixels_pending) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = spi_device_acquire_bus(s_display_spi, portMAX_DELAY);
    if (err != ESP_OK) {
        return err;
    }
    memset(&s_pixel_transaction, 0, sizeof(s_pixel_transaction));
    s_pixel_transaction.command_bits = 8;
    s_pixel_transaction.address_bits = 24;
    s_pixel_transaction.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR |
                                     SPI_TRANS_MODE_QIO;
    s_pixel_transaction.base.cmd = 0x32;
    s_pixel_transaction.base.addr = 0x002c00;
    s_pixel_transaction.base.length = length * 8;
    if (length <= sizeof(s_pixel_transaction.base.tx_data)) {
        s_pixel_transaction.base.flags |= SPI_TRANS_USE_TXDATA;
        memcpy(s_pixel_transaction.base.tx_data, data, length);
    } else {
        s_pixel_transaction.base.tx_buffer = data;
    }
    gpio_set_level(DISPLAY_CS_GPIO, 0);
    err = spi_device_queue_trans(s_display_spi, &s_pixel_transaction.base, portMAX_DELAY);
    if (err != ESP_OK) {
        gpio_set_level(DISPLAY_CS_GPIO, 1);
        spi_device_release_bus(s_display_spi);
        return err;
    }
    s_pixels_pending = true;
    return ESP_OK;
}

void display_bus_deinit(void)
{
    /* Never remove a device or free a buffer while DMA is using it. */
    ESP_ERROR_CHECK(display_bus_wait_pixels(true));
    if (s_display_spi != NULL) {
        spi_bus_remove_device(s_display_spi);
        s_display_spi = NULL;
    }
    if (s_spi_bus_ready) {
        spi_bus_free(DISPLAY_SPI_HOST);
        s_spi_bus_ready = false;
    }
}

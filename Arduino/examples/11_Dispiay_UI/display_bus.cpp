#include "display_bus.h"
#include "display_config.h"

#include "Arduino.h"
#include "driver/spi_master.h"
#include <string.h>

namespace {

// ===== 总线内部辅助函数 =====
// 下面的句柄分别对应普通写事务和三线读事务。两个设备共享同一组
// SCLK、D0~D3 和片选线，但读事务需要额外启用三线输入配置。

spi_device_handle_t g_device = nullptr;
// QSPI 寄存器读取：屏幕采用三线读回，数据从 DB<0> 返回，并使用较低时钟频率。

bool g_bus_initialized = false;

// 命令和窗口参数通常很短；启用 DMA 的 SPI 总线要求缓冲区位于 DMA 可访问内存。

uint8_t g_param_dma[64] __attribute__((aligned(4)));

// QSPI 模式下读写设备共用屏幕 CSX，因此由 GPIO 手动控制片选。
// 执行一笔 SPI 扩展事务。CS 由 GPIO 手动拉低和拉高，确保命令、地址及
// 数据阶段保持在同一次片选有效窗口内。polling_transmit 返回后事务才真正完成。

esp_err_t transmit_on(spi_device_handle_t device, spi_transaction_ext_t &transaction) {
  if (device == nullptr) return ESP_ERR_INVALID_STATE;
  digitalWrite(DISPLAY_CS_GPIO, LOW);
  esp_err_t err = spi_device_polling_transmit(device, &transaction.base);
  digitalWrite(DISPLAY_CS_GPIO, HIGH);
  return err;
}

// 普通写事务使用主写设备发送。

esp_err_t transmit(spi_transaction_ext_t &transaction) {
  return transmit_on(g_device, transaction);
}

// 设置 RS/DCX 电平。QSPI 协议把命令和地址编码在总线事务中，
// 因此 QSPI 模式不需要切换 RS/DCX；普通 SPI 模式才使用该引脚区分命令和数据。

void set_dc(bool data) {
  if (!DISPLAY_USE_QSPI) {
    digitalWrite(DISPLAY_DC_GPIO, data ? HIGH : LOW);
  }
}

// 填充 NV3003A 使用的扩展事务：8 位操作码、24 位地址和可选数据阶段。
// 不设置多线地址标志时，操作码和地址只从 D0 输出；仅数据阶段可以切换为四线。

void configure_ext_transaction(spi_transaction_ext_t &transaction,
                               uint8_t opcode, uint32_t address,
                               const void *data, size_t length,
                               bool quad_data) {
  transaction = {};
  transaction.command_bits = 8;
  transaction.address_bits = 24;
  transaction.base.flags = SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
  // 根据 NV3003A 手册 6.2.3 和 6.4.7：操作码及 24 位地址始终只从 DB<0> 输出，
  // 只有 32h 写入像素数据时才使用 DB<3:0> 四条数据线。

  if (quad_data) transaction.base.flags |= SPI_TRANS_MODE_QIO;
  transaction.base.cmd = opcode;
  transaction.base.addr = address & 0x00FFFFFFu;
  transaction.base.length = length * 8;

  if (length == 0) return;
  if (length <= sizeof(transaction.base.tx_data)) {
    transaction.base.flags |= SPI_TRANS_USE_TXDATA;
    memcpy(transaction.base.tx_data, data, length);
  } else {
    transaction.base.tx_buffer = data;
  }
}

// 普通四线 SPI 的命令发送流程：先发送命令，再在保持 CS 有效的情况下发送参数。
// 参数先复制到 DMA 缓冲区，避免调用者传入的内存不满足 DMA 对齐或可访问要求。

esp_err_t write_spi_command(uint8_t command, const void *data, size_t length) {
  if (length > sizeof(g_param_dma)) return ESP_ERR_INVALID_SIZE;
  if (length != 0) memcpy(g_param_dma, data, length);


  esp_err_t acquire_err = spi_device_acquire_bus(g_device, portMAX_DELAY);
  if (acquire_err != ESP_OK) return acquire_err;
  digitalWrite(DISPLAY_CS_GPIO, LOW);

  spi_transaction_t command_transaction = {};
  command_transaction.flags = SPI_TRANS_USE_TXDATA;
  if (length != 0) command_transaction.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
  command_transaction.length = 8;
  command_transaction.tx_data[0] = command;
  set_dc(false);
  esp_err_t err = spi_device_polling_transmit(g_device, &command_transaction);

  if (err == ESP_OK && length != 0) {
    spi_transaction_t parameter_transaction = {};
    parameter_transaction.length = length * 8;
    set_dc(true);
    if (length <= sizeof(parameter_transaction.tx_data)) {
      parameter_transaction.flags = SPI_TRANS_USE_TXDATA;
      memcpy(parameter_transaction.tx_data, g_param_dma, length);
    } else {
      parameter_transaction.tx_buffer = g_param_dma;
    }
    err = spi_device_polling_transmit(g_device, &parameter_transaction);
  }

  set_dc(false);
  digitalWrite(DISPLAY_CS_GPIO, HIGH);
  spi_device_release_bus(g_device);
  return err;
}

// QSPI 寄存器命令发送。NV3003A 的寄存器命令统一使用 02h，
// 寄存器编号放在 24 位地址的中间字节，参数仍从 D0 单线发送。

esp_err_t write_qspi_command(uint8_t command, const void *data, size_t length) {
  // NV3003A 寄存器写入格式（手册图 6-2-3-1-1）：
  //   02h + {00h, 命令字节, 00h} + 参数字节，全部从 DB<0> 输出。
  // 12h、32h、38h 是 RAM 专用操作码（地址必须为 2Ch 或 3Ch），因此
  // 不能用于初始化命令表以及 2Ah、2Bh 窗口设置命令。
  spi_transaction_ext_t transaction;
  configure_ext_transaction(transaction, 0x02, static_cast<uint32_t>(command) << 8,
                            data, length, false);
  return transmit(transaction);
}

// 使用 NV3003A 规定的 32h 四线 QSPI 方式发送像素数据。

esp_err_t write_qspi_color(const void *data, size_t length) {
  // NV3003A 像素写入格式：
  //   单线诊断：02h + 002C00，像素数据从 D0 输出；
  //   正常 QSPI：32h + 002C00（命令和地址从 D0 输出）+ 像素数据从 D0~D3 输出。
  spi_transaction_ext_t transaction;
  configure_ext_transaction(transaction, 0x32, 0x002C00,
                            data, length, true);
  return transmit(transaction);
}

}  // 匿名命名空间

// 初始化显示总线：先配置复位、背光、RS/DCX 和 CS 的安全默认电平，
// 再创建 SPI 总线及写设备、读设备。初始化失败时释放已申请的资源。

esp_err_t display_bus_init() {
  if (g_device != nullptr) return ESP_OK;

  pinMode(DISPLAY_RST_GPIO, OUTPUT);
  digitalWrite(DISPLAY_RST_GPIO, HIGH);
  pinMode(DISPLAY_BL_GPIO, OUTPUT);
  analogWrite(DISPLAY_BL_GPIO, 0);
  // QSPI 模式不使用 RS/DCX，但该引脚仍连接到屏幕，因此将其
  // 固定在低电平，避免屏幕输入悬空。

  pinMode(DISPLAY_DC_GPIO, OUTPUT);
  digitalWrite(DISPLAY_DC_GPIO, LOW);
  pinMode(DISPLAY_CS_GPIO, OUTPUT);
  digitalWrite(DISPLAY_CS_GPIO, HIGH);

  spi_bus_config_t bus_config = {};
  bus_config.data0_io_num = DISPLAY_DATA0_GPIO;
  bus_config.data1_io_num = DISPLAY_USE_QSPI ? DISPLAY_DATA1_GPIO : -1;
  bus_config.data2_io_num = DISPLAY_USE_QSPI ? DISPLAY_DATA2_GPIO : -1;
  bus_config.data3_io_num = DISPLAY_USE_QSPI ? DISPLAY_DATA3_GPIO : -1;
  bus_config.sclk_io_num = DISPLAY_SCLK_GPIO;
  bus_config.max_transfer_sz = DISPLAY_WIDTH * 2 + 8;

  esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) return err;
  g_bus_initialized = true;

  spi_device_interface_config_t device_config = {};
  device_config.clock_speed_hz = DISPLAY_SPI_HZ;
  device_config.mode = 0;
  device_config.spics_io_num = -1;  // 片选由 transmit_on() 通过 GPIO 手动控制。

  device_config.queue_size = 4;
  device_config.flags = SPI_DEVICE_HALFDUPLEX;
  err = spi_bus_add_device(SPI2_HOST, &device_config, &g_device);
  if (err != ESP_OK) {
    spi_bus_free(SPI2_HOST);
    g_bus_initialized = false;
    g_device = nullptr;
  }
  return err;
}

// 对上层提供统一的命令接口，根据 DISPLAY_USE_QSPI 自动选择 SPI 或 QSPI 实现。

esp_err_t display_bus_write_command(uint8_t command, const void *data, size_t length) {
  if (g_device == nullptr) return ESP_ERR_INVALID_STATE;
  if (length != 0 && data == nullptr) return ESP_ERR_INVALID_ARG;
  return DISPLAY_USE_QSPI ? write_qspi_command(command, data, length)
                          : write_spi_command(command, data, length);
}

// 对上层提供统一的像素接口。QSPI 直接发送 RAM 写入事务；
// 普通 SPI 则把 2Ch 命令和像素数据放在同一个 CS 有效窗口内。

esp_err_t display_bus_write_color(const void *data, size_t length) {
  if (g_device == nullptr) return ESP_ERR_INVALID_STATE;
  if (data == nullptr || length == 0) return ESP_ERR_INVALID_ARG;

  if (DISPLAY_USE_QSPI) {
    return write_qspi_color(data, length);
  }

  // 普通四线 SPI 模式下，RS/DC 低电平时发送命令，
  // RS/DC 高电平时发送像素数据，并在两个阶段之间保持 CS 有效。

  esp_err_t acquire_err = spi_device_acquire_bus(g_device, portMAX_DELAY);
  if (acquire_err != ESP_OK) return acquire_err;
  digitalWrite(DISPLAY_CS_GPIO, LOW);
  spi_transaction_t command_transaction = {};
  command_transaction.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_CS_KEEP_ACTIVE;
  command_transaction.length = 8;
  command_transaction.tx_data[0] = 0x2C;
  set_dc(false);
  esp_err_t err = spi_device_polling_transmit(g_device, &command_transaction);
  if (err == ESP_OK) {
    spi_transaction_t color_transaction = {};
    color_transaction.length = length * 8;
    color_transaction.tx_buffer = data;
    set_dc(true);
    err = spi_device_polling_transmit(g_device, &color_transaction);
  }
  set_dc(false);
  digitalWrite(DISPLAY_CS_GPIO, HIGH);
  spi_device_release_bus(g_device);
  return err;
}

// 释放读设备、写设备和 SPI 总线。重新初始化前必须调用此函数。

void display_bus_deinit() {
  if (g_device != nullptr) {
    spi_bus_remove_device(g_device);
    g_device = nullptr;
  }
  if (g_bus_initialized) {
    spi_bus_free(SPI2_HOST);
    g_bus_initialized = false;
  }
}



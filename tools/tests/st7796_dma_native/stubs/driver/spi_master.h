#pragma once
#include <cstddef>
#include <cstdint>
#include <cassert>
using esp_err_t = int;
using spi_device_handle_t = void*;
constexpr int SPI2_HOST = 1, SPI_DMA_CH_AUTO = 3;
constexpr int ESP_OK = 0, ESP_ERR_INVALID_STATE = 1, ESP_ERR_NO_MEM = 2;
constexpr int portMAX_DELAY = -1;
constexpr unsigned SPI_DEVICE_NO_DUMMY = 1, SPI_TRANS_USE_TXDATA = 2;
constexpr unsigned SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL = 4;
#define ESP_ERROR_CHECK(call) do { assert((call) == ESP_OK); } while (0)
struct spi_transaction_t {
  unsigned flags;
  size_t length;
  const void* tx_buffer;
  uint8_t tx_data[4];
  void* user;
};
struct spi_bus_config_t {
  int sclk_io_num, mosi_io_num, miso_io_num, quadwp_io_num, quadhd_io_num;
  int data4_io_num, data5_io_num, data6_io_num, data7_io_num;
  size_t max_transfer_sz;
};
struct spi_device_interface_config_t {
  int clock_speed_hz, mode, spics_io_num, queue_size;
  unsigned flags;
  void (*post_cb)(spi_transaction_t*);
};
esp_err_t spi_bus_initialize(int, const spi_bus_config_t*, int);
esp_err_t spi_bus_add_device(int, const spi_device_interface_config_t*, spi_device_handle_t*);
esp_err_t spi_bus_free(int);
esp_err_t spi_device_queue_trans(spi_device_handle_t, spi_transaction_t*, int);
esp_err_t spi_device_get_trans_result(spi_device_handle_t, spi_transaction_t**, int);
esp_err_t spi_device_polling_transmit(spi_device_handle_t, spi_transaction_t*);

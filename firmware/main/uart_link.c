#include "uart_link.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char* TAG = "uart_link";

/*
 * UART to the Flipper expansion header. These ESP32-side pins must route to
 * Flipper header pin 13 (TX) / pin 14 (RX). Adjust to match your dev board's
 * wiring; the canonical Flipper-side pins are documented in docs/protocol.md.
 */
#ifndef SONAR_UART_PORT
#define SONAR_UART_PORT UART_NUM_1
#endif
#ifndef SONAR_UART_TX_PIN
#define SONAR_UART_TX_PIN 17
#endif
#ifndef SONAR_UART_RX_PIN
#define SONAR_UART_RX_PIN 18
#endif
#define SONAR_UART_BAUD 115200
#define RX_BUF_SIZE 512

static SemaphoreHandle_t s_tx_mutex;
static provision_cb_t s_on_provision;

void uart_link_send(const uint8_t* buf, size_t len) {
    if(!buf || len == 0) return;
    if(xSemaphoreTake(s_tx_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        uart_write_bytes(SONAR_UART_PORT, (const char*)buf, len);
        xSemaphoreGive(s_tx_mutex);
    } else {
        ESP_LOGW(TAG, "tx lock timeout; dropping frame");
    }
}

void uart_link_send_link(uint8_t link_state) {
    uint8_t buf[SONAR_MAX_FRAME];
    size_t n = sonar_build_link(link_state, buf, sizeof(buf));
    uart_link_send(buf, n);
}

void uart_link_send_heartbeat(void) {
    uint8_t buf[SONAR_MAX_FRAME];
    size_t n = sonar_build_heartbeat(buf, sizeof(buf));
    uart_link_send(buf, n);
}

static void rx_task(void* arg) {
    (void)arg;
    SonarParser parser;
    sonar_parser_reset(&parser);
    uint8_t chunk[128];
    SonarFrame frame;
    for(;;) {
        int n = uart_read_bytes(SONAR_UART_PORT, chunk, sizeof(chunk), pdMS_TO_TICKS(100));
        for(int i = 0; i < n; i++) {
            if(sonar_parser_push(&parser, chunk[i], &frame)) {
                if(frame.type == SONAR_T_PROVISION && s_on_provision) {
                    SonarProvision prov;
                    if(sonar_decode_provision(frame.payload, frame.payload_len, &prov)) {
                        ESP_LOGI(TAG, "provision: ssid=%s id=%s", prov.ssid, prov.sonar_id);
                        s_on_provision(&prov);
                    }
                }
                /* All other inbound types are ignored: link is one-directional. */
            }
        }
    }
}

void uart_link_init(provision_cb_t on_provision) {
    s_on_provision = on_provision;
    s_tx_mutex = xSemaphoreCreateMutex();

    uart_config_t cfg = {
        .baud_rate = SONAR_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(SONAR_UART_PORT, RX_BUF_SIZE, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(SONAR_UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(
        SONAR_UART_PORT, SONAR_UART_TX_PIN, SONAR_UART_RX_PIN, UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE));

    xTaskCreate(rx_task, "sonar_uart_rx", 4096, NULL, 10, NULL);
    ESP_LOGI(TAG, "uart up: tx=%d rx=%d @%d", SONAR_UART_TX_PIN, SONAR_UART_RX_PIN, SONAR_UART_BAUD);
}

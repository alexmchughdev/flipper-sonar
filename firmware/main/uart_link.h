#pragma once
#include <stdint.h>
#include <stddef.h>
#include "claudeogotchi_uart.h"

/* UART link to the Flipper. TX is mutex-guarded; an RX task parses inbound
 * provisioning frames (type 0x10) and invokes the registered callback. */

typedef void (*provision_cb_t)(const ClaudeogotchiProvision* prov);

void uart_link_init(provision_cb_t on_provision);

/* Thread-safe frame send. `buf`/`len` is a complete encoded frame. */
void uart_link_send(const uint8_t* buf, size_t len);

/* Convenience senders (build + send under the same lock). */
void uart_link_send_link(uint8_t link_state);
void uart_link_send_heartbeat(void);

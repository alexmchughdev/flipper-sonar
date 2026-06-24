#pragma once

/* Connects out to <relay_url>/egress/<claudeogotchi_id> over WSS (TLS terminates here),
 * decodes relay JSON, re-frames it as compact UART to the Flipper. Reconnects
 * with backoff and surfaces link state to the FAP via uart_link. */
void ws_client_start(const char* relay_url, const char* claudeogotchi_id);
void ws_client_stop(void);

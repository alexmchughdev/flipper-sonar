#pragma once
#include <stdbool.h>

void wifi_init(void);

/* (Re)connect station to the given network. Non-blocking; connection state is
 * reported via wifi_is_connected(). */
void wifi_connect(const char* ssid, const char* pass);

bool wifi_is_connected(void);

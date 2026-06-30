/*
 * Shim so the FAP gets the ONE canonical UART contract without a copy that can
 * drift. ufbt compiles with this directory on the include path, and this
 * relative include resolves against this file's own location.
 */
#include "../proto/claudeogotchi_uart.h"

#pragma once
#include <stdbool.h>


//Set default state of awning to 0 (retracted)
#define DEFAULT_STATE 0

void app_driver_init(void);
void app_driver_set_state(bool state);


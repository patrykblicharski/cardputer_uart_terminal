#pragma once

struct MenuItem {
    const char* name;
    const char* desc;
    bool        available;
};

static constexpr MenuItem kMenuItems[] = {
    { "UART Terminal", "Serial/UART diagnostics", true  },
    { "I2C Scanner",   "Scan I2C bus + DB",       true  },
    { "LoRa Channel",  "Heltec node bridge",       true  },
    { "BLE UART",      "Scan/connect NUS terminal", true },
};
static constexpr int kMenuItemCount = (int)(sizeof(kMenuItems) / sizeof(kMenuItems[0]));

void menu_Setup();
void menu_Loop();

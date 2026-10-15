#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include "wifi_test.h"

void LCD_Display_Init(void);
extern volatile uint32_t g_lcd_max_field_ms;
extern volatile uint32_t g_lcd_field_updates;
/* At most one changed field per call; called after draining Wi-Fi RX. */
void LCD_Display_Service(const WifiDisplaySnapshot *snapshot, uint32_t now);

#endif

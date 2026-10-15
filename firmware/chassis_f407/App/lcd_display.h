#ifndef LCD_DISPLAY_H
#define LCD_DISPLAY_H

#include <stdint.h>
#include "diagnostics.h"

void LCD_Display_Init(void);
void LCD_Display_Update(const DiagnosticsFrame *frame);

#endif

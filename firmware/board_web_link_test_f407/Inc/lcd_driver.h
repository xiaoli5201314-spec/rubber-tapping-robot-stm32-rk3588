#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include <stdint.h>

#define LCD_WIDTH  480
#define LCD_HEIGHT 800

#define LCD_FSMC_BANK_BASE      0x6C000000UL
#define LCD_FSMC_RS_ADDRESS_BIT 6UL
#define LCD_COMMAND_ADDRESS \
    (LCD_FSMC_BANK_BASE | (((1UL << LCD_FSMC_RS_ADDRESS_BIT) * 2UL) - 2UL))
#define LCD_DATA_ADDRESS (LCD_COMMAND_ADDRESS + 2UL)

#define LCD_COLOR_WHITE   0xFFFF
#define LCD_COLOR_BLACK   0x0000
#define LCD_COLOR_RED     0xF800
#define LCD_COLOR_GREEN   0x07E0
#define LCD_COLOR_BLUE    0x001F
#define LCD_COLOR_YELLOW  0xFFE0
#define LCD_COLOR_CYAN    0x07FF
#define LCD_COLOR_MAGENTA 0xF81F
#define LCD_COLOR_GRAY    0x7BEF

void LCD_Init(void);
void LCD_Clear(uint16_t color);
void LCD_SetWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void LCD_WritePixel(uint16_t color);
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void LCD_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg);
void LCD_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg);
void LCD_DrawCharScaled(uint16_t x, uint16_t y, char c, uint8_t scale,
                        uint16_t fg, uint16_t bg);
void LCD_DrawStringScaled(uint16_t x, uint16_t y, const char *str, uint8_t scale,
                          uint16_t fg, uint16_t bg);
void LCD_DrawNum(uint16_t x, uint16_t y, uint32_t num, uint8_t digits, uint16_t fg, uint16_t bg);
void LCD_DrawChinese(uint16_t x, uint16_t y, uint16_t unicode, uint16_t fg, uint16_t bg);
void LCD_DrawChineseString(uint16_t x, uint16_t y, const uint16_t *str, uint16_t fg, uint16_t bg);
void LCD_DrawChineseSized(uint16_t x, uint16_t y, uint16_t unicode, uint8_t size,
                          uint16_t fg, uint16_t bg);
void LCD_DrawChineseStringSized(uint16_t x, uint16_t y, const uint16_t *str,
                                uint8_t size, uint16_t fg, uint16_t bg);
void LCD_SetBacklight(uint8_t enabled);

#endif

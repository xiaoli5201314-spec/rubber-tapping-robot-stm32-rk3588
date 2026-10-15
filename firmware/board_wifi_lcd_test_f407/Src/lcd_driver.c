#include "lcd_driver.h"
#include "stm32f4xx.h"
#include "board.h"
#include "lcd_nt35510_init.h"
#include "lcd_font_8x16.h"
#include "lcd_font_chinese.h"

#define LCD_CMD  (*(volatile uint16_t *)LCD_COMMAND_ADDRESS)
#define LCD_DATA (*(volatile uint16_t *)LCD_DATA_ADDRESS)

static void LCD_WriteReg(uint16_t reg, uint16_t val) {
    LCD_CMD = reg;
    LCD_DATA = val;
}

static void LCD_WriteCmd(uint16_t cmd) {
    LCD_CMD = cmd;
}

static void LCD_Delay(uint32_t ms) {
    Board_Delay(ms);
}

static void LCD_ConfigurePins(GPIO_TypeDef *gpio, uint16_t pins,
                              uint8_t alternate) {
    for (uint32_t pin = 0U; pin < 16U; ++pin) {
        const uint32_t bit = 1UL << pin;
        const uint32_t shift = pin * 2U;
        const uint32_t af_shift = (pin % 8U) * 4U;
        if (((uint32_t)pins & bit) == 0U) {
            continue;
        }
        gpio->OTYPER &= ~bit;
        /* Match the original LCD board's fast, pulled-up parallel bus. */
        gpio->OSPEEDR = (gpio->OSPEEDR & ~(3UL << shift)) | (2UL << shift);
        gpio->PUPDR = (gpio->PUPDR & ~(3UL << shift)) |
                       ((alternate != 0U ? 1UL : 0UL) << shift);
        if (alternate != 0U) {
            gpio->AFR[pin / 8U] =
                (gpio->AFR[pin / 8U] & ~(15UL << af_shift)) |
                ((uint32_t)alternate << af_shift);
        }
        gpio->MODER = (gpio->MODER & ~(3UL << shift)) |
                       ((alternate != 0U ? 2UL : 1UL) << shift);
    }
}

void LCD_Init(void) {
    RCC->AHB3ENR |= RCC_AHB3ENR_FSMCEN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOEEN |
                    RCC_AHB1ENR_GPIOFEN | RCC_AHB1ENR_GPIOGEN |
                    RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;
    (void)RCC->AHB3ENR;
    __DSB();

    /* PD0/1/8/9/10/14/15: D2/D3/D13-D15/D0/D1; PD4/5: NOE/NWE. */
    LCD_ConfigurePins(GPIOD, (1U << 0) | (1U << 1) | (1U << 4) |
                             (1U << 5) | (1U << 8) | (1U << 9) |
                             (1U << 10) | (1U << 14) | (1U << 15), 12U);
    /* PE7-PE15: D4-D12. */
    LCD_ConfigurePins(GPIOE, 0xFF80U, 12U);
    /* PF12: A6 (RS). */
    LCD_ConfigurePins(GPIOF, 1U << 12, 12U);
    /* PG12: NE4 (CS). */
    LCD_ConfigurePins(GPIOG, 1U << 12, 12U);
    GPIOB->BSRR = 1UL << (15U + 16U);
    LCD_ConfigurePins(GPIOB, 1U << 15, 0U);

    /* Bank 4, 16-bit asynchronous SRAM mode with separate read/write timing. */
    FSMC_Bank1->BTCR[6] = 0U;
    FSMC_Bank1->BTCR[7] = (15U << FSMC_BTR1_ADDSET_Pos) |
                          (60U << FSMC_BTR1_DATAST_Pos);
    FSMC_Bank1E->BWTR[6] = (9U << FSMC_BWTR1_ADDSET_Pos) |
                           (9U << FSMC_BWTR1_DATAST_Pos);
    FSMC_Bank1->BTCR[6] = FSMC_BCR1_MWID_0 |
                          FSMC_BCR1_WREN |
                          FSMC_BCR1_EXTMOD |
                          FSMC_BCR1_MBKEN;

    LCD_Delay(50);

    for (uint16_t i = 0U; i < LCD_NT35510_INIT_COUNT; i++) {
        LCD_WriteReg(LCD_NT35510_INIT[i][0], LCD_NT35510_INIT[i][1]);
    }

    for (uint16_t i = 0U; i < LCD_NT35510_CMD_ONLY_COUNT; i++) {
        LCD_WriteCmd(LCD_NT35510_CMD_ONLY[i]);
        if (i == 0U) {
            LCD_Delay(120);
        }
    }

    /* Portrait mode: 480 x 800, matching the panel's native orientation. */
    LCD_WriteReg(0x3600U, 0x0000U);

    /* HCLK stays at 16 MHz; these timings are slower than the original
       168 MHz driver and comfortably allow register and pixel writes. */
    FSMC_Bank1E->BWTR[6] = (2U << FSMC_BWTR1_ADDSET_Pos) |
                           (2U << FSMC_BWTR1_DATAST_Pos);
    LCD_Delay(10);
}

void LCD_SetBacklight(uint8_t enabled) {
    GPIOB->BSRR = (enabled != 0U) ? (1UL << 15) : (1UL << 31);
}

void LCD_SetWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    if ((w == 0U) || (h == 0U)) {
        return;
    }

    uint16_t xe = x + w - 1;
    uint16_t ye = y + h - 1;

    LCD_WriteReg(0x2A00, x >> 8);
    LCD_WriteReg(0x2A01, x & 0xFF);
    LCD_WriteReg(0x2A02, xe >> 8);
    LCD_WriteReg(0x2A03, xe & 0xFF);

    LCD_WriteReg(0x2B00, y >> 8);
    LCD_WriteReg(0x2B01, y & 0xFF);
    LCD_WriteReg(0x2B02, ye >> 8);
    LCD_WriteReg(0x2B03, ye & 0xFF);

    LCD_WriteCmd(0x2C00);
}

void LCD_WritePixel(uint16_t color) {
    LCD_DATA = color;
}

void LCD_Clear(uint16_t color) {
    LCD_SetWindow(0, 0, LCD_WIDTH, LCD_HEIGHT);
    for (uint32_t i = 0; i < (uint32_t)LCD_WIDTH * LCD_HEIGHT; i++) {
        LCD_WritePixel(color);
    }
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if ((x >= LCD_WIDTH) || (y >= LCD_HEIGHT) || (w == 0U) || (h == 0U)) {
        return;
    }
    if (((uint32_t)x + w) > LCD_WIDTH) {
        w = (uint16_t)(LCD_WIDTH - x);
    }
    if (((uint32_t)y + h) > LCD_HEIGHT) {
        h = (uint16_t)(LCD_HEIGHT - y);
    }

    LCD_SetWindow(x, y, w, h);
    for (uint32_t i = 0; i < (uint32_t)w * h; i++) {
        LCD_WritePixel(color);
    }
}

void LCD_DrawChar(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg) {
    if (((uint32_t)x + 8U > LCD_WIDTH) || ((uint32_t)y + 16U > LCD_HEIGHT)) {
        return;
    }
    if (c < 32 || c > 126) {
        c = 32;
    }
    const uint8_t *glyph = LCD_FONT_8X16[c - 32];

    LCD_SetWindow(x, y, 8, 16);
    for (uint8_t row = 0; row < 16; row++) {
        uint8_t line = glyph[row];
        for (uint8_t col = 0; col < 8; col++) {
            LCD_WritePixel((line & 0x80) ? fg : bg);
            line <<= 1;
        }
    }
}

void LCD_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t fg, uint16_t bg) {
    while (*str) {
        LCD_DrawChar(x, y, *str++, fg, bg);
        x += 8;
    }
}

void LCD_DrawCharScaled(uint16_t x, uint16_t y, char c, uint8_t scale,
                        uint16_t fg, uint16_t bg) {
    uint16_t width;
    uint16_t height;
    const uint8_t *glyph;

    if (scale == 0U) {
        return;
    }
    width = (uint16_t)(8U * scale);
    height = (uint16_t)(16U * scale);
    if (((uint32_t)x + width > LCD_WIDTH) || ((uint32_t)y + height > LCD_HEIGHT)) {
        return;
    }
    if ((c < 32) || (c > 126)) {
        c = 32;
    }
    glyph = LCD_FONT_8X16[c - 32];

    LCD_SetWindow(x, y, width, height);
    for (uint16_t row = 0U; row < height; row++) {
        uint8_t line = glyph[row / scale];
        for (uint16_t col = 0U; col < width; col++) {
            uint8_t mask = (uint8_t)(0x80U >> (col / scale));
            LCD_WritePixel((line & mask) ? fg : bg);
        }
    }
}

void LCD_DrawStringScaled(uint16_t x, uint16_t y, const char *str, uint8_t scale,
                          uint16_t fg, uint16_t bg) {
    uint16_t advance = (uint16_t)(8U * scale);

    while (*str) {
        LCD_DrawCharScaled(x, y, *str++, scale, fg, bg);
        x = (uint16_t)(x + advance);
    }
}

void LCD_DrawNum(uint16_t x, uint16_t y, uint32_t num, uint8_t digits, uint16_t fg, uint16_t bg) {
    char buf[12];
    uint8_t i = 0;
    if (num == 0) {
        buf[i++] = '0';
    } else {
        uint32_t n = num;
        while (n > 0 && i < digits) {
            buf[i++] = '0' + (n % 10);
            n /= 10;
        }
    }
    while (i < digits && i < 11) {
        buf[i++] = ' ';
    }
    buf[i] = '\0';

    for (uint8_t j = 0; j < i / 2; j++) {
        char tmp = buf[j];
        buf[j] = buf[i - 1 - j];
        buf[i - 1 - j] = tmp;
    }

    LCD_DrawString(x, y, buf, fg, bg);
}

static int16_t LCD_FindChineseIndex(uint16_t unicode) {
    int16_t idx = -1;

    for (uint16_t i = 0U; i < CHINESE_CHAR_COUNT; i++) {
        if (chinese_char_codes[i] == unicode) {
            idx = (int16_t)i;
            break;
        }
    }
    return idx;
}

void LCD_DrawChineseSized(uint16_t x, uint16_t y, uint16_t unicode, uint8_t size,
                          uint16_t fg, uint16_t bg) {
    int16_t idx = LCD_FindChineseIndex(unicode);
    const uint8_t *data;
    uint8_t bytes_per_row;

    if ((idx < 0) || ((uint32_t)x + size > LCD_WIDTH) ||
        ((uint32_t)y + size > LCD_HEIGHT)) {
        return;
    }

    if (size == CHINESE_FONT_16_SIZE) {
        data = chinese_font_16_data[(uint16_t)idx];
    } else if (size == CHINESE_FONT_24_SIZE) {
        data = chinese_font_24_data[(uint16_t)idx];
    } else if (size == CHINESE_FONT_48_SIZE) {
        data = chinese_font_48_data[(uint16_t)idx];
    } else {
        return;
    }
    bytes_per_row = (uint8_t)((size + 7U) / 8U);

    LCD_SetWindow(x, y, size, size);
    for (uint8_t row = 0U; row < size; row++) {
        for (uint8_t col = 0U; col < size; col++) {
            uint8_t value = data[(uint16_t)row * bytes_per_row + (col / 8U)];
            uint8_t mask = (uint8_t)(0x80U >> (col % 8U));
            LCD_WritePixel((value & mask) ? fg : bg);
        }
    }
}

void LCD_DrawChineseStringSized(uint16_t x, uint16_t y, const uint16_t *str,
                                uint8_t size, uint16_t fg, uint16_t bg) {
    while (*str) {
        LCD_DrawChineseSized(x, y, *str++, size, fg, bg);
        x = (uint16_t)(x + size);
    }
}

void LCD_DrawChinese(uint16_t x, uint16_t y, uint16_t unicode, uint16_t fg, uint16_t bg) {
    LCD_DrawChineseSized(x, y, unicode, CHINESE_FONT_16_SIZE, fg, bg);
}

void LCD_DrawChineseString(uint16_t x, uint16_t y, const uint16_t *str,
                           uint16_t fg, uint16_t bg) {
    LCD_DrawChineseStringSized(x, y, str, CHINESE_FONT_16_SIZE, fg, bg);
}

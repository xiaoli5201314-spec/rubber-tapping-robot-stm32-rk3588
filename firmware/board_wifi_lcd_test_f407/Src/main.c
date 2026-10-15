#include "board.h"
#include "wifi_test.h"
#include "lcd_display.h"

int main(void)
{
    Board_Init();
    LCD_Display_Init();
    WifiTest_Run();
    return 0;
}

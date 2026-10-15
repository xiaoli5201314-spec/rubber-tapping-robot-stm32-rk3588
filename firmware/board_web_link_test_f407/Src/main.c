#include "board.h"
#include "wifi_test.h"
#include "test_config.h"
#if !WEB_NO_LCD
#include "lcd_display.h"
#endif
#if WEB_RC_TEST
#include "rc_web_test.h"
#endif

int main(void)
{
    Board_Init();
#if WEB_RC_TEST
    RcWeb_Init();
#endif
#if !WEB_NO_LCD
    LCD_Display_Init();
#endif
    WifiTest_Run();
    return 0;
}

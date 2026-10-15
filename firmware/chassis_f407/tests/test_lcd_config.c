#include "lcd_driver.h"
#include "test_support.h"

TEST(explorer_f407_lcd_uses_fsmc_bank4_a6_addresses)
{
    ASSERT_EQ_U32(0x6C00007EU, LCD_COMMAND_ADDRESS);
    ASSERT_EQ_U32(0x6C000080U, LCD_DATA_ADDRESS);
}

TEST(nt35510_portrait_dimensions_match_panel)
{
    ASSERT_EQ_U32(480U, LCD_WIDTH);
    ASSERT_EQ_U32(800U, LCD_HEIGHT);
}

int main(void)
{
    RUN_TEST(explorer_f407_lcd_uses_fsmc_bank4_a6_addresses);
    RUN_TEST(nt35510_portrait_dimensions_match_panel);

    return (test_failures == 0) ? 0 : 1;
}

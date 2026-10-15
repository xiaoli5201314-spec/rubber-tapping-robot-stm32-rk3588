#include "board.h"
#include "wifi_test.h"

int main(void)
{
    Board_Init();
    WifiTest_Run();
    return 0;
}

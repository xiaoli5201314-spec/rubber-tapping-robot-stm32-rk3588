#include "uart4_io.h"
#include "uart4_test.h"

int main(void)
{
    int byte;
    Uart4_Init();
    Uart4Test_Init();
    while (1)
    {
        while ((byte = Uart4_Read()) >= 0)
        {
            Uart4Test_Feed((uint8_t)byte, Uart4_Millis());
        }
        Uart4Test_Service(Uart4_Millis());
    }
}

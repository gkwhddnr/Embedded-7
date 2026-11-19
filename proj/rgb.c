#include <wiringPi.h>
#include <stdio.h>

#define RED   21
#define BLUE  20
#define GREEN 19

// 공통 양극형: 1이면 켜짐(LOW), 0이면 꺼짐(HIGH)
void setColor(int red, int green, int blue)
{
    digitalWrite(RED,   red   ? LOW : HIGH);
    digitalWrite(GREEN, green ? LOW : HIGH);
    digitalWrite(BLUE,  blue  ? LOW : HIGH);
}

int main(void)
{
    if (wiringPiSetupGpio() < 0)
        return 1;

    pinMode(RED, OUTPUT);
    pinMode(BLUE, OUTPUT);
    pinMode(GREEN, OUTPUT);

    setColor(0, 0, 0);
    puts("모두 OFF");
    delay(2000);

    setColor(1, 0, 0);
    puts("빨강 ON");
    delay(2000);

    setColor(0, 1, 0);
    puts("초록 ON");
    delay(2000);

    setColor(0, 0, 1);
    puts("파랑 ON");
    delay(2000);

    setColor(1, 1, 1);
    puts("세 색 모두 ON");
    delay(2000);

    setColor(0, 0, 0);
    puts("모두 OFF");

    return 0;
}
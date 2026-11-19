#include <wiringPi.h>
#include <stdio.h>
#include <signal.h>

#define BUZZER 18   // P18 = GPIO18 = 물리 12번

static volatile sig_atomic_t running = 1;

void stopBuzzer(int sig)
{
    (void)sig;
    running = 0;
}

int main(void)
{
    if (wiringPiSetupGpio() == -1) {
        fprintf(stderr, "GPIO 초기화 실패\n");
        return 1;
    }

    signal(SIGINT, stopBuzzer);
    signal(SIGTERM, stopBuzzer);

    // 방금 작동한 코드와 같은 PWM 설정
    pinMode(BUZZER, PWM_OUTPUT);
    pwmSetMode(PWM_MODE_MS);
    pwmSetClock(19);
    pwmSetRange(1000);
    pwmWrite(BUZZER, 0);

    puts("부저 시험 시작");

    for (int i = 0; i < 3 && running; i++) {
        pwmWrite(BUZZER, 500);  // 듀티비 50%: 소리 출력

        for (int j = 0; j < 50 && running; j++)
            delay(10);         // 총 0.5초

        pwmWrite(BUZZER, 0);   // 소리 정지

        for (int j = 0; j < 50 && running; j++)
            delay(10);         // 총 0.5초
    }

    pwmWrite(BUZZER, 0);
    puts("부저 시험 종료");

    return 0;
}
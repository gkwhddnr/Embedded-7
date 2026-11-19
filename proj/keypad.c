#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <wiringPi.h>

#define SCL 23   // P23 = 물리 16번
#define SDO 24   // P24 = 물리 18번

static volatile sig_atomic_t running = 1;

static void stop(int sig)
{
    (void)sig;
    running = 0;
}

/* 각 비트가 1이면 해당 키가 눌린 상태 */
static uint16_t readKeys(void)
{
    uint16_t keys = 0;

    /* 이전 읽기와 간격 확보 */
    digitalWrite(SCL, HIGH);
    delay(3);

    for (int i = 0; i < 16; i++) {
        digitalWrite(SCL, LOW);
        delayMicroseconds(10);

        if (digitalRead(SDO) == LOW)
            keys |= (uint16_t)(1u << i);

        digitalWrite(SCL, HIGH);
        delayMicroseconds(10);
    }

    return keys;
}

int main(void)
{
    if (wiringPiSetupGpio() < 0) {
        fprintf(stderr, "GPIO 초기화 실패\n");
        return 1;
    }

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    pinMode(SCL, OUTPUT);
    digitalWrite(SCL, HIGH);

    pinMode(SDO, INPUT);
    pullUpDnControl(SDO, PUD_OFF);

    puts("초기화 중: 키패드에서 손을 떼세요.");
    delay(1000);

    puts("키패드 준비 완료. 1~16을 누르세요.");
    puts("종료: Ctrl+C");

    uint16_t previous = 0;

    while (running) {
        uint16_t current = readKeys();

        /* 새로 눌린 키만 출력 */
        uint16_t pressed =
            (uint16_t)(current & (uint16_t)~previous);

        for (int i = 0; i < 8; i++) {
            if (pressed & (1u << i))
                printf("눌린 키: %d\n", i + 1);
        }

        fflush(stdout);
        previous = current;
        delay(20);
    }

    digitalWrite(SCL, HIGH);
    puts("\n키패드 시험 종료");
    return 0;
}
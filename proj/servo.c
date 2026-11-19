#include <wiringPi.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <signal.h>

#define SERVO 14   // GPIO14 = P14 = 물리 8번

static atomic_int pulse_us = 1500;
static atomic_int running = 1;
static volatile sig_atomic_t interrupted = 0;

static void stop(int sig)
{
    (void)sig;
    interrupted = 1;
}

/* 입력 대기 중에도 서보 제어 펄스를 계속 출력 */
static void *servoLoop(void *arg)
{
    (void)arg;

    while (atomic_load(&running) && !interrupted) {
        int pulse = atomic_load(&pulse_us);

        digitalWrite(SERVO, HIGH);
        delayMicroseconds(pulse);

        digitalWrite(SERVO, LOW);
        delayMicroseconds(20000 - pulse);
    }

    digitalWrite(SERVO, LOW);
    return NULL;
}

int main(void)
{
    pthread_t thread;
    char input[64];
    float angle;

    if (wiringPiSetupGpio() < 0) {
        fprintf(stderr, "GPIO 초기화 실패\n");
        return 1;
    }

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    pinMode(SERVO, OUTPUT);
    digitalWrite(SERVO, LOW);

    if (pthread_create(&thread, NULL, servoLoop, NULL) != 0) {
        fprintf(stderr, "서보 제어 스레드 생성 실패\n");
        return 1;
    }

    puts("서보 시험 시작: 처음에는 0, 20, -20으로 시험하세요.");

    while (!interrupted) {
        printf("angle(-90~90), 종료 q: ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        if (input[0] == 'q' || input[0] == 'Q')
            break;

        if (sscanf(input, "%f", &angle) != 1 ||
            !(angle >= -90 && angle <= 90)) {
            puts("-90~90 사이 숫자를 입력하세요.");
            continue;
        }

        /* -90 → 1000us, 0 → 1500us, +90 → 2000us */
        int pulse = (int)(1500 + angle * 1000.0f / 90.0f);
        atomic_store(&pulse_us, pulse);

        printf("설정: %.1f, 펄스폭: %dus\n", angle, pulse);
    }

    atomic_store(&running, 0);
    pthread_join(thread, NULL);
    digitalWrite(SERVO, LOW);

    puts("\n서보 시험 종료");
    return 0;
}
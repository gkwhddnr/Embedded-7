#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <wiringPi.h>

#define SCL   23
#define SDO   24
#define LED   16
#define SERVO 14

#define PASSWORD "1234"
#define PASSWORD_LENGTH 4

static atomic_int pulse_us = 500;
static atomic_int running = 1;
static volatile sig_atomic_t interrupted = 0;

static void stop(int sig)
{
    (void)sig;
    interrupted = 1;
}

static void waitMs(int ms)
{
    for (int elapsed = 0;
         elapsed < ms && !interrupted;
         elapsed += 10) {
        delay(10);
    }
}

/* 시험 완료한 범위: -90=500us, 90=2500us */
static void rotateServo(float angle)
{
    int pulse = (int)(1500 + angle * 1000.0f / 90.0f);
    atomic_store(&pulse_us, pulse);
}

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

/* 8키 모드: 각 비트가 1이면 해당 키가 눌림 */
static uint8_t readKeys(void)
{
    uint8_t keys = 0;

    digitalWrite(SCL, HIGH);
    delay(3);

    for (int i = 0; i < 8; i++) {
        digitalWrite(SCL, LOW);
        delayMicroseconds(10);

        if (digitalRead(SDO) == LOW)
            keys |= (uint8_t)(1u << i);

        digitalWrite(SCL, HIGH);
        delayMicroseconds(10);
    }

    return keys;
}

/* 한 키만 눌렸으면 1~8, 아니면 0 */
static int keyNumber(uint8_t keys)
{
    if (keys == 0 || (keys & (keys - 1)) != 0)
        return 0;

    for (int i = 0; i < 8; i++) {
        if (keys & (1u << i))
            return i + 1;
    }

    return 0;
}

static void authenticationResult(int success)
{
    if (success) {
        puts("\n인증 성공! 문을 엽니다.");

        rotateServo(90);
        waitMs(3000);

        if (!interrupted) {
            puts("문을 닫습니다.");
            rotateServo(-90);
            waitMs(1000);
        }
    } else {
        puts("\n인증 실패! LED 3번 깜빡임");

        for (int i = 0; i < 3 && !interrupted; i++) {
            digitalWrite(LED, LOW);
            waitMs(250);
            digitalWrite(LED, HIGH);
            waitMs(250);
        }
    }
}

int main(void)
{
    pthread_t thread;
    char input[PASSWORD_LENGTH + 1] = {0};
    int length = 0;

    if (wiringPiSetupGpio() < 0) {
        fprintf(stderr, "GPIO 초기화 실패\n");
        return 1;
    }

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    pinMode(LED, OUTPUT);
    digitalWrite(LED, LOW);

    pinMode(SERVO, OUTPUT);
    digitalWrite(SERVO, LOW);

    pinMode(SCL, OUTPUT);
    digitalWrite(SCL, HIGH);

    pinMode(SDO, INPUT);
    pullUpDnControl(SDO, PUD_OFF);

    rotateServo(-90);

    if (pthread_create(&thread, NULL, servoLoop, NULL) != 0) {
        fprintf(stderr, "서보 스레드 생성 실패\n");
        return 1;
    }

    puts("초기화 중: 키패드에서 손을 떼세요.");
    waitMs(1000);

    digitalWrite(LED, HIGH);
    puts("LED 켜짐: 비밀번호 4자리를 입력하세요.");
    puts("종료: Ctrl+C");

    uint8_t previous = 0;
    uint8_t stable = 0;
    int sameCount = 0;
    int armed = 0;

    while (!interrupted) {
        uint8_t current = readKeys();

        /* 같은 상태가 두 번 연속 확인되면 처리 */
        if (current == previous) {
            if (sameCount < 2)
                sameCount++;
        } else {
            previous = current;
            sameCount = 1;
        }

        if (sameCount >= 2) {
            stable = current;

            if (stable == 0) {
                /* 손을 뗀 뒤에만 다음 입력 허용 */
                armed = 1;
            } else if (armed) {
                armed = 0;
                int key = keyNumber(stable);

                if (key == 0) {
                    puts("\n한 번에 한 키씩 눌러 주세요.");
                } else {
                    input[length++] = (char)('0' + key);
                    input[length] = '\0';

                    printf("*");
                    fflush(stdout);

                    if (length == PASSWORD_LENGTH) {
                        int success =
                            strcmp(input, PASSWORD) == 0;

                        authenticationResult(success);

                        length = 0;
                        input[0] = '\0';

                        /* 처리 중 입력은 버리고 손 떼기 대기 */
                        previous = 0;
                        sameCount = 0;
                        armed = 0;

                        if (!interrupted) {
                            digitalWrite(LED, HIGH);
                            puts("다음 비밀번호를 입력하세요.");
                        }
                    }
                }
            }
        }

        waitMs(20);
    }

    atomic_store(&running, 0);
    pthread_join(thread, NULL);

    digitalWrite(LED, LOW);
    digitalWrite(SERVO, LOW);
    digitalWrite(SCL, HIGH);

    puts("\n종료: LED와 서보 신호 정지");
    return 0;
}   
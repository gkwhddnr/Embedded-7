#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#include <wiringPi.h>
#include <nfc/nfc.h>

#define SERVO       14      // P14 = GPIO14 = 물리 8번
#define CLOSE_ANGLE (-90)
#define OPEN_ANGLE  90
#define OPEN_TIME   3000    // 문 열림 유지 시간(ms)

typedef struct {
    const char *name;
    size_t length;
    uint8_t uid[10];
} RegisteredTag;

/* 등록된 태그 */
static const RegisteredTag tags[] = {
    { "작업자1", 4, { 0xB0, 0xDB, 0x5C, 0x0E } },
    { "작업자2", 4, { 0x73, 0x4A, 0xC2, 0x91 } }
};

static const size_t tag_count =
    sizeof(tags) / sizeof(tags[0]);

/* 사용자가 시험한 500~2500us 범위 */
static atomic_int pulse_us = 500;
static atomic_int running = 1;
static volatile sig_atomic_t interrupted = 0;

static void stop(int sig)
{
    (void)sig;
    interrupted = 1;
}

/* 종료 요청을 확인하면서 대기 */
static void waitMs(int ms)
{
    for (int elapsed = 0;
         elapsed < ms && !interrupted;
         elapsed += 10) {
        usleep(10000);
    }
}

/* -90 → 500us, 0 → 1500us, 90 → 2500us */
static void rotateServo(float angle)
{
    if (angle < -90) angle = -90;
    if (angle > 90) angle = 90;

    int pulse = (int)(1500 + angle * 1000.0f / 90.0f);
    atomic_store(&pulse_us, pulse);
}

/* NFC를 읽는 동안에도 서보 펄스 출력 */
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

static int findTag(const uint8_t *uid, size_t length)
{
    for (size_t i = 0; i < tag_count; i++) {
        if (length == tags[i].length &&
            memcmp(uid, tags[i].uid, length) == 0) {
            return (int)i;
        }
    }

    return -1;
}

int main(void)
{
    nfc_context *context = NULL;
    nfc_device *device = NULL;
    pthread_t thread;
    int result = 0;

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    if (wiringPiSetupGpio() < 0) {
        fprintf(stderr, "GPIO 초기화 실패\n");
        return 1;
    }

    pinMode(SERVO, OUTPUT);
    digitalWrite(SERVO, LOW);

    nfc_init(&context);
    if (context == NULL) {
        fprintf(stderr, "libnfc 초기화 실패\n");
        return 1;
    }

    /* 기존 pn532.conf 설정 사용 */
    device = nfc_open(context, NULL);
    if (device == NULL) {
        fprintf(stderr, "NFC 장치 열기 실패\n");
        nfc_exit(context);
        return 1;
    }

    if (nfc_initiator_init(device) < 0 ||
        nfc_device_set_property_bool(
            device, NP_INFINITE_SELECT, false) < 0) {
        nfc_perror(device, "NFC 초기화 실패");
        nfc_close(device);
        nfc_exit(context);
        return 1;
    }

    rotateServo(CLOSE_ANGLE);

    if (pthread_create(&thread, NULL, servoLoop, NULL) != 0) {
        fprintf(stderr, "서보 스레드 생성 실패\n");
        nfc_close(device);
        nfc_exit(context);
        return 1;
    }

    const nfc_modulation modulation = {
        .nmt = NMT_ISO14443A,
        .nbr = NBR_106
    };

    puts("초기 위치: -90 (문 닫힘)");
    waitMs(1000);

    puts("NFC 태그를 대세요. 종료: Ctrl+C");

    while (!interrupted) {
        nfc_target target;

        int count = nfc_initiator_select_passive_target(
            device, modulation, NULL, 0, &target
        );

        if (interrupted)
            break;

        if (count < 0) {
            nfc_perror(device, "태그 읽기 실패");
            result = 1;
            break;
        }

        if (count == 0) {
            waitMs(100);
            continue;
        }

        size_t length = target.nti.nai.szUidLen;
        const uint8_t *uid = target.nti.nai.abtUid;

        printf("\nUID: ");
        for (size_t i = 0; i < length; i++)
            printf("%02X ", uid[i]);
        putchar('\n');

        int index = findTag(uid, length);

        if (index >= 0) {
            printf("인증 성공: %s\n", tags[index].name);

            puts("문 열기: -90 → 90");
            rotateServo(OPEN_ANGLE);

            waitMs(OPEN_TIME);

            if (!interrupted) {
                puts("문 닫기: 90 → -90");
                rotateServo(CLOSE_ANGLE);
                waitMs(1000);
            }
        } else {
            puts("인증 실패: 등록되지 않은 태그");
        }

        if (interrupted)
            break;

        /* 태그를 뗄 때까지 기다려 중복 작동 방지 */
        puts("태그를 떼어 주세요.");
        fflush(stdout);

        int present;
        while (!interrupted) {
            present =
                nfc_initiator_target_is_present(device, NULL);

            if (present == NFC_ETGRELEASED)
                break;

            if (present < 0) {
                nfc_perror(device, "태그 제거 확인 실패");
                result = 1;
                break;
            }

            waitMs(100);
        }

        if (interrupted || result != 0)
            break;

        nfc_initiator_deselect_target(device);
        puts("다음 태그를 대세요.");
    }

    atomic_store(&running, 0);
    pthread_join(thread, NULL);
    digitalWrite(SERVO, LOW);

    nfc_close(device);
    nfc_exit(context);

    puts("\n프로그램 종료: 서보 신호 정지");
    return result;
}
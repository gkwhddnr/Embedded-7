
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <wiringPi.h>
#include <nfc/nfc.h>

/* GPIO 번호: BCM 기준 */
#define SCL 23
#define SDO 24
#define SERVO 14

#define OLED_ADDR 0x3C
#define OLED_WIDTH 128

#define PASSWORD "1234"
#define OPEN_TIME 10000

#define CLOSE_ANGLE (-90)
#define OPEN_ANGLE 90

typedef struct {
    const char *name;
    size_t length;
    uint8_t uid[10];
} RegisteredTag;

static const RegisteredTag tags[] = {
    {"작업자1", 4, {0xB0, 0xDB, 0x5C, 0x0E}},
    {"작업자2", 4, {0x73, 0x4A, 0xC2, 0x91}}
};

static const size_t tag_count =
    sizeof(tags) / sizeof(tags[0]);

/* 0=대기, 1=키패드 성공, 2=NFC 성공, 3=문 작동 중 */
static atomic_int auth_state = 0;
static atomic_int pulse_us = 500;
static atomic_int running = 1;

static volatile sig_atomic_t interrupted = 0;
static int oled_fd = -1;

static void stop(int sig)
{
    (void)sig;
    interrupted = 1;
}

static void waitMs(int ms)
{
    for (int t = 0; t < ms && !interrupted; t += 10)
        usleep(10000);
}

/* ---------------- 키패드 ---------------- */

static uint16_t readKeys(void)
{
    uint16_t keys = 0;

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

/* ---------------- OLED ---------------- */

static int oledCommand(uint8_t cmd)
{
    uint8_t data[2] = {0x00, cmd};
    return write(oled_fd, data, 2) == 2 ? 0 : -1;
}

static int oledInit(void)
{
    oled_fd = open("/dev/i2c-1", O_RDWR);

    if (oled_fd < 0) {
        perror("OLED 열기 실패");
        return -1;
    }

    if (ioctl(oled_fd, I2C_SLAVE, OLED_ADDR) < 0) {
        perror("OLED 주소 설정 실패");
        return -1;
    }

    const uint8_t cmds[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F,
        0xD3, 0x00, 0x40, 0x8D, 0x14,
        0x20, 0x02, /* Page addressing mode */
        0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0x7F, 0xD9, 0xF1,
        0xDB, 0x40, 0xA4, 0xA6, 0xAF
    };

    for (size_t i = 0; i < sizeof(cmds); i++) {
        if (oledCommand(cmds[i]) < 0)
            return -1;
    }

    return 0;
}

static const uint8_t font[26][5] = {
    ['A'-'A'] = {0x7E,0x11,0x11,0x11,0x7E},
    ['B'-'A'] = {0x7F,0x49,0x49,0x49,0x36},
    ['C'-'A'] = {0x3E,0x41,0x41,0x41,0x22},
    ['D'-'A'] = {0x7F,0x41,0x41,0x22,0x1C},
    ['E'-'A'] = {0x7F,0x49,0x49,0x49,0x41},
    ['F'-'A'] = {0x7F,0x09,0x09,0x09,0x01},
    ['I'-'A'] = {0x41,0x41,0x7F,0x41,0x41},
    ['L'-'A'] = {0x7F,0x40,0x40,0x40,0x40},
    ['N'-'A'] = {0x7F,0x02,0x0C,0x10,0x7F},
    ['P'-'A'] = {0x7F,0x09,0x09,0x09,0x06},
    ['R'-'A'] = {0x7F,0x09,0x19,0x29,0x46},
    ['S'-'A'] = {0x46,0x49,0x49,0x49,0x31},
    ['T'-'A'] = {0x01,0x01,0x7F,0x01,0x01},
    ['U'-'A'] = {0x3F,0x40,0x40,0x40,0x3F},
    ['Y'-'A'] = {0x07,0x08,0x70,0x08,0x07}
};

static void oledText(int page, const char *str)
{
    if (page < 0 || page > 7)
        return;

    oledCommand(0xB0 + page);
    oledCommand(0x00);
    oledCommand(0x10);

    uint8_t buffer[OLED_WIDTH];
    memset(buffer, 0, sizeof(buffer));

    int pos = 0;

    while (*str && pos + 6 <= OLED_WIDTH) {
        char c = *str++;

        if (c >= 'a' && c <= 'z')
            c -= 32;

        if (c >= 'A' && c <= 'Z') {
            for (int j = 0; j < 5; j++)
                buffer[pos++] = font[c - 'A'][j];
        } else {
            pos += 5;
        }

        buffer[pos++] = 0;
    }

    for (int offset = 0; offset < OLED_WIDTH; offset += 16) {
        uint8_t chunk[17];
        chunk[0] = 0x40;

        memcpy(chunk + 1, buffer + offset, 16);
        write(oled_fd, chunk, sizeof(chunk));
    }
}

static void oledShow(const char *line1, const char *line2)
{
    for (int p = 0; p < 8; p++)
        oledText(p, "");

    oledText(2, line1);
    oledText(4, line2);
}

/* ---------------- 서보모터 ---------------- */

static void rotateServo(float angle)
{
    if (angle < -90) angle = -90;
    if (angle > 90) angle = 90;

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

/* ---------------- NFC ---------------- */

static int findTag(const uint8_t *uid, size_t length)
{
    for (size_t i = 0; i < tag_count; i++) {
        if (length == tags[i].length &&
            memcmp(uid, tags[i].uid, length) == 0)
            return (int)i;
    }

    return -1;
}

static void *nfcLoop(void *arg)
{
    (void)arg;

    nfc_context *context = NULL;
    nfc_device *device = NULL;

    nfc_init(&context);

    if (!context) {
        fprintf(stderr, "NFC 초기화 실패\n");
        return NULL;
    }

    device = nfc_open(context, NULL);

    if (!device) {
        fprintf(stderr, "NFC 장치 열기 실패\n");
        nfc_exit(context);
        return NULL;
    }

    if (nfc_initiator_init(device) < 0 ||
        nfc_device_set_property_bool(
            device, NP_INFINITE_SELECT, false) < 0) {
        nfc_perror(device, "NFC 설정 실패");
        nfc_close(device);
        nfc_exit(context);
        return NULL;
    }

    const nfc_modulation modulation = {
        .nmt = NMT_ISO14443A,
        .nbr = NBR_106
    };

    puts("NFC 준비 완료");

    while (!interrupted) {
        if (atomic_load(&auth_state) != 0) {
            waitMs(50);
            continue;
        }

        nfc_target target;

        int count = nfc_initiator_select_passive_target(
            device, modulation, NULL, 0, &target
        );

        if (interrupted)
            break;

        if (count < 0) {
            nfc_perror(device, "NFC 읽기 실패");
            break;
        }

        if (count == 0)
            continue;

        size_t length = target.nti.nai.szUidLen;
        const uint8_t *uid = target.nti.nai.abtUid;

        printf("NFC UID: ");
        for (size_t i = 0; i < length; i++)
            printf("%02X ", uid[i]);
        putchar('\n');

        int index = findTag(uid, length);

        if (index >= 0) {
            int expected = 0;

            if (atomic_compare_exchange_strong(
                    &auth_state, &expected, 2)) {
                printf("NFC 인증 성공: %s\n",
                       tags[index].name);
            }
        } else {
            puts("NFC 인증 실패");
        }

        fflush(stdout);
        nfc_initiator_deselect_target(device);

        /* 같은 태그 연속 인식 완화 */
        waitMs(300);
    }

    nfc_close(device);
    nfc_exit(context);
    return NULL;
}

/* ---------------- 출입 제어 ---------------- */

static void openDoor(void)
{
    /* 인증 잠금: 키패드와 NFC 모두 대기 */
    atomic_store(&auth_state, 3);

    oledShow("SUCCESS", "TRUE");

    puts("문 열기: -90 -> 90");
    rotateServo(OPEN_ANGLE);

    waitMs(OPEN_TIME);

    puts("문 닫기: 90 -> -90");
    rotateServo(CLOSE_ANGLE);

    waitMs(1000);

    if (!interrupted) {
        oledShow("READY", "INPUT");

        /* 새 인증 허용 */
        atomic_store(&auth_state, 0);
        puts("인증 대기 상태");
    }
}

int main(void)
{
    pthread_t servo_thread;
    pthread_t nfc_thread;

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    if (wiringPiSetupGpio() < 0) {
        fprintf(stderr, "GPIO 초기화 실패\n");
        return 1;
    }

    pinMode(SCL, OUTPUT);
    digitalWrite(SCL, HIGH);

    pinMode(SDO, INPUT);
    pullUpDnControl(SDO, PUD_OFF);

    pinMode(SERVO, OUTPUT);
    digitalWrite(SERVO, LOW);

    if (oledInit() < 0) {
        fprintf(stderr, "OLED 초기화 실패\n");
        if (oled_fd >= 0)
            close(oled_fd);
        return 1;
    }

    rotateServo(CLOSE_ANGLE);

    if (pthread_create(
            &servo_thread, NULL, servoLoop, NULL) != 0) {
        fprintf(stderr, "서보 스레드 생성 실패\n");
        close(oled_fd);
        return 1;
    }

    if (pthread_create(
            &nfc_thread, NULL, nfcLoop, NULL) != 0) {
        fprintf(stderr, "NFC 스레드 생성 실패\n");
        atomic_store(&running, 0);
        pthread_join(servo_thread, NULL);
        close(oled_fd);
        return 1;
    }

    oledShow("READY", "INPUT");

    puts("출입 통제 시스템 시작");
    puts("키패드 1234 또는 등록된 NFC 태그");
    puts("종료: Ctrl+C");

    char entered[5] = {0};
    int count = 0;
    uint16_t previous = 0;

    while (!interrupted) {
        int state = atomic_load(&auth_state);

        if (state == 1 || state == 2) {
            count = 0;
            entered[0] = '\0';

            openDoor();

            /* 열린 동안 눌린 키 재입력 방지 */
            previous = readKeys();
            continue;
        }

        if (state != 0) {
            waitMs(20);
            continue;
        }

        uint16_t current = readKeys();
        uint16_t pressed =
            (uint16_t)(current & (uint16_t)~previous);

        previous = current;

        for (int i = 0; i < 8; i++) {
            if (!(pressed & (1u << i)))
                continue;

            int key = i + 1;

            entered[count++] = '0' + key;
            entered[count] = '\0';

            printf("키패드 입력: %s\n", entered);

            if (count == 4) {
                if (strcmp(entered, PASSWORD) == 0) {
                    int expected = 0;

                    if (atomic_compare_exchange_strong(
                            &auth_state, &expected, 1)) {
                        puts("키패드 인증 성공");
                    }
                } else {
                puts("키패드 인증 실패");

                // OLED에 WAIT 표시
                oledShow("WAIT", "");

                // 2초 대기
                waitMs(2000);

                // 다시 초기 화면으로 복귀
                if (atomic_load(&auth_state) == 0)
                    oledShow("READY", "INPUT");
            }

                count = 0;
                entered[0] = '\0';
            }
        }

        fflush(stdout);
        waitMs(20);
    }

    atomic_store(&running, 0);

    pthread_join(servo_thread, NULL);
    pthread_join(nfc_thread, NULL);

    digitalWrite(SCL, HIGH);
    digitalWrite(SERVO, LOW);

    oledShow("", "");
    close(oled_fd);

    puts("\n프로그램 종료");
    return 0;
}

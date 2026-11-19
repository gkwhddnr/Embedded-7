
#include <stdio.h>
#include <stdint.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <wiringPi.h>

/* 기존에 작동하는 키패드 핀 유지 */
#define SCL 23
#define SDO 24

/* OLED 설정 */
#define OLED_ADDR 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

#define PASSWORD "1234"

static volatile sig_atomic_t running = 1;
static int oled_fd = -1;

static void stop(int sig)
{
    (void)sig;
    running = 0;
}

/* 기존에 정상 작동하는 키패드 읽기 함수 */
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

/* OLED에 명령 전송 */
static int oledCommand(uint8_t cmd)
{
    uint8_t data[2] = {0x00, cmd};
    return write(oled_fd, data, 2) == 2 ? 0 : -1;
}

/* OLED 초기화 */
static int oledInit(void)
{
    oled_fd = open("/dev/i2c-1", O_RDWR);

    if (oled_fd < 0) {
        perror("OLED I2C 열기 실패");
        return -1;
    }

    if (ioctl(oled_fd, I2C_SLAVE, OLED_ADDR) < 0) {
        perror("OLED 주소 설정 실패");
        return -1;
    }

    const uint8_t commands[] = {
        0xAE,       /* 화면 끄기 */
        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,
        0x8D, 0x14,
        0x20, 0x00,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0x7F,
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,
        0xA6,
        0xAF
    };

    for (size_t i = 0; i < sizeof(commands); i++) {
        if (oledCommand(commands[i]) < 0)
            return -1;
    }

    return 0;
}

/* 5x7 영문 글꼴: 필요한 문자만 정의 */
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

/* OLED 한 페이지(8픽셀 높이)에 문자열 출력 */
static void oledText(int page, const char *text)
{
    if (page < 0 || page > 7)
        return;

    oledCommand(0xB0 + page);
    oledCommand(0x00);
    oledCommand(0x10);

    uint8_t buffer[129];
    buffer[0] = 0x40;

    memset(buffer + 1, 0, OLED_WIDTH);

    int pos = 1;

    while (*text && pos + 5 <= 129) {
        char c = *text++;

        if (c >= 'a' && c <= 'z')
            c -= 32;

        if (c >= 'A' && c <= 'Z') {
            for (int j = 0; j < 5; j++)
                buffer[pos++] = font[c - 'A'][j];
        } else {
            pos += 5;
        }

        if (pos < 129)
            buffer[pos++] = 0x00;
    }

    /* SMBus 전송 크기 제한을 피하기 위해 분할 전송 */
    for (int offset = 0; offset < OLED_WIDTH; offset += 16) {
        uint8_t chunk[17];
        chunk[0] = 0x40;

        memcpy(chunk + 1, buffer + 1 + offset, 16);
        write(oled_fd, chunk, sizeof(chunk));
    }
}

/* OLED 화면 전체 지우기 */
static void oledClear(void)
{
    for (int page = 0; page < 8; page++)
        oledText(page, "");
}

/* OLED 두 줄 출력 */
static void oledShow(const char *line1, const char *line2)
{
    oledClear();
    oledText(2, line1);
    oledText(4, line2);
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

    if (oledInit() < 0) {
        fprintf(stderr, "OLED 초기화 실패\n");
        if (oled_fd >= 0)
            close(oled_fd);
        return 1;
    }

    puts("초기화 중: 키패드에서 손을 떼세요.");
    delay(1000);

    oledShow("READY", "INPUT");

    puts("키패드 준비 완료.");
    puts("비밀번호 4자리를 입력하세요.");
    puts("종료: Ctrl+C");

    char entered[5] = {0};
    int count = 0;

    uint16_t previous = 0;

    while (running) {
        uint16_t current = readKeys();

        uint16_t pressed =
            (uint16_t)(current & (uint16_t)~previous);

        /* 1~8번 키 인식: 기존 테스트 코드와 동일 */
        for (int i = 0; i < 8; i++) {
            if (!(pressed & (1u << i)))
                continue;

            int key = i + 1;

            printf("눌린 키: %d\n", key);

            entered[count++] = '0' + key;
            entered[count] = '\0';

            printf("입력: %s\n", entered);

            if (count == 4) {
                if (strcmp(entered, PASSWORD) == 0) {
                    puts("TRUE - 인증 성공");
                    oledShow("SUCCESS", "TRUE");
                } else {
                    puts("FALSE - 인증 실패");
                    oledShow("FAILED", "FALSE");
                }

                delay(5000);

                count = 0;
                entered[0] = '\0';

                oledShow("READY", "INPUT");
            }
        }

        fflush(stdout);
        previous = current;
        delay(20);
    }

    digitalWrite(SCL, HIGH);

    oledClear();
    close(oled_fd);

    puts("\n프로그램 종료");
    return 0;
}

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <nfc/nfc.h>

static volatile sig_atomic_t running = 1;

/* 사진에서 확인한 등록 태그 UID */
static const uint8_t allowed_uid[] = {
    0xB0, 0xDB, 0x5C, 0x0E
};

static void stop(int sig)
{
    (void)sig;
    running = 0;
}

int main(void)
{
    nfc_context *context = NULL;
    nfc_device *device = NULL;
    int result = 0;

    signal(SIGINT, stop);
    signal(SIGTERM, stop);

    nfc_init(&context);
    if (context == NULL) {
        fprintf(stderr, "libnfc 초기화 실패\n");
        return 1;
    }

    /* 앞서 작성한 /etc/nfc/devices.d 설정 사용 */
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
        result = 1;
        goto cleanup;
    }

    const nfc_modulation modulation = {
        .nmt = NMT_ISO14443A,
        .nbr = NBR_106
    };

    puts("NFC 준비 완료. 키링을 대세요. 종료: Ctrl+C");

    while (running) {
        nfc_target target;

        int count = nfc_initiator_select_passive_target(
            device, modulation, NULL, 0, &target
        );

        if (!running)
            break;

        if (count < 0) {
            nfc_perror(device, "태그 읽기 실패");
            result = 1;
            break;
        }

        if (count == 0) {
            usleep(100000);
            continue;
        }

        size_t length = target.nti.nai.szUidLen;
        const uint8_t *uid = target.nti.nai.abtUid;

        printf("UID: ");
        for (size_t i = 0; i < length; i++)
            printf("%02X ", uid[i]);
        putchar('\n');

        if (length == sizeof(allowed_uid) &&
            memcmp(uid, allowed_uid, length) == 0) {
            puts("인증 성공: 등록된 태그입니다.");
        } else {
            puts("인증 실패: 등록되지 않은 태그입니다.");
        }

        fflush(stdout);

        nfc_initiator_deselect_target(device);

        /* 시험용 재인식 간격 */
        for (int i = 0; i < 20 && running; i++)
            usleep(100000);
    }

cleanup:
    nfc_close(device);
    nfc_exit(context);
    puts("\nNFC 종료");
    return result;
}
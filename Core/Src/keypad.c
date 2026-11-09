/*
 * keypad.c
 *
 *  Created on: Nov 16, 2025
 *      Author: Mario76
 */

#include "keypad.h"
#include "main.h"
#include <string.h>

#define _KEYPAD_DELAY(x)   delay_us(x)

#define SETTLE_US     30   /* 행 전환 후 열 라인이 안정될 때까지 대기 (원본 100us도 문제는 없음) */
#define DEBOUNCE_MS   5    /* 키별 디바운스. keypad.h의 20ms는 입력 지연이 커서 5ms를 권장 */

GPIO_TypeDef* _KEYPAD_COLUMN_GPIO_PORT[] = { C1_GPIO_Port, C2_GPIO_Port, C3_GPIO_Port, C4_GPIO_Port };
uint16_t      _KEYPAD_COLUMN_GPIO_PIN[]  = { C1_Pin, C2_Pin, C3_Pin, C4_Pin };

GPIO_TypeDef* _KEYPAD_ROW_GPIO_PORT[] = { R0_GPIO_Port, R1_GPIO_Port, R2_GPIO_Port,
                                          R3_GPIO_Port, R4_GPIO_Port, R5_GPIO_Port };
uint16_t      _KEYPAD_ROW_GPIO_PIN[]  = { R0_Pin, R1_Pin, R2_Pin, R3_Pin, R4_Pin, R5_Pin };

/* 회로도와 대조 완료: 펌웨어 r0..r5 = 회로도 R1..R6, c0..c3 = C1..C4 */
const HID_Key keypadMap[ROW_SIZE][COL_SIZE] = {
    { {0x00, 0x53}, {0x00, 0x54}, {0x00, 0x55}, {0x00, 0x56} },  /* NumLock / * -  */
    { {0x00, 0x5F}, {0x00, 0x60}, {0x00, 0x61}, {0x00, 0x57} },  /* 7 8 9 +        */
    { {0x00, 0x5C}, {0x00, 0x5D}, {0x00, 0x5E}, {0x00, 0x00} },  /* 4 5 6 -        */
    { {0x00, 0x59}, {0x00, 0x5A}, {0x00, 0x5B}, {0x00, 0x58} },  /* 1 2 3 Enter    */
    { {0x00, 0x62}, {0x00, 0x00}, {0x00, 0x63}, {0x00, 0x00} },  /* 0 - . -        */
    { {0x01, 0x1D}, {0x01, 0x04}, {0x01, 0x06}, {0x01, 0x19} }   /* ^Z ^A ^C ^V    */
};

/* ---- 키 상태 ---- */
static uint8_t  key_stable[ROW_SIZE][COL_SIZE];    /* 디바운스로 확정된 상태 (1=눌림) */
static uint8_t  key_pending[ROW_SIZE][COL_SIZE];   /* raw가 확정 상태와 다른 상태가 진행 중 */
static uint32_t key_since[ROW_SIZE][COL_SIZE];     /* 다른 상태가 시작된 시각 (ms) */

/* ---- USB 전송 ---- */
static HID_KeyboardReport_TypeDef next_report;     /* 보낼 예정인 리포트 */
static HID_KeyboardReport_TypeDef tx_buf;          /* 전송 중인 버퍼: 전송이 끝날 때까지 건드리지 않음 */
static HID_KeyboardReport_TypeDef last_sent;
static uint8_t tx_pending;

/* ---- 디버그 카운터 (Live Expressions에 추가해서 관찰) ---- */
volatile uint32_t dbg_scan_count;
volatile uint32_t dbg_chatter[ROW_SIZE][COL_SIZE]; /* 디바운스 시간 안에 원래 상태로 돌아간 횟수 */
volatile uint32_t dbg_press[ROW_SIZE][COL_SIZE];   /* 확정된 press 횟수 */
volatile uint32_t dbg_tx_ok, dbg_tx_retry;
volatile uint8_t  dbg_raw[ROW_SIZE];               /* 마지막 raw 스캔 (비트 c = 열 c) */

static uint8_t hid_is_idle(void)
{
    /* state 0 = HID_IDLE (버전에 따라 이름이 HID_IDLE 또는 USBD_HID_IDLE).
       미들웨어 버전에 따라 pClassDataCmsit[hUsbDeviceFS.classId]일 수 있음 */
    USBD_HID_HandleTypeDef *hhid = (USBD_HID_HandleTypeDef *)hUsbDeviceFS.pClassData;

    return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) &&
           (hhid != NULL) && (hhid->state == 0U);
}

static void read_matrix(uint8_t raw[ROW_SIZE][COL_SIZE])
{
    for (uint8_t r = 0; r < ROW_SIZE; r++)
        HAL_GPIO_WritePin(_KEYPAD_ROW_GPIO_PORT[r], _KEYPAD_ROW_GPIO_PIN[r], GPIO_PIN_SET);

    for (uint8_t r = 0; r < ROW_SIZE; r++) {
        uint8_t bits = 0;

        HAL_GPIO_WritePin(_KEYPAD_ROW_GPIO_PORT[r], _KEYPAD_ROW_GPIO_PIN[r], GPIO_PIN_RESET);
        _KEYPAD_DELAY(SETTLE_US);

        for (uint8_t c = 0; c < COL_SIZE; c++) {
            raw[r][c] = (HAL_GPIO_ReadPin(_KEYPAD_COLUMN_GPIO_PORT[c],
                                          _KEYPAD_COLUMN_GPIO_PIN[c]) == GPIO_PIN_RESET);
            bits |= raw[r][c] << c;
        }
        dbg_raw[r] = bits;

        HAL_GPIO_WritePin(_KEYPAD_ROW_GPIO_PORT[r], _KEYPAD_ROW_GPIO_PIN[r], GPIO_PIN_SET);
    }
}

static void build_report(HID_KeyboardReport_TypeDef *rep)
{
    uint8_t n = 0;

    memset(rep, 0, sizeof(*rep));
    for (uint8_t r = 0; r < ROW_SIZE; r++) {
        for (uint8_t c = 0; c < COL_SIZE; c++) {
            HID_Key k = keypadMap[r][c];

            if (!key_stable[r][c] || k.keycode == 0)
                continue;
            if (n >= MAX_KEYS) {                       /* 7키 이상: ErrorRollOver */
                memset(rep->keycodes, 0x01, MAX_KEYS);
                return;
            }
            rep->keycodes[n++] = k.keycode;
            rep->modifier |= k.modifier;
            /* 주의: Ctrl 매크로 키를 누르고 있으면 다른 키도 Ctrl+키가 됩니다 */
        }
    }
}

void Keypad_Scan(void)
{
    uint8_t raw[ROW_SIZE][COL_SIZE];
    uint32_t now = HAL_GetTick();
    uint8_t changed = 0;

    dbg_scan_count++;
    read_matrix(raw);

    /* 키별 디바운스: raw가 DEBOUNCE_MS 동안 계속 바뀐 상태로 유지되면 확정 */
    for (uint8_t r = 0; r < ROW_SIZE; r++) {
        for (uint8_t c = 0; c < COL_SIZE; c++) {
            if (raw[r][c] == key_stable[r][c]) {
                if (key_pending[r][c])
                    dbg_chatter[r][c]++;               /* 튀었다가 원래대로 돌아감 = 채터링 */
                key_pending[r][c] = 0;
                continue;
            }
            if (!key_pending[r][c]) {
                key_pending[r][c] = 1;
                key_since[r][c] = now;
            } else if (now - key_since[r][c] >= DEBOUNCE_MS) {
                key_stable[r][c] = raw[r][c];
                key_pending[r][c] = 0;
                if (raw[r][c])
                    dbg_press[r][c]++;
                changed = 1;
            }
        }
    }

    if (changed) {
        HID_KeyboardReport_TypeDef rep;

        build_report(&rep);
        if (memcmp(&rep, &last_sent, sizeof(rep)) != 0) {
            next_report = rep;
            tx_pending = 1;
        }
    }

    /* 전송: IDLE일 때만. BUSY면 다음 스캔에서 최신 next_report로 재시도 */
    if (tx_pending) {
        if (hid_is_idle()) {
            tx_buf = next_report;
            USBD_HID_SendReport(&hUsbDeviceFS, (uint8_t *)&tx_buf, sizeof(tx_buf));
            last_sent = tx_buf;
            tx_pending = 0;
            dbg_tx_ok++;
        } else {
            dbg_tx_retry++;
        }
    }
}

void Keypad_Test(void)
{
    static HID_KeyboardReport_TypeDef rep;             /* 여기도 정적 버퍼 */

    memset(&rep, 0, sizeof(rep));
    rep.modifier    = keypadMap[1][1].modifier;
    rep.keycodes[0] = keypadMap[1][1].keycode;
    USBD_HID_SendReport(&hUsbDeviceFS, (uint8_t *)&rep, sizeof(rep));
}

/*
 * keypad_config.h
 *
 *  Created on: Nov 16, 2025
 *      Author: Mario76
 */

#ifndef INC_KEYPAD_H_
#define INC_KEYPAD_H_

#define _KEYPAD_DEBOUNCE_TIME_MS 20
#define COL_SIZE 4
#define ROW_SIZE 6
#define MAX_KEYS 6

#include "usb_device.h"
#include "usbd_hid.h"

typedef struct {
    uint8_t modifier;
    uint8_t reserved;
    uint8_t keycodes[MAX_KEYS];
} HID_KeyboardReport_TypeDef;

typedef struct
{
	uint8_t modifier;
	uint8_t keycode;
} HID_Key;

extern USBD_HandleTypeDef hUsbDeviceFS;

void Keypad_Scan(void);

void Keypad_Test(void);

#endif /* INC_KEYPAD_H_ */

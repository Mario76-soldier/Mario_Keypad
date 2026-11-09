/*
 * ws2812.h
 *
 *  Created on: Nov 27, 2025
 *      Author: Mario76
 */

#ifndef INC_WS2812_H_
#define INC_WS2812_H_

#include "stm32f1xx_hal.h"
#include <string.h>
#include <stdlib.h>

#define LED_DATA_ADDR   0x0800F800   // 2KB 영역 시작 주소
#define PAGE_SIZE       1024
#define PAGE_COUNT      2
#define WS2812_MAGIC 	0xA83F328C

#define LED 10
#define MIN(X,Y) ((X) < (Y) ? (X) : (Y))
#define WS2812_LEN LED*24+48

typedef struct {
	TIM_HandleTypeDef *htim;
	DMA_HandleTypeDef *hdma;
}ws2812_init_t;

typedef struct{
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint16_t led_no;
}ws2812_send_t;


void HAL_TIM_MspPostInit(TIM_HandleTypeDef *htim);

void ws2812_convert_color(ws2812_send_t color);

void ws2812_change(void);

void ws2812_start(ws2812_init_t *ws);

void ws2812_solid_mode(ws2812_send_t color);

#endif /* INC_WS2812_H_ */

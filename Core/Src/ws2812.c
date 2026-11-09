/*
 * ws2812.c
 *
 *  Created on: Nov 27, 2025
 *      Author: Mario76
 */

#include "ws2812.h"

uint8_t buf[WS2812_LEN]={0,};

ws2812_init_t ws2812_init_v={};

static void encode_byte(uint8_t value, uint8_t *out)
{
    for (int i = 0; i < 8; i++) {
        out[i] = (value & (1 << (7 - i))) ? 57 : 28;
    }
}


void ws2812_convert_color(ws2812_send_t color){ // GRB
	uint8_t red = MIN(100, color.red);
	uint8_t green = MIN(100, color.green);
	uint8_t blue = MIN(100, color.blue);

	encode_byte(green, &buf[color.led_no*24 + 0]);
	encode_byte(red, &buf[color.led_no*24 + 8]);
	encode_byte(blue, &buf[color.led_no*24 + 16]);
}

void ws2812_flash_save_led_state(uint8_t *buf, uint32_t len)
{
    HAL_FLASH_Unlock();

    // Page erase
    FLASH_EraseInitTypeDef erase = {
        .TypeErase = FLASH_TYPEERASE_PAGES,
        .PageAddress = LED_DATA_ADDR,
        .NbPages = PAGE_COUNT
    };
    uint32_t err;
    HAL_FLASHEx_Erase(&erase, &err);

    // Write magic + length
    uint32_t magic = WS2812_MAGIC;
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, LED_DATA_ADDR, magic);
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, LED_DATA_ADDR + 4, len);

    // Write payload
    uint32_t write_addr = LED_DATA_ADDR + 8;
    for (uint32_t i = 0; i < len; i += 4)
    {
        uint32_t w = 0xFFFFFFFF;
        memcpy(&w, &buf[i], MIN(4, len - i));
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, write_addr, w);
        write_addr += 4;
    }

    HAL_FLASH_Lock();
}

int ws2812_flash_load_led_state(uint8_t *buf, uint32_t max_len)
{
    uint32_t magic = *(uint32_t*)LED_DATA_ADDR;
    if (magic != WS2812_MAGIC)
        return -1; // no data

    uint32_t len = *(uint32_t*)(LED_DATA_ADDR + 4);
    if (len > max_len)
        return -2; // buffer too small

    uint8_t *p = (uint8_t*)(LED_DATA_ADDR + 8);
    memcpy(buf, p, len);

    return len;
}

void ws2812_change(void){
	HAL_TIM_PWM_Stop_DMA(ws2812_init_v.htim, TIM_CHANNEL_1);
	HAL_TIM_PWM_Start_DMA(ws2812_init_v.htim, TIM_CHANNEL_1, (uint32_t*)buf, WS2812_LEN);
	while(ws2812_init_v.hdma->Instance->CNDTR > 0);
	HAL_Delay(100);
}

void ws2812_change_save(void){
	ws2812_flash_save_led_state(buf, WS2812_LEN);
	ws2812_change();
}

void ws2812_start(ws2812_init_t *ws){
	ws2812_init_v.htim=ws->htim;
	ws2812_init_v.hdma=ws->hdma;

	if(ws2812_flash_load_led_state(buf, WS2812_LEN)<0){
		for(int i=0; i<LED; i++){
			for(int j=0; j<24; j++){
				buf[i*24+j]=28;
			}
		}
	}
	ws2812_change();
}

void ws2812_solid_mode(ws2812_send_t color){
	int i;
	uint8_t red = MIN(100, color.red);
	uint8_t green = MIN(100, color.green);
	uint8_t blue = MIN(100, color.blue);
	for (i = 0; i < LED; i++) {
		encode_byte(green, &buf[i*24 + 0]);
		encode_byte(red, &buf[i*24 + 8]);
		encode_byte(blue, &buf[i*24 + 16]);
	}
	ws2812_change_save();
}

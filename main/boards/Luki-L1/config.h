#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>


// #define TOUCH_EN
// #define LED_EN


// #define AUDIO_INPUT_REFERENCE true
#define AUDIO_INPUT_SAMPLE_RATE  24000
#define AUDIO_OUTPUT_SAMPLE_RATE 24000

#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_44
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_42
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_43
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_11
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_10
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_9

#define AUDIO_CODEC_PA_PIN       GPIO_NUM_12
#define AUDIO_CODEC_PA_OUTPUT_INVERT false

//led power
#define BUILTIN_LED_POWER GPIO_NUM_8 // 高电平有效
#define BUILTIN_LED_POWER_OUTPUT_INVERT false

#define BUILTIN_LED_GPIO        GPIO_NUM_7

//button 
#define LEFT_BUTTON_GPIO   GPIO_NUM_21
#define RIGHT_BUTTON_GPIO GPIO_NUM_47

#define LEFT_BUTTON_CTRL_GPIO GPIO_NUM_48
#define LEFT_BUTTON_CTRL_OUTPUT_INVERT false
//touch

#ifdef TOUCH_EN

#define TOUCH_BUTTON_GPIO   GPIO_NUM_1
#define TOUCH_BUTTON_ACTIVE_LEVEL  1 // v1.3
// #define TOUCH_BUTTON_ACTIVE_LEVEL  0 //  <v1.3

#endif

//vib
#define VIBRATOR_GPIO   GPIO_NUM_18
 


//display
#define DISPLAY_SDA_PIN GPIO_NUM_4
#define DISPLAY_SCL_PIN GPIO_NUM_3
#define DISPLAY_DC_PIN GPIO_NUM_5
#define DISPLAY_CS_PIN GPIO_NUM_2
#define DISPLAY_RST_PIN GPIO_NUM_6


// #define  CONFIG_LCD_GC9D01
#define CONFIG_LCD_NV3007

#ifdef CONFIG_LCD_GC9D01

#define DISPLAY_WIDTH   160
#define DISPLAY_HEIGHT  160
#define DISPLAY_SWAP_XY  true
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
 
#define DISPLAY_OFFSET_X  0x00
#define DISPLAY_OFFSET_Y  0x0f+0

#elif defined(CONFIG_LCD_NV3007)

#define DISPLAY_WIDTH   428
#define DISPLAY_HEIGHT  142

//正常视角
#define DISPLAY_SWAP_XY  true
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_OFFSET_X  0x00
#define DISPLAY_OFFSET_Y  0x0e

//旋转180°
// #define DISPLAY_SWAP_XY  true
// #define DISPLAY_MIRROR_X false
// #define DISPLAY_MIRROR_Y true
// #define DISPLAY_OFFSET_X  0x00
// #define DISPLAY_OFFSET_Y  0x0c



#else
#error "Please select a valid LCD configuration (CONFIG_LCD_GC9D01 or CONFIG_LCD_NV3007)"  // 增强健壮性：未配置时报错

#endif



#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_15
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT true

//4g
#define ML307_RX_PIN GPIO_NUM_13
#define ML307_TX_PIN GPIO_NUM_14

#define ML307_POWER_PIN GPIO_NUM_39 // 高电平有效
#define ML307_POWER_OUTPUT_INVERT false


//power
#define AXP2101_I2C_ADDR 0x34

 
//i2c
#define I2C_SDA_PIN  GPIO_NUM_16
#define I2C_SCL_PIN  GPIO_NUM_17


#endif // _BOARD_CONFIG_H_

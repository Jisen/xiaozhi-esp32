#include "ml307_board.h"
#include "codecs/no_audio_codec.h"
// #include "display/lcd_display.h"
#include "my_display.h"

#include "application.h"
#include "button.h"
#include "led/single_led.h"
// #include "iot/thing_manager.h"
#include "config.h"
#include "power_save_timer.h"
#include "axp2101.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#ifdef CONFIG_LCD_GC9D01
#include "esp_lcd_gc9d01n.h"
#elif defined(CONFIG_LCD_NV3007)
#include "esp_lcd_nv3007.h"
#endif


 

#define TAG "Luki_L1"
#ifdef TOUCH_EN
#include "touch_element/touch_button.h"
#define CONFIG_TOUCH_ELEM_CALLBACK
#define TOUCH_BUTTON_NUM     1

#endif




LV_FONT_DECLARE(font_puhui_16_4);
LV_FONT_DECLARE(font_awesome_16_4);
LV_IMG_DECLARE(normal_optimize); 


// LV_IMG_DECLARE(icons8_boring);
 
class customDisplay : public CustomEmojiDisplay {
public:
lv_obj_t* emoj_gif = nullptr;
customDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy)
        : CustomEmojiDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y, swap_xy, 
                    {
                        .text_font = &font_puhui_16_4,
                        .icon_font = &font_awesome_16_4,
                        .emoji_font = font_emoji_32_init(),
                    }) {


        DisplayLockGuard lock(this); 

        // SetupUI();
        
    }

     
}; 
class Pmic : public Axp2101 {
public:
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {

        ESP_LOGI(TAG, "ReadReg(0x22):%02X",ReadReg(0x22));
        // ** EFUSE defaults **
        WriteReg(0x22, 0b110); // PWRON > OFFLEVEL as POWEROFF Source enable
        ESP_LOGI(TAG, "ReadReg(0x22):%02X",ReadReg(0x22));

        // WriteReg(0x27, 0x10);  // hold 4s to power off
        WriteReg(0x27, 0x13);  // hold 4s to power off,开机2S

     

    
        WriteReg(0x93, 0x1C); // 配置 aldo2 输出为 3.3V
    
        uint8_t value = ReadReg(0x90); // XPOWERS_AXP2101_LDO_ONOFF_CTRL0
        value = value | 0x02; // set bit 1 (ALDO2)
        WriteReg(0x90, value);  // and power channels now enabled
        // WriteReg(0x90, 0);  // close ldo

    
        WriteReg(0x64, 0x03); // CV charger voltage setting to 4.2V
        
        WriteReg(0x61, 0x05); // set Main battery precharge current to 125mA
        WriteReg(0x62, 0x0A); // set Main battery charger current to 400mA ( 0x08-200mA, 0x09-300mA, 0x0A-400mA )
        WriteReg(0x63, 0x15); // set Main battery term charge current to 125mA
    
        WriteReg(0x14, 0x00); // set minimum system voltage to 4.1V (default 4.7V), for poor USB cables
        WriteReg(0x15, 0x00); // set input voltage limit to 3.88v, for poor USB cables
        WriteReg(0x16, 0x05); // set input current limit to 2000mA
    
        WriteReg(0x24, 0x01); // set Vsys for PWROFF threshold to 3.2V (default - 2.6V and kill battery)
        WriteReg(0x50, 0x14); // set TS pin to EXTERNAL input (not temperature)
        // WriteReg(0x50, 0x10); // 完全关闭温度检测
        
    }

    void closeAldo2() {
        WriteReg(0x90, 0);  // close ldo
    }
};


class CustomAudioCodec : public NoAudioCodecSimplex {

    public:
    CustomAudioCodec(int input_sample_rate, int output_sample_rate, gpio_num_t spk_bclk, gpio_num_t spk_ws, gpio_num_t spk_dout, gpio_num_t mic_sck, gpio_num_t mic_ws, gpio_num_t mic_din)
    :NoAudioCodecSimplex(input_sample_rate,  output_sample_rate,  spk_bclk,  spk_ws,  spk_dout,  mic_sck,  mic_ws,  mic_din){}

    virtual ~CustomAudioCodec(){};

    
    virtual void EnableInput(bool enable) override{
        NoAudioCodecSimplex::EnableInput(enable);
    }
    virtual void EnableOutput(bool enable) override{
        if (enable){
            gpio_set_level(gpio_num_t(AUDIO_CODEC_PA_PIN), 1);
        }else{
            gpio_set_level(gpio_num_t(AUDIO_CODEC_PA_PIN), 0);
        }
        NoAudioCodecSimplex::EnableOutput(enable);
    } 
};
 
 
    
class Luki_L1 : public Ml307Board {
private:
    i2c_master_bus_handle_t codec_i2c_bus_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    customDisplay* display_;

    // anim::EmojiWidget* display_ = nullptr;
    Pmic* pmic_ = nullptr;
    Button left_button_;
    Button right_button_;
    
    // 添加按钮长按状态跟踪
    bool left_button_long_pressed_ = false;
    bool right_button_long_pressed_ = false;
    bool left_button_pressed_ = false;  // 添加左键按下状态跟踪

    // 添加触摸按钮长按状态跟踪
    bool touch_button_long_pressed_ = false;

    PowerSaveTimer* power_save_timer_;

    void InitializePowerSaveTimer() {
        power_save_timer_ = new PowerSaveTimer(-1, 60, -1);
        power_save_timer_->OnEnterSleepMode([this]() {
            ESP_LOGI(TAG, "Enabling sleep mode");
            // if (!modem_.Command("AT+MLPMCFG=\"sleepmode\",2,0")) {
            //     ESP_LOGE(TAG, "Failed to enable module sleep mode");
            // }
            auto display = GetDisplay();
            display->SetChatMessage("system", "");
            display->SetEmotion("sleepy");
            GetBacklight()->SetBrightness(1);            
             
        });
        power_save_timer_->OnExitSleepMode([this]() {                       
            auto display = GetDisplay();
            display->SetChatMessage("system", "");
            display->SetEmotion("neutral");
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            ESP_LOGI(TAG, "Shutting down");
            pmic_->closeAldo2();
            Disable4GModule();
            DisableLeftButton();
            // vTaskDelay(pdMS_TO_TICKS(500));

            pmic_->PowerOff();
            vTaskDelay(pdMS_TO_TICKS(500));
        });

        power_save_timer_->SetEnabled(true);
    }

    #ifdef LED_EN
    void InitializeLedPower() {
        // 设置GPIO模式
        gpio_reset_pin(BUILTIN_LED_POWER);
        gpio_set_direction(BUILTIN_LED_POWER, GPIO_MODE_OUTPUT);
        gpio_set_level(BUILTIN_LED_POWER, BUILTIN_LED_POWER_OUTPUT_INVERT ? 0 : 1);
    }
    #endif

    void Enable4GModule() {
       // 设置GPIO模式
       gpio_reset_pin(ML307_POWER_PIN);
       gpio_set_direction(ML307_POWER_PIN, GPIO_MODE_OUTPUT);
       gpio_set_level(ML307_POWER_PIN, ML307_POWER_OUTPUT_INVERT ? 0 : 1);
    }

    void Disable4GModule() {
        // 设置GPIO模式       
        gpio_reset_pin(ML307_POWER_PIN);
        gpio_set_direction(ML307_POWER_PIN, GPIO_MODE_OUTPUT);
        gpio_set_level(ML307_POWER_PIN, ML307_POWER_OUTPUT_INVERT ? 1 : 0);
     }

    void InitVibrator() {
    // 设置GPIO模式，关闭马达
       gpio_reset_pin(VIBRATOR_GPIO);
       gpio_set_level(VIBRATOR_GPIO, 0);
       gpio_set_direction(VIBRATOR_GPIO, GPIO_MODE_OUTPUT);
       
       gpio_set_level(VIBRATOR_GPIO, 1);
       vTaskDelay(pdMS_TO_TICKS(100));
       gpio_set_level(VIBRATOR_GPIO, 0);

    }
    #ifdef TOUCH_EN
        /* Touch buttons handle */
     touch_button_handle_t button_handle[TOUCH_BUTTON_NUM];

    /* Touch buttons channel array */
     const touch_pad_t channel_array[TOUCH_BUTTON_NUM] = {
        TOUCH_PAD_NUM1,
    };

    /* Touch buttons channel sensitivity array */
     const float channel_sens_array[TOUCH_BUTTON_NUM] = {
        0.07F,
    };

 

    /* Button callback routine */
    static void button_handler(touch_button_handle_t out_handle, touch_button_message_t *out_message, void *arg)
    {
        (void) out_handle; //Unused
        
        Luki_L1* instance = static_cast<Luki_L1*>(arg);

        // if (out_message->event == TOUCH_BUTTON_EVT_ON_PRESS) {
        //     ESP_LOGI(TAG, "Button[%d] Press", (int)arg);
        //     instance->power_save_timer_->WakeUp();
        //     // instance->display_->ShowNotification(">(_︶_)<");
        // } else 
        if (out_message->event == TOUCH_BUTTON_EVT_ON_RELEASE) {
            ESP_LOGI(TAG, "Button[%d] Release", (int)arg);
            if(instance->touch_button_long_pressed_){
                instance->touch_button_long_pressed_ = false;
                instance->display_->SetEmotion("neutral");
            }
        } else if (out_message->event == TOUCH_BUTTON_EVT_ON_LONGPRESS) {
            ESP_LOGI(TAG, "Button[%d] LongPress", (int)arg);
            instance->touch_button_long_pressed_ = true;
            instance->power_save_timer_->WakeUp();
            instance->display_->SetEmotion("laughing");
            gpio_set_level(VIBRATOR_GPIO, 1);    
            vTaskDelay(pdMS_TO_TICKS(300));     // 震动100ms
            gpio_set_level(VIBRATOR_GPIO, 0);   // 停止震动        
        }
    }

    void InitTouch(void)
    {

        // 设置GPIO模式，关闭马达
        gpio_reset_pin(TOUCH_BUTTON_GPIO);
        gpio_set_level(TOUCH_BUTTON_GPIO, 0);
        gpio_set_direction(TOUCH_BUTTON_GPIO, GPIO_MODE_OUTPUT);
        vTaskDelay(pdMS_TO_TICKS(500));
        /* Initialize Touch Element library */
        touch_elem_global_config_t global_config = TOUCH_ELEM_GLOBAL_DEFAULT_CONFIG();
        ESP_ERROR_CHECK(touch_element_install(&global_config));
        ESP_LOGI(TAG, "Touch element library installed");

        touch_button_global_config_t button_global_config = TOUCH_BUTTON_GLOBAL_DEFAULT_CONFIG();
        ESP_ERROR_CHECK(touch_button_install(&button_global_config));
        ESP_LOGI(TAG, "Touch button installed");
        for (int i = 0; i < TOUCH_BUTTON_NUM; i++) {
            touch_button_config_t button_config = {
                .channel_num = channel_array[i],
                .channel_sens = channel_sens_array[i]
            };
            /* Create Touch buttons */
            ESP_ERROR_CHECK(touch_button_create(&button_config, &button_handle[i]));
            /* Subscribe touch button events (On Press, On Release, On LongPress) */
            ESP_ERROR_CHECK(touch_button_subscribe_event(button_handle[i],
                                                        TOUCH_ELEM_EVENT_ON_PRESS | TOUCH_ELEM_EVENT_ON_RELEASE | TOUCH_ELEM_EVENT_ON_LONGPRESS,
                                                        this));
 
            /* Set EVENT as the dispatch method */
            ESP_ERROR_CHECK(touch_button_set_dispatch_method(button_handle[i], TOUCH_ELEM_DISP_CALLBACK));
            /* Register a handler function to handle event messages */
            ESP_ERROR_CHECK(touch_button_set_callback(button_handle[i], button_handler));
 
            /* Set LongPress event trigger threshold time */
            ESP_ERROR_CHECK(touch_button_set_longpress(button_handle[i], 2000));
        }
        ESP_LOGI(TAG, "Touch buttons created");

 

        touch_element_start();
        ESP_LOGI(TAG, "Touch element library start");
    }

    #endif
 

    void EnableAudioOut() {
        // 设置GPIO模式
        gpio_reset_pin(AUDIO_CODEC_PA_PIN);
        gpio_set_direction(AUDIO_CODEC_PA_PIN, GPIO_MODE_OUTPUT);
        gpio_set_level(AUDIO_CODEC_PA_PIN, AUDIO_CODEC_PA_OUTPUT_INVERT ? 0 : 1);
     }

     void EnableLeftButton() {
        // 设置GPIO模式
        gpio_reset_pin(LEFT_BUTTON_CTRL_GPIO);
        gpio_set_direction(LEFT_BUTTON_CTRL_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level(LEFT_BUTTON_CTRL_GPIO, LEFT_BUTTON_CTRL_OUTPUT_INVERT ? 0 : 1);
     }

     void DisableLeftButton() {
        // 设置GPIO模式
        gpio_reset_pin(LEFT_BUTTON_CTRL_GPIO);
        gpio_set_direction(LEFT_BUTTON_CTRL_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level(LEFT_BUTTON_CTRL_GPIO, LEFT_BUTTON_CTRL_OUTPUT_INVERT ? 1 : 0);
     }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_SDA_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_SCL_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitGc9d01nDisplay(){
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片 
        ESP_LOGD(TAG, "Install LCD driver");        
        
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
        panel_config.bits_per_pixel = 16;

        #ifdef CONFIG_LCD_GC9D01
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9d01n(panel_io, &panel_config, &panel));
        #elif defined(CONFIG_LCD_NV3007)
        ESP_ERROR_CHECK(esp_lcd_new_panel_nv3007(panel_io, &panel_config, &panel));
        #endif
        

        esp_lcd_panel_reset(panel);
        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, false);
        #ifdef CONFIG_LCD_GC9D01
        // esp_lcd_panel_swap_xy(panel, true);
        // esp_lcd_panel_mirror(panel, true, true);
        #elif defined(CONFIG_LCD_NV3007)
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
        #endif


        esp_lcd_panel_disp_on_off(panel, true);
        display_ = new customDisplay(panel_io, panel,
            DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X,
            DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY );
        // display_ = new anim::EmojiWidget(panel, panel_io);
                         
    #ifdef CONFIG_LCD_GC9D01
    lv_disp_set_rotation(NULL, LV_DISP_ROTATION_270);
    #endif
    }

    void InitializeCodecI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)0,
            .sda_io_num = I2C_SDA_PIN,
            .scl_io_num = I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &codec_i2c_bus_));
    }

    void InitializeButtons() {
        right_button_.OnPressDown([this]() {
            power_save_timer_->WakeUp();
            // 只有在左键没有按下的情况下才开始监听
            if (!left_button_pressed_) {
                Application::GetInstance().StartListening();
            }
        });
        right_button_.OnPressUp([this]() {
            power_save_timer_->WakeUp();
            Application::GetInstance().StopListening();
            // 松开右键时重置状态
            right_button_long_pressed_ = false;
        });

        // 右键长按处理
        right_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            right_button_long_pressed_ = true;
            
            // 检查是否两个按钮都被长按
            if (left_button_long_pressed_ && right_button_long_pressed_) {
                ExecutePowerOff();
            }
        });

        // 左键按下处理
        left_button_.OnPressDown([this]() {
            power_save_timer_->WakeUp();
            left_button_pressed_ = true;  // 设置左键按下状态
        });
        
        // 左键松开处理
        left_button_.OnPressUp([this]() {
            power_save_timer_->WakeUp();
            // 松开左键时重置状态
            left_button_long_pressed_ = false;
            left_button_pressed_ = false;  // 重置左键按下状态
        });

        // 左键长按处理
        left_button_.OnLongPress([this]() {
            power_save_timer_->WakeUp();
            left_button_long_pressed_ = true;
            
            // 检查是否两个按钮都被长按
            if (left_button_long_pressed_ && right_button_long_pressed_) {
                ExecutePowerOff();
            }
        });
    }

    // 提取关机逻辑到单独函数
    void ExecutePowerOff() {
        ESP_LOGI(TAG, "PowerOff - Both buttons long pressed");
        auto display = GetDisplay();
        display->SetEmotion("sleepy");
        gpio_set_level(VIBRATOR_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(VIBRATOR_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(1000));

        pmic_->closeAldo2();
        DisableLeftButton();
        pmic_->PowerOff();
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeIot() {
        #if CONFIG_IOT_PROTOCOL_XIAOZHI
        auto& thing_manager = iot::ThingManager::GetInstance();
        thing_manager.AddThing(iot::CreateThing("Speaker"));
        thing_manager.AddThing(iot::CreateThing("Battery"));
        thing_manager.AddThing(iot::CreateThing("Screen"));
    #elif CONFIG_IOT_PROTOCOL_MCP
         
    #endif
    }

public:
    Luki_L1() : Ml307Board(ML307_TX_PIN, ML307_RX_PIN),   

        left_button_(LEFT_BUTTON_GPIO,false, 3000),
        right_button_(RIGHT_BUTTON_GPIO,false, 3000) {
        InitializePowerSaveTimer();
        InitializeCodecI2c();
        pmic_ = new Pmic(codec_i2c_bus_, AXP2101_I2C_ADDR);  

        
        Enable4GModule();
        InitializeSpi();
        InitGc9d01nDisplay();
        InitVibrator();
        EnableAudioOut();
#ifdef  LED_EN
        InitializeLedPower();
#endif

        // if(gpio_get_level(RIGHT_BUTTON_GPIO)!=0)
        // {
        //    pmic_->closeAldo2();
        //    DisableLeftButton();
        //    pmic_->PowerOff();
        //    vTaskDelay(pdMS_TO_TICKS(3000 ));
        // }

        InitializeButtons();
        EnableLeftButton() ;
        
        InitializeIot();
        GetBacklight()->RestoreBrightness();
    #ifdef TOUCH_EN
        InitTouch();
    #endif

    }
#ifdef LED_EN
    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }
    #endif

    virtual AudioCodec* GetAudioCodec() override {
        // static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
        static CustomAudioCodec audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,

            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
        return &backlight;
    }

    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        static bool last_discharging = false;
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        if (discharging != last_discharging) {
            power_save_timer_->SetEnabled(discharging);
            last_discharging = discharging;
        }

        level = pmic_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveMode(bool enabled) override {
        if (!enabled) {
            power_save_timer_->WakeUp();
        }
        Ml307Board::SetPowerSaveMode(enabled);
    }
};

DECLARE_BOARD(Luki_L1);
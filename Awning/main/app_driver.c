#include <driver/ledc.h>
#include <esp_log.h>

#include "app_driver.h"

//Servo PWM pin
#define OUTPUT_GPIO 4

//Define PWM parameters and servo constants for PWM calculation
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_RESOLUTION     LEDC_TIMER_13_BIT  // 8192 steps
#define SERVO_MIN_PULSE_US  500  // 0º position
#define SERVO_MAX_PULSE_US  2500 // 180º position
#define SERVO_FREQ_HZ       50   // Servo PWM frequency

void app_driver_init()
{
    //Configure timer
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz = SERVO_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    //Configure channel
    ledc_channel_config_t channel_conf = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = OUTPUT_GPIO,
        .duty = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel_conf));
}

void app_driver_set_state(bool state)
{
    //Set servo angle based on desired state (180º for on, 0º for off)
    int angle;
    if (state) {
        angle = 180;
    } else {
        angle = 0;
    }

    //Calculate PWM duty cycle and apply to pin
    uint32_t pulse_width_us = SERVO_MIN_PULSE_US + (angle * (SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US)/180);
    uint32_t duty = (pulse_width_us * (1 << LEDC_RESOLUTION)) / (1000000 / SERVO_FREQ_HZ); 
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
}

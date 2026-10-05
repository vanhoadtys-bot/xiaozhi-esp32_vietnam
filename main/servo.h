#pragma once
#include <driver/ledc.h>

// Định nghĩa 4 chân xuất lệnh cho Servo trên mạch tím XH-S3E-AI_V1.0
#define SERVO_FL_GPIO  39  // Chân Trước - Bên Trái (Lỗ chân 039/Vol- cũ)
#define SERVO_FR_GPIO  40  // Chân Trước - Bên Phải (Lỗ chân 040/Vol+ cũ)
#define SERVO_BL_GPIO  43  // Chân Sau - Bên Trái   (Lỗ chân 043/Tx cũ)
#define SERVO_BR_GPIO  44  // Chân Sau - Bên Phải   (Lỗ chân 044/Rx cũ)

#define SERVO_FL_CH    LEDC_CHANNEL_0
#define SERVO_FR_CH    LEDC_CHANNEL_1
#define SERVO_BL_CH    LEDC_CHANNEL_2
#define SERVO_BR_CH    LEDC_CHANNEL_3

#define SERVO_MIN_US   500
#define SERVO_MAX_US   2500
#define SERVO_FREQ_HZ  50
#define SERVO_NEUTRAL  90

void servo_init(void);
void servo_set_angle(ledc_channel_t channel, int degrees);
void servo_all_neutral(void);

// Danh sách các hàm hành động của chó robot
void anim_good_boy(void);
void anim_sit_down(void);
void anim_lie_down(void);
void anim_stretch(void);
void anim_walk(void);
void anim_dance(void);

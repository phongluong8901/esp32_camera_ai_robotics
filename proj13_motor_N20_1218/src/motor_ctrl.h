#pragma once

#define MOTOR_STBY_GPIO 17

#define MOTOR_A_PWM 4
#define MOTOR_A_IN1 5
#define MOTOR_A_IN2 6

#define MOTOR_B_PWM 7
#define MOTOR_B_IN1 15
#define MOTOR_B_IN2 16

void motor_ctrl_init(void);
// speed: -100 (lui het ga) -> 0 (dung) -> 100 (tien het ga)
void motor_ctrl_set(int left_speed, int right_speed);
void motor_ctrl_stop(void);
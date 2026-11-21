#pragma once

#define SERVO1_GPIO 15
#define SERVO2_GPIO 16

void servo_ctrl_init(void);
void servo_ctrl_set_angle(int servo_id, int angle);
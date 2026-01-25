#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void trigger_alarm_actions(uint8_t r, uint8_t g, uint8_t b, int duration_ms);
void sensor_task(void *arg);
void display_task(void *arg);
void logic_set_sensor_task_handle(TaskHandle_t h);
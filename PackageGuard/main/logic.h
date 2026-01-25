#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void sensor_task(void *arg);
void display_task(void *arg);
void logic_set_sensor_task_handle(TaskHandle_t h);
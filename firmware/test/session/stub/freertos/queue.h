#pragma once
// A queue of pointers between the harness's threads (harness.c).
#include "freertos/FreeRTOS.h"
typedef struct queue *QueueHandle_t;
QueueHandle_t xQueueCreate(int len, int item_size);
BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks);
BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks);

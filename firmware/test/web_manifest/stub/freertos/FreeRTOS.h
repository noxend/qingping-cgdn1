#pragma once
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(m) ((void) (m))
#define taskEXIT_CRITICAL(m) ((void) (m))

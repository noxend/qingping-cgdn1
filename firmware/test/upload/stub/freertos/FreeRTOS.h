// Host stand-in: one thread, so a critical section has nothing to hold off.
#pragma once
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(mux) ((void) (mux))
#define taskEXIT_CRITICAL(mux) ((void) (mux))

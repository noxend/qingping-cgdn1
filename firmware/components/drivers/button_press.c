#include "button_press.h"

static button_event_t on_released(button_press_t *b, uint32_t now_ms) {
  if (!b->have_up) {
    b->have_up = true;
    b->up_since = now_ms;
  }
  uint32_t up_ms = now_ms - b->up_since;
  if (!b->armed) {
    b->armed = up_ms >= BUTTON_ARM_MS;
    return BUTTON_NONE;
  }
  if (!b->down || up_ms < BUTTON_RELEASE_MS) {
    return BUTTON_NONE;
  }
  b->down = false;
  return b->hold_sent ? BUTTON_NONE : BUTTON_CLICK;
}

button_event_t button_press_step(button_press_t *b, bool pressed, uint32_t now_ms) {
  if (!pressed) {
    return on_released(b, now_ms);
  }
  b->have_up = false;
  if (!b->armed) {
    return BUTTON_NONE;
  }
  if (!b->down) {
    b->down = true;
    b->hold_sent = false;
    b->long_sent = false;
    b->down_at = now_ms;
    return BUTTON_NONE;
  }
  uint32_t held_ms = now_ms - b->down_at;
  if (!b->hold_sent && held_ms >= BUTTON_HOLD_MS) {
    b->hold_sent = true;
    return BUTTON_HOLD;
  }
  if (!b->long_sent && held_ms >= BUTTON_LONG_MS) {
    b->long_sent = true;
    return BUTTON_LONG;
  }
  return BUTTON_NONE;
}

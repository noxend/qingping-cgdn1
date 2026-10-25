#include "battery.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#define CELL_CHANNEL ADC_CHANNEL_3  // GPIO39
#define USB_GPIO GPIO_NUM_34
#define CHARGE_GPIO GPIO_NUM_35
#define SAMPLES 5

static const char *TAG = "battery";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static battery_t s_last;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// The stock firmware's discharge table at 0x3f409826: entry i is the cell voltage
// at 100 - i percent.
static const uint16_t PERCENT_MV[101] = {
    4200, 4150, 4140, 4102, 4080, 4062, 4050, 4042, 4034, 4028, 4023, 4018, 4014, 4009, 4004,
    3998, 3991, 3982, 3973, 3963, 3951, 3940, 3929, 3916, 3914, 3907, 3892, 3882, 3872, 3861,
    3852, 3844, 3834, 3826, 3817, 3808, 3799, 3790, 3780, 3771, 3761, 3751, 3741, 3731, 3721,
    3710, 3700, 3689, 3679, 3669, 3659, 3649, 3639, 3630, 3622, 3613, 3605, 3598, 3590, 3583,
    3577, 3570, 3565, 3566, 3556, 3550, 3544, 3537, 3531, 3526, 3519, 3513, 3508, 3502, 3495,
    3489, 3482, 3475, 3468, 3460, 3452, 3444, 3434, 3425, 3415, 3403, 3392, 3380, 3366, 3353,
    3342, 3330, 3319, 3308, 3295, 3281, 3266, 3243, 3213, 3100, 2900,
};

static int percent_of(int mv) {
  if (mv == 0) {
    return -1;
  }
  for (int i = 0; i < 101; i++) {
    if (mv >= PERCENT_MV[i]) {
      return 100 - i;
    }
  }
  return 0;
}

void battery_init(void) {
  gpio_config_t io = {
      .pin_bit_mask = (1ULL << USB_GPIO) | (1ULL << CHARGE_GPIO),
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,  // 34-39 have no pull
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io);

  adc_oneshot_unit_init_cfg_t unit = {
      .unit_id = ADC_UNIT_1,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
  };
  // The cell's half is at most 2.1 V, inside the 12 dB range.
  adc_oneshot_chan_cfg_t chan = {
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
  };
  adc_cali_line_fitting_config_t cali = {
      .unit_id = ADC_UNIT_1,
      .atten = ADC_ATTEN_DB_12,
      .bitwidth = ADC_BITWIDTH_12,
      .default_vref = 1100,  // used only on a chip without the eFuse calibration
  };
  esp_err_t e = adc_oneshot_new_unit(&unit, &s_adc);
  if (e == ESP_OK) {
    e = adc_oneshot_config_channel(s_adc, CELL_CHANNEL, &chan);
  }
  if (e == ESP_OK) {
    e = adc_cali_create_scheme_line_fitting(&cali, &s_cali);
  }
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "ADC not available: %s", esp_err_to_name(e));
    s_cali = NULL;
  }
}

static int cell_mv(void) {
  if (s_cali == NULL) {
    return 0;
  }
  int sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    int mv = 0;
    if (adc_oneshot_get_calibrated_result(s_adc, s_cali, CELL_CHANNEL, &mv) != ESP_OK) {
      return 0;
    }
    sum += mv;
    vTaskDelay(1);  // spread over a Wi-Fi burst, as the stock does
  }
  return sum / SAMPLES * 2;
}

battery_t battery_read(void) {
  battery_t b = {
      .mv = cell_mv(),
      .usb = gpio_get_level(USB_GPIO) == 0,
  };
  b.charging = b.usb && gpio_get_level(CHARGE_GPIO) == 0;
  b.percent = percent_of(b.mv);
  portENTER_CRITICAL(&s_lock);
  s_last = b;
  portEXIT_CRITICAL(&s_lock);
  return b;
}

battery_t battery_last(void) {
  portENTER_CRITICAL(&s_lock);
  battery_t b = s_last;
  portEXIT_CRITICAL(&s_lock);
  return b;
}

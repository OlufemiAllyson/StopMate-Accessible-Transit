// ESP32 coefficient adapter equivalent to ESP-SR model_path.c.
#include "hilexin_wn5.h"
#include <string.h>
model_coeff_getter_t *srmodel_get_model_coeff(char *name) {
  if (!name || strcmp(name, "wn5_hilexin")) return NULL;
  return (model_coeff_getter_t *)&get_coeff_hilexin_wn5;
}

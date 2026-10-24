# ESP32 WakeNet dependency

Selected original ESP32 headers and prebuilt libraries from Espressif ESP-SR
v2.0.0, commit `1c0503e7e2dc5e1ecb2fd3da0a5d7e51e3574842`:
https://github.com/espressif/esp-sr/tree/1c0503e7e2dc5e1ecb2fd3da0a5d7e51e3574842

Includes WakeNet5 and the built-in `wn5_hilexin` (Hi Lexin) coefficients.
The small coefficient adapter replaces the unused filesystem/model-selection
parts of upstream model_path.c. Models are linked into application flash.
See LICENSE for the upstream Espressif MIT license.

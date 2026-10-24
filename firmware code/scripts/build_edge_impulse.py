"""Build the user's unmodified C++ export as part of the PlatformIO firmware."""
from pathlib import Path
Import('env')
root = Path(env.subst('$PROJECT_DIR')) / 'eeellls-project-1-cpp-mcu-v2-impulse-#5'
if not (root / 'model-parameters/model_metadata.h').is_file():
    raise RuntimeError(f'Missing Edge Impulse export: {root}')
env.Append(CPPPATH=[str(root)], CPPDEFINES=[
    ('EI_PORTING_ESPRESSIF', 0), ('EI_PORTING_ARDUINO', 1),
    ('EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN', 0), ('EIDSP_USE_ESP_DSP', 1),
    ('EI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN', 0), ('EIDSP_USE_CMSIS_DSP', 0),
])
env.BuildSources('$BUILD_DIR/edge_impulse', str(root), src_filter=[
    '-<*>', '+<tflite-model/>', '+<edge-impulse-sdk/dsp/>',
    '+<edge-impulse-sdk/tensorflow/>', '+<edge-impulse-sdk/porting/arduino/>',
])

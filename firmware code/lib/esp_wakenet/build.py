Import("env")
from pathlib import Path
root = Path(env.subst("$PROJECT_DIR")) / "lib" / "esp_wakenet"
env.Append(LIBPATH=[str(root / "prebuilt")], LIBS=["wakenet", "c_speech_features", "dl_lib", "hilexin_wn5"])

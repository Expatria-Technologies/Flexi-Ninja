import shutil
from pathlib import Path

Import("env")

def rename_after_build(target, source, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    env_name = env.subst("$PIOENV")
    names = {
        "flexi-2350_spi": "Flexi-Ninja_FlexiHAL_2350_SPI",
        "flexi-2350_w5500": "Flexi-Ninja_FlexiHAL_2350_W5500",
        "picobob-dlx": "Flexi-Ninja_PicoBOB_DLX",
    }
    new_name = names.get(env_name)
    if new_name:
        for ext in [".uf2", ".elf"]:
            src = build_dir / f"firmware{ext}"
            if src.is_file():
                dst = build_dir / f"{new_name}{ext}"
                shutil.copy2(str(src), str(dst))
                print(f"Copied {src.name} -> {dst.name}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", rename_after_build)

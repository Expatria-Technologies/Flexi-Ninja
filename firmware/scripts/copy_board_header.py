import shutil, sys
from pathlib import Path

Import("env")

env_name = env.subst("$PIOENV")

header_map = {
    "flexi-2350_w5500": "flexi_hal_2350.h",
    "flexi-2350_spi": "flexi_hal_2350.h",
    "picobob-dlx": "picobob_dlx.h",
}

header_name = header_map.get(env_name)
if not header_name:
    sys.stderr.write(f"Unknown environment '{env_name}', no board header mapping\n")
    env.Exit(-1)

FRAMEWORK_DIR = Path(env.PioPlatform().get_package_dir("framework-picosdk"))
BOARD_HEADER_SRC = Path(env.subst("$PROJECT_DIR")) / "include" / "boards" / header_name
BOARD_HEADER_DST = FRAMEWORK_DIR / "src" / "boards" / "include" / "boards" / header_name

if BOARD_HEADER_SRC.is_file():
    BOARD_HEADER_DST.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(str(BOARD_HEADER_SRC), str(BOARD_HEADER_DST))
else:
    sys.stderr.write("Board header not found: %s\n" % BOARD_HEADER_SRC)
    env.Exit(-1)

# PlatformIO builder checks env["CPPDEFINES"] (not -D build_flags) to decide
# which stdio libraries to link. Make sure PIO_STDIO_UART is visible there.
cpp_defines = env.Flatten(env.get("CPPDEFINES", []))
stdio_flags = {"PIO_STDIO_UART", "PIO_STDIO_USB"}
for flag in stdio_flags:
    if flag not in cpp_defines:
        env.Append(CPPDEFINES=[flag])

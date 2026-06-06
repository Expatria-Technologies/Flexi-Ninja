import os
import subprocess
from pathlib import Path

Import("env")

flexgpio_bin = Path(env.subst("$PROJECT_DIR")).parent / "FlexGPIO" / "build" / "FlexGPIO_ram.bin"

if not flexgpio_bin.is_file():
    print("Building FlexGPIO RAM image...")

    platform = env.PioPlatform()
    pico_sdk_dir = platform.get_package_dir("framework-picosdk")
    toolchain_dir = platform.get_package_dir("toolchain-gccarmnoneeabi")

    if not pico_sdk_dir:
        print("ERROR: framework-picosdk not found in PlatformIO packages")
        env.Exit(-1)

    if not toolchain_dir:
        print("ERROR: toolchain-gccarmnoneeabi not found in PlatformIO packages")
        env.Exit(-1)

    os.environ["PATH"] = str(Path(toolchain_dir) / "bin") + os.pathsep + os.environ.get("PATH", "")

    flexgpio_dir = flexgpio_bin.parent.parent
    build_dir = flexgpio_bin.parent
    build_dir.mkdir(parents=True, exist_ok=True)

    cmake = "cmake"

    result = subprocess.run(
        [cmake, f"-DPICO_SDK_PATH={pico_sdk_dir}", str(flexgpio_dir)],
        cwd=str(build_dir), capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"FlexGPIO CMake configure failed:\n{result.stdout}\n{result.stderr}")
        env.Exit(-1)

    result = subprocess.run(
        [cmake, "--build", ".", "--target", "FlexGPIO_ram"],
        cwd=str(build_dir), capture_output=True, text=True
    )
    if result.returncode != 0:
        print(f"FlexGPIO build failed:\n{result.stdout}\n{result.stderr}")
        env.Exit(-1)

    print(f"FlexGPIO RAM image built: {flexgpio_bin}")
else:
    print(f"FlexGPIO RAM image already exists at {flexgpio_bin}, skipping build")

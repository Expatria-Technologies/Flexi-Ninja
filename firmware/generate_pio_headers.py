import os
import subprocess
import sys

# Get the project directory
# When run as a PlatformIO extra_script, we need to handle both cases
if "__file__" in dir():
    project_dir = os.path.dirname(os.path.abspath(__file__))
else:
    # Assume we're in the project directory
    project_dir = os.getcwd()

print(f"Project directory: {project_dir}")

# Find pioasm tool
pioasm = None
pioasm_paths = [
    os.path.expanduser("~/.platformio/packages/tool-pioasm-rp2040-earlephilhower/pioasm"),
    "/home/mike/.platformio/packages/tool-pioasm-rp2040-earlephilhower/pioasm",
]

for path in pioasm_paths:
    if os.path.exists(path):
        pioasm = path
        break

if pioasm is None:
    print("ERROR: pioasm tool not found")
    sys.exit(1)

print(f"Using pioasm: {pioasm}")

# PIO files to process
pio_files = [
    ("lib/quadrature_encoder_substep/quadrature_encoder_substep.pio",
     "lib/quadrature_encoder_substep/quadrature_encoder_substep.pio.h"),
    ("pio/quadrature_encoder.pio", "pio/quadrature_encoder.pio.h"),
    ("pio/freq_generator.pio", "pio/freq_generator.pio.h"),
    ("pio/step_counter.pio", "pio/step_counter.pio.h"),
]

for pio_file, header_file in pio_files:
    pio_path = os.path.join(project_dir, pio_file)
    header_path = os.path.join(project_dir, header_file)

    if os.path.exists(pio_path):
        print(f"Generating {header_file} from {pio_file}")
        try:
            result = subprocess.run(
                [pioasm, pio_path, header_path],
                capture_output=True,
                text=True,
                check=True
            )
            if result.stdout:
                print(result.stdout)
        except subprocess.CalledProcessError as e:
            print(f"ERROR generating {header_file}: {e}")
            if e.stderr:
                print(e.stderr)
    else:
        print(f"WARNING: {pio_file} not found, skipping")

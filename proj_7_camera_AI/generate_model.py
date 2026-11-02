import os
import sys
from pathlib import Path

try:
    Import("env")
    project_dir = env["PROJECT_DIR"]
    build_dir = env.subst("$BUILD_DIR")
except Exception:
    project_dir = os.path.dirname(os.path.abspath(__file__))
    build_dir = os.path.join(project_dir, ".pio", "build", "esp32-s3-devkitc-1")

pack_script = os.path.join(
    project_dir, "managed_components", "espressif__esp-dl", "fbs_loader", "pack_espdl_models.py"
)
models_dir = os.path.join(
    project_dir, "managed_components", "espressif__human_face_detect", "models", "s3"
)

msr_model = os.path.join(models_dir, "human_face_detect_msr_s8_v1.espdl")
mnp_model = os.path.join(models_dir, "human_face_detect_mnp_s8_v1.espdl")

out_espdl_dir = os.path.join(build_dir, "espdl_models")
out_espdl = os.path.join(out_espdl_dir, "human_face_detect.espdl")
out_s = os.path.join(build_dir, "human_face_detect.espdl.S")

os.makedirs(out_espdl_dir, exist_ok=True)
os.makedirs(build_dir, exist_ok=True)

if os.path.exists(msr_model) and os.path.exists(mnp_model) and os.path.exists(pack_script):
    sys.path.insert(0, os.path.dirname(pack_script))
    from pack_espdl_models import pack_models

    print(">>> [Edge AI] Packing ESP-DL face detection models...")
    pack_models([msr_model, mnp_model], out_file=out_espdl, pack_format="PDL3")

    print(">>> [Edge AI] Generating human_face_detect.espdl.S assembly file...")
    with open(out_espdl, "rb") as f:
        data = f.read()

    data_len = len(data)
    varname = "human_face_detect_espdl"

    lines = [
        f"/* Converted from {out_espdl} */",
        ".data",
        "#if !defined (__APPLE__) && !defined (__linux__)",
        ".section .rodata.embedded",
        "#endif",
        ".balign 16",
        f"\n.global {varname}",
        f"{varname}:",
        f"\n.global _binary_{varname}_start",
        f"_binary_{varname}_start: /* for objcopy compatibility */",
    ]

    for i in range(0, data_len, 16):
        chunk = data[i : i + 16]
        hex_str = ", ".join(f"0x{b:02x}" for b in chunk)
        lines.append(f".byte {hex_str}")

    lines.append(f"\n.global _binary_{varname}_end")
    lines.append(f"_binary_{varname}_end: /* for objcopy compatibility */")
    lines.append(f"\n.global {varname}_length")
    lines.append(f"{varname}_length:")
    lines.append(f".long {data_len}\n")

    with open(out_s, "w") as f:
        f.write("\n".join(lines))

    print(f">>> [Edge AI] Successfully generated {out_s} ({data_len} bytes)")
else:
    print(f">>> [Edge AI] Warning: Models not found in {models_dir}")


import os
import sys
from pathlib import Path

def generate():
    try:
        Import("env")
        project_dir = Path(env.subst("$PROJECT_DIR"))
        build_dir = Path(env.subst("$BUILD_DIR"))
    except Exception:
        project_dir = Path(__file__).parent.resolve()
        build_dir = project_dir / ".pio" / "build" / "esp32-s3-devkitc-1"

    # Clean up corrupted CMake cache if it was misidentified as Windows host
    cmake_cache = build_dir / "CMakeCache.txt"
    cmake_files = build_dir / "CMakeFiles"
    if cmake_cache.exists():
        try:
            with open(cmake_cache, "r", encoding="utf-8", errors="ignore") as f:
                content = f.read()
            if "CMAKE_RC_COMPILER" in content or "NOTFOUND" in content:
                print("[generate_model] Cleaning up corrupted CMake cache...")
                cmake_cache.unlink()
                if cmake_files.exists():
                    import shutil
                    shutil.rmtree(cmake_files, ignore_errors=True)
        except Exception as e:
            print(f"[generate_model] Warning during cache cleanup: {e}")

    print(f"\n[generate_model] Generating human_face_detect model for {build_dir.name}...")

    fbs_loader_dir = project_dir / "managed_components" / "espressif__esp-dl" / "fbs_loader"
    if str(fbs_loader_dir) not in sys.path:
        sys.path.insert(0, str(fbs_loader_dir))

    try:
        import pack_espdl_models
    except ImportError as e:
        print(f"[generate_model] Failed to import pack_espdl_models: {e}")
        return

    models_dir = project_dir / "managed_components" / "espressif__human_face_detect" / "models" / "s3"
    model_files = [
        models_dir / "human_face_detect_msr_s8_v1.espdl",
        models_dir / "human_face_detect_mnp_s8_v1.espdl",
    ]

    for mf in model_files:
        if not mf.exists():
            print(f"[generate_model] Error: Model file not found: {mf}")
            return

    espdl_models_dir = build_dir / "espdl_models"
    espdl_models_dir.mkdir(parents=True, exist_ok=True)

    packed_model_path = espdl_models_dir / "human_face_detect.espdl"
    asm_path = build_dir / "human_face_detect.espdl.S"

    # Pack models into PDL3
    pack_espdl_models.pack_models(
        [str(f) for f in model_files],
        out_file=str(packed_model_path),
        pack_format="PDL3",
        package_version="v1.0"
    )

    # Convert packed model to assembly file (.S)
    with open(packed_model_path, "rb") as f:
        data = f.read()

    data_len = len(data)
    varname = "human_face_detect_espdl"

    with open(asm_path, "w") as f:
        f.write(f"/* Data converted from {packed_model_path.name} */\n")
        f.write(".data\n")
        f.write("#if !defined (__APPLE__) && !defined (__linux__)\n")
        f.write(".section .rodata.embedded\n")
        f.write("#endif\n")
        f.write(".balign 16\n")
        f.write(f"\n.global {varname}\n{varname}:\n")
        f.write(f"\n.global _binary_{varname}_start\n_binary_{varname}_start: /* for objcopy compatibility */\n")

        lines = []
        for i in range(0, data_len, 16):
            chunk = data[i:i + 16]
            hex_bytes = ", ".join(f"0x{b:02x}" for b in chunk)
            lines.append(f".byte {hex_bytes}\n")
        f.writelines(lines)

        f.write(f"\n.global _binary_{varname}_end\n_binary_{varname}_end: /* for objcopy compatibility */\n")
        f.write(f"\n.global {varname}_length\n{varname}_length:\n")
        f.write(f".long {data_len}\n")

    print(f"[generate_model] Successfully generated {asm_path} ({data_len} bytes)\n")

generate()


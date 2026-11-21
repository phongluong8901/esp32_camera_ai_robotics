"""Interactive CSI dataset recorder for ESP32-S3 Human Activity Recognition."""

from __future__ import annotations

import argparse
import shutil
import sys
import time
from pathlib import Path

import numpy as np

from csi_common import CSIUDPClient, DEFAULT_LABELS, UDP_PORT, WINDOW_SIZE

ACTION_GUIDES = {
    "dung_yen": "Roi khoi khu vuc cam bien hoac ngoi/dung bat dong",
    "go_phim": "Ngoi tai cho va go ban phim / di chuot binh thuong",
    "vay_tay": "Ngoi/dung mot cho va vay tay manh",
    "di_lai": "Dung day di lai binh thuong trong phong",
    "nhay": "Nhay len xuong tai cho hoac bat nhay",
}


def record_label(
    client: CSIUDPClient,
    label: str,
    duration_s: float,
    countdown_s: int,
    out_dir: Path,
) -> bool:
    guide = ACTION_GUIDES.get(label, "Thuc hien hanh dong tuong ung")
    print(f"\n=== NHAN: [{label}] ===")
    print(f"Huong dan: {guide}")
    ans = input(
        f"Nhan ENTER de bat dau dem nguoc {countdown_s}s "
        f"(hoac go 's' de bo qua, 'q' de thoat): "
    ).strip().lower()
    if ans == "q":
        return False
    if ans == "s":
        print(f"-> Bo qua nhan [{label}].")
        return True

    # Keep UDP subscription warm during countdown and discard stale pre-countdown packets
    for rem in range(countdown_s, 0, -1):
        print(f"  Chuan bi [{label}] sau {rem}s...", end="\r", flush=True)
        t_end = time.monotonic() + 1.0
        while time.monotonic() < t_end:
            client.recv_frame(timeout=0.1)
    print(f"  >>> BAT DAU [{label}] trong {duration_s:.0f}s!        ")

    amps_list: list[np.ndarray] = []
    rssi_list: list[int] = []
    ts_list: list[int] = []

    t_start = time.monotonic()
    last_print = t_start
    while True:
        elapsed = time.monotonic() - t_start
        if elapsed >= duration_s:
            break
        pkt = client.recv_frame(timeout=0.3)
        if pkt is not None:
            ts_ms, rssi, _valid, _seq, amps = pkt
            amps_list.append(amps)
            rssi_list.append(rssi)
            ts_list.append(ts_ms)

        now = time.monotonic()
        if now - last_print >= 0.5:
            pkt_cnt = len(amps_list)
            rate = pkt_cnt / max(elapsed, 0.1)
            cur_rssi = rssi_list[-1] if rssi_list else 0
            print(
                f"  [{label}] {elapsed:4.1f}/{duration_s:.0f}s | "
                f"{pkt_cnt:4d} goi ({rate:4.1f} pkt/s) | RSSI {cur_rssi} dBm   ",
                end="\r",
                flush=True,
            )
            last_print = now

    print()
    if len(amps_list) < WINDOW_SIZE + 10:
        print(
            f"[CANH BAO] Chi thu duoc {len(amps_list)} goi "
            f"(can it nhat {WINDOW_SIZE + 10}). Khong luu file!"
        )
        return True

    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = int(time.time())
    out_path = out_dir / f"{label}_{stamp}.npz"
    np.savez_compressed(
        out_path,
        amps=np.vstack(amps_list).astype(np.float32),
        rssi=np.asarray(rssi_list, dtype=np.int16),
        ts_ms=np.asarray(ts_list, dtype=np.uint32),
        label=np.asarray(label),
    )
    print(f"  Da luu {len(amps_list)} goi -> {out_path}")
    return True


def main() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    parser = argparse.ArgumentParser(description="Thu du lieu Wi-Fi CSI tu ESP32-S3")
    parser.add_argument("esp_ip", help="Dia chi IP cua ESP32-S3 (in trong Serial Monitor)")
    parser.add_argument("--port", type=int, default=UDP_PORT, help="Cong UDP ESP32 (mac dinh 5555)")
    parser.add_argument("--duration", type=float, default=35.0, help="Thoi gian thu moi nhan (giay)")
    parser.add_argument("--countdown", type=int, default=5, help="Dem nguoc chuan bi (giay)")
    parser.add_argument("--labels", nargs="+", default=DEFAULT_LABELS, help="Danh sach nhan can thu")
    parser.add_argument("--clear", action="store_true", help="Xoa dataset cu truoc khi thu")
    args = parser.parse_args()

    out_dir = Path(__file__).resolve().parent / "dataset"
    if args.clear and out_dir.exists():
        shutil.rmtree(out_dir)
        print("Da xoa thu muc dataset cu.")

    client = CSIUDPClient(args.esp_ip, args.port)
    print(f"Dang ket noi UDP toi ESP32 {args.esp_ip}:{args.port} ...")

    try:
        # Verify stream before prompting
        got_first = False
        for _ in range(10):
            if client.recv_frame(timeout=0.4) is not None:
                got_first = True
                break
        if not got_first:
            print(
                "[LOI] Khong nhan duoc goi UDP tu ESP32! Kiem tra:\n"
                "  1) Da Clean -> Build -> Upload firmware moi nhat chua?\n"
                "  2) IP ESP32 co dung khong va PC voi ESP32 co chung Wi-Fi khong?"
            )
            return

        print("Ket noi UDP thanh cong! Giu ESP32 co dinh trong suot qua trinh thu.")
        for label in args.labels:
            ok = record_label(client, label, args.duration, args.countdown, out_dir)
            if not ok:
                break

        print("\nHoan tat thu du lieu. Chay tiep: python ml/train.py")
    except KeyboardInterrupt:
        print("\nDa dung thu du lieu.")
    finally:
        client.close()


if __name__ == "__main__":
    main()


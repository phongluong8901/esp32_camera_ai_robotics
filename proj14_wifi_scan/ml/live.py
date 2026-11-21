"""Realtime Wi-Fi CSI action prediction and web dashboard feedback."""

from __future__ import annotations

import argparse
import sys
import time
from collections import deque
from pathlib import Path

import joblib
import numpy as np

from csi_common import (
    CSIUDPClient,
    UDP_PORT,
    WINDOW_SIZE,
    extract_features,
    extract_features_v2,
)


def main() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    ml_dir = Path(__file__).resolve().parent
    default_model = ml_dir / "model_v2.joblib"
    if not default_model.exists():
        default_model = ml_dir / "model.joblib"

    parser = argparse.ArgumentParser(description="Du doan hanh dong Wi-Fi CSI realtime")
    parser.add_argument("esp_ip", help="Dia chi IP cua ESP32-S3")
    parser.add_argument("--port", type=int, default=UDP_PORT)
    parser.add_argument("--model", type=Path, default=default_model)
    parser.add_argument("--interval", type=float, default=0.25, help="Chu ky du doan (giay)")
    parser.add_argument("--smooth", type=int, default=4, help="So lan du doan lam muot xac suat")
    args = parser.parse_args()

    if not args.model.exists():
        raise FileNotFoundError(
            f"Khong tim thay model tai {args.model}. Hay chay train.py truoc!"
        )

    bundle = joblib.load(args.model)
    clf = bundle["model"]
    ver = str(bundle.get("version", "v1")).lower()
    feat_fn = extract_features_v2 if ver == "v2" else extract_features
    labels = list(bundle["labels"])
    win_size = int(bundle.get("window_size", WINDOW_SIZE))
    print(
        f"Da nap model [{ver.upper()}] {bundle.get('model_name', 'Classifier')} "
        f"(Test Acc = {bundle.get('test_acc', 0.0) * 100:.1f}%, nhan = {labels})"
    )

    amps_buf: deque[np.ndarray] = deque(maxlen=win_size)
    rssi_buf: deque[int] = deque(maxlen=win_size)
    ts_buf: deque[int] = deque(maxlen=win_size)
    prob_hist: deque[np.ndarray] = deque(maxlen=max(1, args.smooth))

    client = CSIUDPClient(args.esp_ip, args.port)
    print(f"Dang nghe luong CSI tu {args.esp_ip}:{args.port} ... (Ctrl+C de dung)")

    last_pred_at = 0.0
    pps_win_start = time.monotonic()
    pps_cnt = 0
    cur_pps = 0.0

    try:
        while True:
            pkt = client.recv_frame(timeout=0.2)
            if pkt is not None:
                ts_ms, rssi, _valid, _seq, amps = pkt
                amps_buf.append(amps)
                rssi_buf.append(rssi)
                ts_buf.append(ts_ms)
                pps_cnt += 1

            now = time.monotonic()
            if now - pps_win_start >= 1.0:
                cur_pps = pps_cnt / (now - pps_win_start)
                pps_cnt = 0
                pps_win_start = now

            if len(amps_buf) < win_size:
                if now - last_pred_at >= 0.4:
                    print(
                        f"  Dang gom du lieu dem: {len(amps_buf)}/{win_size} goi...   ",
                        end="\r",
                        flush=True,
                    )
                    last_pred_at = now
                continue

            if now - last_pred_at >= args.interval:
                last_pred_at = now
                feat = feat_fn(
                    np.asarray(amps_buf, dtype=np.float32),
                    np.asarray(rssi_buf, dtype=np.float32),
                    np.asarray(ts_buf, dtype=np.float64),
                ).reshape(1, -1)

                probs = clf.predict_proba(feat)[0]
                prob_hist.append(probs)
                avg_probs = np.mean(prob_hist, axis=0)

                best_idx = int(np.argmax(avg_probs))
                pred_label = str(labels[best_idx])
                conf_pct = int(round(float(avg_probs[best_idx]) * 100.0))

                client.send_action(pred_label, conf_pct)

                bar = " | ".join(
                    f"{lbl}:{p * 100:4.0f}%" for lbl, p in zip(labels, avg_probs)
                )
                print(
                    f"  => [{pred_label.upper():^9s}] ({conf_pct:3d}%) | "
                    f"{cur_pps:4.1f} pkt/s | {bar}   ",
                    end="\r",
                    flush=True,
                )
    except KeyboardInterrupt:
        print("\nDa dung nhan dien realtime.")
    finally:
        client.close()


if __name__ == "__main__":
    main()


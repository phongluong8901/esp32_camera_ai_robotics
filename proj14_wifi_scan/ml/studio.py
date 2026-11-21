"""Web-based AI Action Studio server with V1 (Standard) and V2 (Pro) side-by-side training & comparison."""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import threading
import time
import webbrowser
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Optional

import joblib
import numpy as np

from csi_common import (
    CSIUDPClient,
    DEFAULT_LABELS,
    UDP_PORT,
    WINDOW_SIZE,
    extract_features,
    extract_features_v2,
)
from train import run_training

LABEL_RE = re.compile(r"^[a-zA-Z0-9_-]{1,24}$")

CAL_WARMUP_PKTS = 15
CAL_TOTAL_PKTS = 80
NF_MIN = 0.004
NF_MAX = 0.08


class StudioEngine:
    """Thread-safe CSI UDP receiver, Dual-EMA metrics tracker, recorder, V1/V2 trainer, and predictor."""

    def __init__(self, esp_ip: str, udp_port: int, ml_dir: Path) -> None:
        self.esp_ip = esp_ip
        self.udp_port = udp_port
        self.ml_dir = ml_dir
        self.dataset_dir = ml_dir / "dataset"
        self.model_paths = {
            "v1": ml_dir / "model.joblib",
            "v2": ml_dir / "model_v2.joblib",
        }

        self._lock = threading.Lock()
        self._stop_event = threading.Event()
        self._client = CSIUDPClient(esp_ip, udp_port)

        # Streaming & radar telemetry state
        self._med_amp = np.zeros(62, dtype=np.float32)
        self._fast_amp = np.zeros(62, dtype=np.float32)
        self._sub_init = np.zeros(62, dtype=bool)
        self._is_calibrating = True
        self._cal_count = 0
        self._cal_dev_sum = 0.0
        self._cal_dev_cnt = 0
        self._cal_start_t = 0.0
        self._noise_floor = 0.015
        self._ema_var = 0.0
        self._ema_motion = 0.0
        self._prev_t = 0.0

        self._rssi = -60
        self._pkts = 0
        self._pps = 0.0
        self._pps_win_start = time.monotonic()
        self._pps_cnt = 0
        self._last_pkt_t = 0.0
        self._subs = [0] * 16

        # Rolling inference buffer & dual model state (v1 and v2)
        self._amps_buf: deque[np.ndarray] = deque(maxlen=WINDOW_SIZE)
        self._rssi_buf: deque[int] = deque(maxlen=WINDOW_SIZE)
        self._ts_buf: deque[int] = deque(maxlen=WINDOW_SIZE)
        self._prob_hist: deque[np.ndarray] = deque(maxlen=5)
        self._last_pred_t = 0.0

        self.models: dict[str, Any] = {"v1": None, "v2": None}
        self.model_labels: dict[str, list[str]] = {"v1": [], "v2": []}
        self.model_infos: dict[str, Optional[dict[str, Any]]] = {"v1": None, "v2": None}
        self.active_version = "v2"

        self.live_enabled = False
        self.live_act = ""
        self.live_conf = 0
        self.live_probs: dict[str, float] = {}

        # Studio recording / training state machine
        self.mode = "idle"  # idle | countdown | recording | training
        self.training_version = ""
        self.rec_label = ""
        self.rec_duration = 35.0
        self.rec_countdown = 5.0
        self.rec_countdown_end = 0.0
        self.rec_start_t = 0.0
        self.rec_end_t = 0.0
        self.rec_cancel_req = False
        self.rec_done_seq = 0
        self._rec_amps: list[np.ndarray] = []
        self._rec_rssi: list[int] = []
        self._rec_ts: list[int] = []

        self.message = "San sang thu du lieu & train AI (V1 / V2 Pro)."
        self.error = ""
        self._dataset_cache: dict[str, Any] = {"counts": {}, "files": {}, "total_packets": 0}

        self._refresh_dataset_cache()
        self._load_existing_models()

        self._thread = threading.Thread(target=self._run_loop, name="csi_studio_udp", daemon=True)
        self._thread.start()

    def _load_existing_models(self) -> None:
        loaded_any = False
        for ver, path in self.model_paths.items():
            if not path.exists():
                continue
            try:
                bundle = joblib.load(path)
                clf = bundle["model"]
                labels = [str(x) for x in bundle.get("labels", [])]
                self.models[ver] = clf
                self.model_labels[ver] = labels
                self.model_infos[ver] = {
                    "version": ver,
                    "name": str(bundle.get("model_name", f"Model_{ver.upper()}")),
                    "test_acc": float(bundle.get("test_acc", 0.0)),
                    "labels": labels,
                    "per_class": bundle.get("per_class", {}),
                    "train_windows": int(bundle.get("train_windows", 0)),
                    "test_windows": int(bundle.get("test_windows", 0)),
                }
                self.active_version = ver
                loaded_any = True
            except Exception as exc:
                self.error = f"Khong nap duoc model {ver.upper()}: {exc}"

        if self.models["v2"] is not None:
            self.active_version = "v2"
        elif self.models["v1"] is not None:
            self.active_version = "v1"

        if loaded_any:
            self.live_enabled = True
            info = self.model_infos[self.active_version]
            if info:
                self.message = (
                    f"Dang dung [{self.active_version.upper()}] {info['name']} "
                    f"({info['test_acc'] * 100:.1f}%)"
                )

    def _refresh_dataset_cache(self) -> None:
        counts: dict[str, int] = {lbl: 0 for lbl in DEFAULT_LABELS}
        files: dict[str, int] = {lbl: 0 for lbl in DEFAULT_LABELS}
        total = 0

        if self.dataset_dir.exists():
            for f in sorted(self.dataset_dir.glob("*.npz")):
                try:
                    with np.load(f, allow_pickle=False) as data:
                        lbl = str(data["label"].item())
                        cnt = int(len(data["amps"]))
                    counts[lbl] = counts.get(lbl, 0) + cnt
                    files[lbl] = files.get(lbl, 0) + 1
                    total += cnt
                except Exception:
                    continue

        with self._lock:
            self._dataset_cache = {
                "counts": counts,
                "files": files,
                "total_packets": int(total),
            }

    def calibrate(self) -> None:
        with self._lock:
            self._med_amp.fill(0.0)
            self._fast_amp.fill(0.0)
            self._sub_init.fill(False)
            self._is_calibrating = True
            self._cal_count = 0
            self._cal_dev_sum = 0.0
            self._cal_dev_cnt = 0
            self._cal_start_t = time.monotonic()
            self._prev_t = 0.0
            self._ema_var = 0.0
            self._ema_motion = 0.0

    def _update_signal_metrics(self, amps: np.ndarray, rssi: int, now: float) -> None:
        active = amps > 0.005
        if int(np.sum(active)) < 10:
            return

        if self._prev_t > 0.0:
            dt = max(0.005, min(0.5, now - self._prev_t))
        else:
            dt = 0.05
        self._prev_t = now

        alpha_fast = 1.0 - math.exp(-dt / 0.12)
        alpha_med = 1.0 - math.exp(-dt / 1.80)

        if self._is_calibrating:
            if self._cal_start_t <= 0.0:
                self._cal_start_t = now
            self._cal_count += 1

            if self._cal_count <= CAL_WARMUP_PKTS:
                new_mask = active & (~self._sub_init)
                old_mask = active & self._sub_init
                self._med_amp[new_mask] = amps[new_mask]
                self._fast_amp[new_mask] = amps[new_mask]
                self._sub_init[new_mask] = True
                w = 1.0 / float(self._cal_count)
                self._med_amp[old_mask] += w * (amps[old_mask] - self._med_amp[old_mask])
                self._fast_amp[old_mask] = self._med_amp[old_mask]
            else:
                new_mask = active & (~self._sub_init)
                old_mask = active & self._sub_init
                self._med_amp[new_mask] = amps[new_mask]
                self._fast_amp[new_mask] = amps[new_mask]
                self._sub_init[new_mask] = True
                self._fast_amp[old_mask] += alpha_fast * (amps[old_mask] - self._fast_amp[old_mask])
                self._med_amp[old_mask] += alpha_med * (amps[old_mask] - self._med_amp[old_mask])
                if np.any(old_mask):
                    diff = self._fast_amp[old_mask] - self._med_amp[old_mask]
                    dev = float(math.sqrt(float(np.mean(diff * diff))))
                    self._cal_dev_sum += dev
                    self._cal_dev_cnt += 1

            if self._cal_count >= CAL_TOTAL_PKTS or (
                (now - self._cal_start_t >= 2.5) and self._cal_count >= CAL_WARMUP_PKTS + 10
            ):
                nf = (self._cal_dev_sum / self._cal_dev_cnt) if self._cal_dev_cnt > 0 else 0.015
                self._noise_floor = max(NF_MIN, min(NF_MAX, float(nf)))
                self._is_calibrating = False
                self._ema_var = 1.0
                self._ema_motion = 0.0
        else:
            eval_mask = active & self._sub_init
            if np.any(eval_mask):
                self._fast_amp[eval_mask] += alpha_fast * (amps[eval_mask] - self._fast_amp[eval_mask])
                diff = self._fast_amp[eval_mask] - self._med_amp[eval_mask]
                dev = float(math.sqrt(float(np.mean(diff * diff))))
                nf = max(self._noise_floor, NF_MIN)
                raw_score = max(0.0, min(20.0, dev / nf))

                new_mask = active & (~self._sub_init)
                self._med_amp[new_mask] = amps[new_mask]
                self._fast_amp[new_mask] = amps[new_mask]
                self._sub_init[new_mask] = True
                self._med_amp[eval_mask] += alpha_med * (amps[eval_mask] - self._med_amp[eval_mask])

                if dev < self._noise_floor:
                    self._noise_floor += (1.0 - math.exp(-dt / 4.0)) * (dev - self._noise_floor)
                elif raw_score < 1.35:
                    self._noise_floor += (1.0 - math.exp(-dt / 30.0)) * (dev - self._noise_floor)
                self._noise_floor = max(NF_MIN, min(NF_MAX, float(self._noise_floor)))

                alpha_v = (1.0 - math.exp(-dt / 0.08)) if raw_score > self._ema_var else (1.0 - math.exp(-dt / 0.90))
                self._ema_var += alpha_v * (raw_score - self._ema_var)
                excess = max(0.0, self._ema_var - 1.0)
                self._ema_motion += (1.0 - math.exp(-dt / 0.35)) * (excess - self._ema_motion)

        disp_vals = np.where(self._sub_init, self._fast_amp, amps)[active]
        subs: list[int] = []
        if disp_vals.size > 0:
            for chunk in np.array_split(disp_vals, 16):
                val = float(np.mean(chunk)) * 45.0 if chunk.size > 0 else 0.0
                subs.append(max(0, min(100, int(round(val)))))
        else:
            subs = [0] * 16

        self._rssi = int(rssi)
        self._pkts += 1
        self._last_pkt_t = now
        self._subs = subs

    def _run_loop(self) -> None:
        while not self._stop_event.is_set():
            pkt = self._client.recv_frame(timeout=0.15)
            now = time.monotonic()

            if pkt is not None:
                ts_ms, rssi, _valid, _seq, amps = pkt
                self._pps_cnt += 1
                with self._lock:
                    self._update_signal_metrics(amps, rssi, now)
                    self._amps_buf.append(amps)
                    self._rssi_buf.append(rssi)
                    self._ts_buf.append(ts_ms)
                    is_rec = self.mode == "recording" and not self.rec_cancel_req
                if is_rec:
                    self._rec_amps.append(amps)
                    self._rec_rssi.append(int(rssi))
                    self._rec_ts.append(int(ts_ms))

            if now - self._pps_win_start >= 1.0:
                rate = self._pps_cnt / max(now - self._pps_win_start, 1e-3)
                with self._lock:
                    self._pps = float(rate)
                self._pps_cnt = 0
                self._pps_win_start = now

            # Check recording state machine transitions
            finish_label = ""
            finish_cancelled = False
            with self._lock:
                if self.mode == "countdown":
                    if self.rec_cancel_req:
                        self.mode = "idle"
                        self.rec_cancel_req = False
                        self.message = "Da huy dem nguoc."
                    elif now >= self.rec_countdown_end:
                        self._rec_amps.clear()
                        self._rec_rssi.clear()
                        self._rec_ts.clear()
                        self.rec_start_t = now
                        self.rec_end_t = now + self.rec_duration
                        self.mode = "recording"
                        self.message = f"Dang thu [{self.rec_label}]..."
                elif self.mode == "recording":
                    if self.rec_cancel_req or now >= self.rec_end_t:
                        finish_label = self.rec_label
                        finish_cancelled = self.rec_cancel_req

            if finish_label:
                saved_cnt = 0
                err_msg = ""
                if not finish_cancelled:
                    if len(self._rec_amps) >= WINDOW_SIZE + 10:
                        self.dataset_dir.mkdir(parents=True, exist_ok=True)
                        stamp = int(time.time())
                        out_path = self.dataset_dir / f"{finish_label}_{stamp}.npz"
                        np.savez_compressed(
                            out_path,
                            amps=np.vstack(self._rec_amps).astype(np.float32),
                            rssi=np.asarray(self._rec_rssi, dtype=np.int16),
                            ts_ms=np.asarray(self._rec_ts, dtype=np.uint32),
                            label=np.asarray(finish_label),
                        )
                        saved_cnt = len(self._rec_amps)
                    else:
                        err_msg = (
                            f"Chi thu duoc {len(self._rec_amps)} goi cho [{finish_label}] "
                            f"(can >= {WINDOW_SIZE + 10}). Hay kiem tra ket noi ESP32."
                        )

                self._rec_amps.clear()
                self._rec_rssi.clear()
                self._rec_ts.clear()
                self._refresh_dataset_cache()

                with self._lock:
                    self.rec_cancel_req = False
                    self.mode = "idle"
                    if finish_cancelled:
                        self.message = f"Da dung thu [{finish_label}]."
                    elif err_msg:
                        self.error = err_msg
                    else:
                        self.rec_done_seq += 1
                        self.error = ""
                        self.message = f"Da luu {saved_cnt} goi cho nhan [{finish_label}]."

            # Realtime AI inference when live_enabled and idle
            with self._lock:
                ver = self.active_version
                active_clf = self.models.get(ver)
                should_pred = (
                    self.mode == "idle"
                    and self.live_enabled
                    and active_clf is not None
                    and len(self._amps_buf) >= WINDOW_SIZE
                    and (now - self._last_pred_t >= 0.25)
                )
                if should_pred:
                    self._last_pred_t = now
                    clf = active_clf
                    labels = list(self.model_labels.get(ver, []))
                    amps_arr = np.asarray(self._amps_buf, dtype=np.float32)
                    rssi_arr = np.asarray(self._rssi_buf, dtype=np.float32)
                    ts_arr = np.asarray(self._ts_buf, dtype=np.float64)
                else:
                    clf = None
                    labels = []
                    amps_arr = rssi_arr = ts_arr = None

            if clf is not None and amps_arr is not None:
                try:
                    feat_fn = extract_features_v2 if ver == "v2" else extract_features
                    feat = feat_fn(amps_arr, rssi_arr, ts_arr).reshape(1, -1)
                    probs = clf.predict_proba(feat)[0]
                    with self._lock:
                        self._prob_hist.append(probs)
                        if ver == "v2" and len(self._prob_hist) > 1:
                            # Exponentially weighted probability smoothing for V2
                            weights = np.linspace(0.4, 1.0, len(self._prob_hist))
                            weights /= np.sum(weights)
                            avg_probs = np.average(np.vstack(self._prob_hist), axis=0, weights=weights)
                        else:
                            avg_probs = np.mean(list(self._prob_hist)[-3:], axis=0)
                        best_idx = int(np.argmax(avg_probs))
                        pred_label = str(labels[best_idx])
                        conf_pct = int(round(float(avg_probs[best_idx]) * 100.0))
                        self.live_act = pred_label
                        self.live_conf = conf_pct
                        self.live_probs = {
                            str(lbl): round(float(p), 4) for lbl, p in zip(labels, avg_probs)
                        }
                    self._client.send_action(pred_label, conf_pct)
                except Exception:
                    pass

    def start_record(self, label: str, duration: float, countdown: float) -> tuple[bool, str]:
        lbl = label.strip().lower()
        if not LABEL_RE.fullmatch(lbl):
            return False, "Ten nhan khong hop le (chi dung a-z, 0-9, _, -)."
        dur = max(10.0, min(120.0, float(duration)))
        cd = max(0.0, min(15.0, float(countdown)))

        with self._lock:
            if self.mode != "idle":
                return False, f"He thong dang ban ({self.mode}), hay dung truoc."
            if (time.monotonic() - self._last_pkt_t > 2.5) or self._pkts == 0:
                return False, "Chua nhan duoc goi UDP tu ESP32. Kiem tra IP/firmware."
            now = time.monotonic()
            self.rec_label = lbl
            self.rec_duration = dur
            self.rec_countdown = cd
            self.rec_cancel_req = False
            self.error = ""
            self._rec_amps.clear()
            self._rec_rssi.clear()
            self._rec_ts.clear()
            if cd > 0:
                self.mode = "countdown"
                self.rec_countdown_end = now + cd
                self.message = f"Chuan bi [{lbl}]..."
            else:
                self.mode = "recording"
                self.rec_start_t = now
                self.rec_end_t = now + dur
                self.message = f"Dang thu [{lbl}]..."
        return True, "OK"

    def stop_record(self) -> tuple[bool, str]:
        with self._lock:
            if self.mode in ("countdown", "recording"):
                self.rec_cancel_req = True
                return True, "Dang dung..."
            return False, "Khong co phien thu nao dang chay."

    def clear_dataset(self, label: Optional[str]) -> tuple[bool, str]:
        with self._lock:
            if self.mode != "idle":
                return False, f"Khong the xoa khi dang {self.mode}."

        removed = 0
        if self.dataset_dir.exists():
            if label:
                lbl = label.strip().lower()
                if not LABEL_RE.fullmatch(lbl):
                    return False, "Ten nhan khong hop le."
                for f in self.dataset_dir.glob(f"{lbl}_*.npz"):
                    f.unlink(missing_ok=True)
                    removed += 1
                msg = f"Da xoa {removed} file cua nhan [{lbl}]."
            else:
                for f in self.dataset_dir.glob("*.npz"):
                    f.unlink(missing_ok=True)
                    removed += 1
                msg = f"Da xoa tat ca {removed} file dataset."
        else:
            msg = "Dataset dang trong."

        self._refresh_dataset_cache()
        with self._lock:
            self.error = ""
            self.message = msg
        return True, msg

    def start_train(self, version: str = "v2") -> tuple[bool, str]:
        ver = "v2" if str(version).lower() == "v2" else "v1"
        with self._lock:
            if self.mode != "idle":
                return False, f"He thong dang ban ({self.mode})."
            self.mode = "training"
            self.training_version = ver
            self.error = ""
            self.message = f"Dang trich dac trung & train AI [{ver.upper()}]..."

        t = threading.Thread(
            target=self._train_worker,
            args=(ver,),
            name=f"csi_studio_train_{ver}",
            daemon=True,
        )
        t.start()
        return True, f"Bat dau train AI [{ver.upper()}]."

    def _train_worker(self, ver: str) -> None:
        try:
            out_path = self.model_paths[ver]
            payload = run_training(self.dataset_dir, out_path, version=ver)
            final_clf = payload["model"]
            final_labels = list(payload["labels"])
            best_name = str(payload["model_name"])
            best_acc = float(payload["test_acc"])

            info = {
                "version": ver,
                "name": best_name,
                "test_acc": round(best_acc, 4),
                "labels": final_labels,
                "per_class": payload["per_class"],
                "train_windows": int(payload["train_windows"]),
                "test_windows": int(payload["test_windows"]),
            }
            with self._lock:
                self.models[ver] = final_clf
                self.model_labels[ver] = final_labels
                self.model_infos[ver] = info
                self.active_version = ver
                self._prob_hist.clear()
                self.live_enabled = True
                self.mode = "idle"
                self.training_version = ""
                self.error = ""
                self.message = (
                    f"Train [{ver.upper()}] xong: {best_name} "
                    f"(Test Acc {best_acc * 100:.1f}%) · Dang dung {ver.upper()}"
                )
        except Exception as exc:
            with self._lock:
                self.mode = "idle"
                self.training_version = ""
                self.error = str(exc)

    def select_model(self, version: str) -> tuple[bool, str]:
        ver = "v2" if str(version).lower() == "v2" else "v1"
        with self._lock:
            if self.models.get(ver) is None:
                return False, f"Chua train model [{ver.upper()}]! Hay bam Train {ver.upper()} truoc."
            self.active_version = ver
            self._prob_hist.clear()
            self.live_enabled = True
            info = self.model_infos[ver]
            acc_str = f"{info['test_acc'] * 100:.1f}%" if info else ""
            self.error = ""
            self.message = f"Da chuyen sang dung model [{ver.upper()}] ({acc_str})."
            return True, self.message

    def set_live(self, enabled: bool) -> tuple[bool, str]:
        with self._lock:
            active_clf = self.models.get(self.active_version)
            if enabled and active_clf is None:
                return False, "Chua co model! Hay thu it nhat 2 nhan roi bam Train V1 hoac V2."
            self.live_enabled = bool(enabled)
            self._prob_hist.clear()
            if not self.live_enabled:
                self.live_act = ""
                self.live_conf = 0
                self.live_probs = {}
                self.message = "Da tat AI Live."
            else:
                self.message = f"Da bat nhan dien AI Live ({self.active_version.upper()})."
            self.error = ""
        return True, self.message

    def get_metrics(self) -> dict[str, Any]:
        with self._lock:
            now = time.monotonic()
            stale = (self._last_pkt_t == 0.0) or (now - self._last_pkt_t > 2.0)
            active_clf = self.models.get(self.active_version)
            use_ai = self.live_enabled and (active_clf is not None) and (self.mode == "idle") and not stale
            return {
                "rssi": int(self._rssi),
                "var": 0.0 if (stale or self._is_calibrating) else round(float(self._ema_var), 2),
                "motion": 0.0 if (stale or self._is_calibrating) else round(float(self._ema_motion), 2),
                "pkts": int(self._pkts),
                "pps": 0 if stale else int(round(self._pps)),
                "cal": 1 if self._is_calibrating else 0,
                "nf": round(float(self._noise_floor), 4),
                "raw": int(self._pkts),
                "act": self.live_act if use_ai else "",
                "conf": int(self.live_conf) if use_ai else 0,
                "subs": list(self._subs),
            }

    def get_status(self) -> dict[str, Any]:
        with self._lock:
            now = time.monotonic()
            connected = (self._last_pkt_t > 0.0) and (now - self._last_pkt_t < 2.5)
            rec_info: Optional[dict[str, Any]] = None
            if self.mode == "countdown":
                rem = max(0.0, self.rec_countdown_end - now)
                rec_info = {
                    "label": self.rec_label,
                    "remaining_s": round(rem, 1),
                    "elapsed_s": 0.0,
                    "duration_s": float(self.rec_duration),
                    "packets": 0,
                }
            elif self.mode == "recording":
                elapsed = max(0.0, now - self.rec_start_t)
                rem = max(0.0, self.rec_end_t - now)
                rec_info = {
                    "label": self.rec_label,
                    "remaining_s": round(rem, 1),
                    "elapsed_s": round(elapsed, 1),
                    "duration_s": float(self.rec_duration),
                    "packets": int(len(self._rec_amps)),
                }

            active_info = self.model_infos.get(self.active_version)
            active_clf = self.models.get(self.active_version)

            return {
                "ok": True,
                "esp_ip": self.esp_ip,
                "udp_connected": bool(connected),
                "pps": int(round(self._pps)) if connected else 0,
                "mode": self.mode,
                "training_version": self.training_version,
                "rec": rec_info,
                "rec_done_seq": int(self.rec_done_seq),
                "dataset": dict(self._dataset_cache),
                "active_version": self.active_version,
                "model": active_info,
                "models": {
                    "v1": self.model_infos["v1"],
                    "v2": self.model_infos["v2"],
                },
                "live": {
                    "enabled": bool(self.live_enabled and active_clf is not None),
                    "version": self.active_version,
                    "act": self.live_act,
                    "conf": int(self.live_conf),
                    "probs": dict(self.live_probs),
                },
                "message": self.message,
                "error": self.error,
            }

    def close(self) -> None:
        self._stop_event.set()
        self._thread.join(timeout=1.5)
        self._client.close()


def make_handler(engine: StudioEngine, html_path: Path):
    """Create HTTP handler bound to the shared StudioEngine instance."""

    class StudioHTTPHandler(BaseHTTPRequestHandler):
        def log_message(self, format: str, *args: Any) -> None:
            return

        def _send_common_headers(self, content_type: str, length: int) -> None:
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(length))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.send_header("Access-Control-Allow-Private-Network", "true")

        def _send_json(self, payload: dict[str, Any], code: int = 200) -> None:
            raw = json.dumps(payload).encode("utf-8")
            self.send_response(code)
            self._send_common_headers("application/json; charset=utf-8", len(raw))
            self.end_headers()
            self.wfile.write(raw)

        def _read_json_body(self) -> dict[str, Any]:
            length = int(self.headers.get("Content-Length", "0") or "0")
            if length <= 0 or length > 8192:
                return {}
            raw = self.rfile.read(length)
            try:
                parsed = json.loads(raw.decode("utf-8"))
                return parsed if isinstance(parsed, dict) else {}
            except Exception:
                return {}

        def do_OPTIONS(self) -> None:
            self.send_response(204)
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type")
            self.send_header("Access-Control-Allow-Private-Network", "true")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()

        def do_GET(self) -> None:
            path = self.path.split("?", 1)[0]
            if path in ("/", "/index.html"):
                raw = html_path.read_bytes()
                self.send_response(200)
                self._send_common_headers("text/html; charset=utf-8", len(raw))
                self.end_headers()
                self.wfile.write(raw)
            elif path == "/api/metrics":
                self._send_json(engine.get_metrics())
            elif path == "/api/studio/status":
                self._send_json(engine.get_status())
            else:
                self._send_json({"ok": False, "error": "Not found"}, code=404)

        def do_POST(self) -> None:
            path = self.path.split("?", 1)[0]
            body = self._read_json_body()

            if path == "/api/studio/record":
                ok, msg = engine.start_record(
                    str(body.get("label", "")),
                    float(body.get("duration", 35.0)),
                    float(body.get("countdown", 5.0)),
                )
                self._send_json({"ok": ok, "error": "" if ok else msg}, code=200 if ok else 400)
            elif path == "/api/studio/stop":
                ok, msg = engine.stop_record()
                self._send_json({"ok": ok, "message": msg})
            elif path == "/api/studio/clear":
                lbl = body.get("label")
                ok, msg = engine.clear_dataset(str(lbl) if lbl else None)
                self._send_json({"ok": ok, "message": msg}, code=200 if ok else 400)
            elif path == "/api/studio/train":
                ver = str(body.get("version", "v2"))
                ok, msg = engine.start_train(version=ver)
                self._send_json({"ok": ok, "error": "" if ok else msg}, code=200 if ok else 400)
            elif path == "/api/studio/select_model":
                ver = str(body.get("version", "v2"))
                ok, msg = engine.select_model(version=ver)
                self._send_json({"ok": ok, "error": "" if ok else msg}, code=200 if ok else 400)
            elif path == "/api/studio/live":
                ok, msg = engine.set_live(bool(body.get("enabled", False)))
                self._send_json({"ok": ok, "error": "" if ok else msg}, code=200 if ok else 400)
            elif path == "/api/studio/calibrate":
                engine.calibrate()
                self._send_json({"ok": True})
            else:
                self._send_json({"ok": False, "error": "Not found"}, code=404)

    return StudioHTTPHandler


def main() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    ml_dir = Path(__file__).resolve().parent
    html_path = ml_dir.parent / "src" / "index.html"
    if not html_path.exists():
        raise FileNotFoundError(f"Khong tim thay {html_path}")

    parser = argparse.ArgumentParser(description="Web UI thu du lieu, train (V1 vs V2 Pro) & chay AI Wi-Fi CSI")
    parser.add_argument("esp_ip", help="Dia chi IP cua ESP32-S3 (vi du 192.168.1.12)")
    parser.add_argument("--http-port", type=int, default=8000, help="Cong web Studio (mac dinh 8000)")
    parser.add_argument("--udp-port", type=int, default=UDP_PORT, help="Cong UDP ESP32 (mac dinh 5555)")
    parser.add_argument("--no-browser", action="store_true", help="Khong tu dong mo trinh duyet")
    args = parser.parse_args()

    engine = StudioEngine(args.esp_ip, args.udp_port, ml_dir)
    handler = make_handler(engine, html_path)
    server = ThreadingHTTPServer(("127.0.0.1", args.http_port), handler)
    server.daemon_threads = True

    url = f"http://127.0.0.1:{args.http_port}"
    print("=" * 58)
    print(f"  CSI AI ACTION STUDIO (V1 & V2 Pro) dang chay tai: {url}")
    print(f"  ESP32 UDP target: {args.esp_ip}:{args.udp_port}")
    print("  Bam Ctrl+C trong cua so nay de dung Studio.")
    print("=" * 58)

    if not args.no_browser:
        threading.Timer(0.6, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nDang dong CSI AI Studio...")
    finally:
        server.server_close()
        engine.close()


if __name__ == "__main__":
    main()

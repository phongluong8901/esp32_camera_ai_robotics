"""Shared UDP CSI stream client and V1 + V2 feature extraction for ESP32-S3 HAR."""

from __future__ import annotations

import socket
import struct
import time
from typing import Optional

import numpy as np
from numpy.lib.stride_tricks import sliding_window_view

UDP_PORT = 5555
FRAME_FMT = "<IbBH62H"
FRAME_SIZE = struct.calcsize(FRAME_FMT)  # 132 bytes

WINDOW_SIZE = 64  # ~1.3s at ~50 pkt/s
STRIDE = 16       # V1 stride (~0.3s step)
STRIDE_V2 = 8     # V2 denser stride (~0.15s step, 2x training windows)
DEFAULT_LABELS = ["dung_yen", "go_phim", "vay_tay", "di_lai", "nhay"]


class CSIUDPClient:
    """UDP client with automatic hole-punching keepalive to ESP32 port 5555."""

    def __init__(self, esp_ip: str, port: int = UDP_PORT) -> None:
        self.esp_addr = (esp_ip, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # Bind local ephemeral port first so recvfrom is safe on Windows.
        self.sock.bind(("0.0.0.0", 0))
        self.sock.settimeout(0.5)
        self._last_sub_at = 0.0

    def ensure_subscribed(self, interval: float = 3.0) -> None:
        now = time.monotonic()
        if now - self._last_sub_at >= interval:
            self.sock.sendto(b"SUB", self.esp_addr)
            self._last_sub_at = now

    def recv_frame(self, timeout: float = 0.5) -> Optional[tuple[int, int, int, int, np.ndarray]]:
        self.ensure_subscribed()
        self.sock.settimeout(timeout)
        try:
            data, _ = self.sock.recvfrom(512)
        except (socket.timeout, ConnectionResetError, OSError):
            return None

        if len(data) != FRAME_SIZE:
            return None

        ts_ms, rssi, valid_cnt, seq, *raw_amps = struct.unpack(FRAME_FMT, data)
        amps = np.asarray(raw_amps, dtype=np.float32) / 1000.0
        return ts_ms, rssi, valid_cnt, seq, amps

    def send_action(self, label: str, conf_pct: int) -> None:
        conf_clamped = max(0, min(100, int(conf_pct)))
        msg = f"ACT:{label}:{conf_clamped}".encode("ascii", errors="ignore")
        try:
            self.sock.sendto(msg, self.esp_addr)
        except OSError:
            pass

    def close(self) -> None:
        try:
            self.sock.sendto(b"UNSUB", self.esp_addr)
        except OSError:
            pass
        self.sock.close()


def _safe_pair_corr(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Compute row-wise Pearson correlation without zero-variance warnings."""
    a_c = a - np.mean(a, axis=1, keepdims=True)
    b_c = b - np.mean(b, axis=1, keepdims=True)
    num = np.sum(a_c * b_c, axis=1)
    den = np.sqrt(np.sum(a_c * a_c, axis=1) * np.sum(b_c * b_c, axis=1))
    out = np.zeros_like(num, dtype=np.float32)
    valid = den > 1e-8
    out[valid] = num[valid] / den[valid]
    return out


def extract_features(
    amps_win: np.ndarray,
    rssi_win: np.ndarray,
    ts_win: np.ndarray,
) -> np.ndarray:
    """[V1] Extract 89 position-invariant temporal, spectral, and spatial CSI features."""
    x = np.asarray(amps_win, dtype=np.float32)
    rssi = np.asarray(rssi_win, dtype=np.float32)
    ts = np.asarray(ts_win, dtype=np.float64)

    n = x.shape[0]
    if n >= 3:
        dts = np.diff(ts)
        pos_dts = dts[dts > 1.0]
        median_dt_ms = float(np.median(pos_dts)) if pos_dts.size > 0 else 20.0
        fs = float(np.clip(1000.0 / median_dt_ms, 15.0, 120.0))
    else:
        fs = 50.0

    feats: list[float] = []

    # 1. Group 62 subcarriers into 8 frequency sub-bands (8 * 8 = 64 features)
    freqs = np.fft.rfftfreq(n, d=1.0 / fs)
    mask_low = (freqs >= 0.3) & (freqs < 1.5)
    mask_mid = (freqs >= 1.5) & (freqs < 4.0)
    mask_high = (freqs >= 4.0) & (freqs <= 10.0)

    for sub in np.array_split(x, 8, axis=1):
        mask = (sub > 0.005).astype(np.float32)
        cnt = np.maximum(mask.sum(axis=1), 1.0)
        band = (sub * mask).sum(axis=1) / cnt

        p25, p75 = np.percentile(band, [25.0, 75.0])
        p05, p95 = np.percentile(band, [5.0, 95.0])
        diff = np.diff(band) if n > 1 else np.zeros(1, dtype=np.float32)

        centered = band - float(np.mean(band))
        psd = np.abs(np.fft.rfft(centered)) ** 2 / max(n, 1)

        feats.extend([
            float(np.std(band)),
            float(p75 - p25),
            float(p95 - p05),
            float(np.sqrt(np.mean(diff * diff))),
            float(np.max(np.abs(diff))),
            float(np.sum(psd[mask_low])) if np.any(mask_low) else 0.0,
            float(np.sum(psd[mask_mid])) if np.any(mask_mid) else 0.0,
            float(np.sum(psd[mask_high])) if np.any(mask_high) else 0.0,
        ])

    # 2. Spatial turbulence across subcarriers per frame (5 features)
    turb = np.std(x, axis=1)
    tp05, tp95 = np.percentile(turb, [5.0, 95.0])
    tdiff = np.diff(turb) if n > 1 else np.zeros(1, dtype=np.float32)
    feats.extend([
        float(np.mean(turb)),
        float(np.std(turb)),
        float(tp95),
        float(tp95 - tp05),
        float(np.sqrt(np.mean(tdiff * tdiff))),
    ])

    # 3. Frame-to-frame Euclidean step energy (5 features)
    if n > 1:
        step = np.sqrt(np.mean(np.diff(x, axis=0) ** 2, axis=1))
    else:
        step = np.zeros(1, dtype=np.float32)
    step_mean = float(np.mean(step))
    step_max = float(np.max(step))
    feats.extend([
        step_mean,
        float(np.std(step)),
        step_max,
        float(np.percentile(step, 95.0)),
        step_max / (step_mean + 1e-6),
    ])

    # 4. Deviation from window mean profile (6 features)
    mean_prof = np.mean(x, axis=0, keepdims=True)
    dev = np.sqrt(np.mean((x - mean_prof) ** 2, axis=1))
    dev_mean = float(np.mean(dev))
    dev_max = float(np.max(dev))
    feats.extend([
        dev_mean,
        float(np.std(dev)),
        dev_max,
        float(np.percentile(dev, 95.0)),
        float(np.percentile(dev, 10.0)),
        dev_max / (dev_mean + 1e-6),
    ])

    # 5. Spectral shape of motion deviation curve (3 features)
    dev_psd = np.abs(np.fft.rfft(dev - dev_mean)) ** 2
    valid_spec = freqs >= 0.3
    if np.any(valid_spec):
        sf = freqs[valid_spec]
        sp = dev_psd[valid_spec]
        p_sum = float(np.sum(sp))
        if p_sum > 1e-12:
            dom_freq = float(sf[int(np.argmax(sp))])
            centroid = float(np.sum(sf * sp) / p_sum)
            probs = sp / p_sum
            nz = probs > 1e-12
            entropy = float(-np.sum(probs[nz] * np.log2(probs[nz])))
        else:
            dom_freq = centroid = entropy = 0.0
    else:
        dom_freq = centroid = entropy = 0.0
    feats.extend([dom_freq, centroid, entropy])

    # 6. Temporal lag-5 self-correlation (2 features)
    if n > 5:
        corr = _safe_pair_corr(x[:-5], x[5:])
        feats.extend([float(np.mean(corr)), float(np.min(corr))])
    else:
        feats.extend([1.0, 1.0])

    # 7. RSSI dynamics (no raw RSSI level to avoid seat-position overfitting) (4 features)
    rp05, rp95 = np.percentile(rssi, [5.0, 95.0])
    rdiff = np.diff(rssi) if n > 1 else np.zeros(1, dtype=np.float32)
    feats.extend([
        float(np.std(rssi)),
        float(rp95 - rp05),
        float(np.sqrt(np.mean(rdiff * rdiff))),
        float(np.max(np.abs(rdiff))),
    ])

    arr = np.asarray(feats, dtype=np.float32)
    return np.nan_to_num(arr, nan=0.0, posinf=0.0, neginf=0.0)


def _denoise_csi(x: np.ndarray) -> np.ndarray:
    """Vectorized 5-tap Hampel outlier filter + 3-tap temporal low-pass smoothing."""
    n = x.shape[0]
    if n < 5:
        return x.copy()

    # 1. 5-tap Hampel filter along time axis (axis=0)
    padded = np.pad(x, ((2, 2), (0, 0)), mode="edge")
    # shape: (n, 62, 5)
    wins = sliding_window_view(padded, window_shape=5, axis=0)
    med = np.median(wins, axis=-1)
    mad = np.median(np.abs(wins - med[..., None]), axis=-1)
    thresh = 3.0 * 1.4826 * mad + 1e-4
    clean = np.where(np.abs(x - med) > thresh, med, x)

    # 2. 3-tap smoothing [0.2, 0.6, 0.2] to remove single-frame thermal jitter
    pad3 = np.pad(clean, ((1, 1), (0, 0)), mode="edge")
    smoothed = 0.2 * pad3[:-2] + 0.6 * pad3[1:-1] + 0.2 * pad3[2:]
    return smoothed.astype(np.float32)


def _max_autocorr(sig: np.ndarray, min_lag: int, max_lag: int) -> float:
    """Compute peak normalized autocorrelation across [min_lag, max_lag]."""
    c = sig - float(np.mean(sig))
    energy = float(np.sum(c * c))
    n = len(c)
    if energy <= 1e-10 or min_lag >= n:
        return 0.0
    hi = min(max_lag, n - 2)
    if hi < min_lag:
        return 0.0
    best = -1.0
    for lag in range(min_lag, hi + 1):
        r = float(np.sum(c[:-lag] * c[lag:])) / energy
        if r > best:
            best = r
    return max(0.0, best)


def extract_features_v2(
    amps_win: np.ndarray,
    rssi_win: np.ndarray,
    ts_win: np.ndarray,
) -> np.ndarray:
    """[V2 Pro] Hampel-denoised 145-feature extractor with spatial drift, burst & periodicity."""
    x_raw = np.asarray(amps_win, dtype=np.float32)
    rssi = np.asarray(rssi_win, dtype=np.float32)
    ts = np.asarray(ts_win, dtype=np.float64)

    xd = _denoise_csi(x_raw)
    n = xd.shape[0]
    if n >= 3:
        dts = np.diff(ts)
        pos_dts = dts[dts > 1.0]
        median_dt_ms = float(np.median(pos_dts)) if pos_dts.size > 0 else 20.0
        fs = float(np.clip(1000.0 / median_dt_ms, 15.0, 120.0))
    else:
        fs = 50.0

    # Base 89 features computed on denoised CSI
    base_feats = extract_features(xd, rssi, ts)
    extra: list[float] = []

    # A. Macroscopic Start-vs-End Spatial Drift (6 features)
    # Separates location-changing motion (di_lai) from in-place motion (go_phim, vay_tay)
    quarters = np.array_split(xd, 4, axis=0)
    q_means = [np.mean(q, axis=0) for q in quarters if len(q) > 0]
    if len(q_means) == 4:
        m0, m1, m2, m3 = q_means
        diff_03 = m3 - m0
        drift_l2 = float(np.sqrt(np.mean(diff_03 * diff_03)))
        drift_l1 = float(np.mean(np.abs(diff_03)))
        drift_max = float(np.max(np.abs(diff_03)))
        pair_dists = [
            float(np.sqrt(np.mean((q_means[i] - q_means[j]) ** 2)))
            for i in range(4)
            for j in range(i + 1, 4)
        ]
        max_pair_dist = max(pair_dists)
        step_chain = (
            float(np.sqrt(np.mean((m1 - m0) ** 2)))
            + float(np.sqrt(np.mean((m2 - m1) ** 2)))
            + float(np.sqrt(np.mean((m3 - m2) ** 2)))
        )
        path_ratio = drift_l2 / (step_chain + 1e-6)
        denom = float(np.linalg.norm(m0) * np.linalg.norm(m3))
        cos_sim = float(np.dot(m0, m3) / denom) if denom > 1e-8 else 1.0
        extra.extend([drift_l2, drift_l1, drift_max, max_pair_dist, path_ratio, 1.0 - cos_sim])
    else:
        extra.extend([0.0] * 6)

    # B. 4-Subsegment Burst & Energy Contrast (10 features)
    # Captures impulsive jump bursts (nhay) vs steady typing/walking
    seg_stds: list[float] = []
    seg_vels: list[float] = []
    for q in quarters:
        if len(q) > 1:
            seg_stds.append(float(np.mean(np.std(q, axis=0))))
            qv = np.sqrt(np.mean(np.diff(q, axis=0) ** 2, axis=1))
            seg_vels.append(float(np.mean(qv)))
        else:
            seg_stds.append(0.0)
            seg_vels.append(0.0)
    s_arr = np.asarray(seg_stds, dtype=np.float32)
    v_arr = np.asarray(seg_vels, dtype=np.float32)
    extra.extend([
        float(np.max(s_arr)),
        float(np.min(s_arr)),
        float(np.std(s_arr)),
        float(np.max(s_arr) / (np.min(s_arr) + 1e-5)),
        float(np.max(s_arr) / (np.mean(s_arr) + 1e-5)),
        float(np.max(v_arr)),
        float(np.min(v_arr)),
        float(np.std(v_arr)),
        float(np.max(v_arr) / (np.min(v_arr) + 1e-5)),
        float(np.max(v_arr) / (np.mean(v_arr) + 1e-5)),
    ])

    # C. Second-Order Acceleration Dynamics (6 features)
    if n > 2:
        acc = np.diff(xd, n=2, axis=0)
        acc_mag = np.sqrt(np.mean(acc * acc, axis=1))
    else:
        acc_mag = np.zeros(1, dtype=np.float32)
    acc_mean = float(np.mean(acc_mag))
    acc_max = float(np.max(acc_mag))
    extra.extend([
        acc_mean,
        float(np.std(acc_mag)),
        acc_max,
        float(np.percentile(acc_mag, 95.0)),
        float(np.percentile(acc_mag, 50.0)),
        acc_max / (acc_mean + 1e-6),
    ])

    # D. Top-12 Most Sensitive Subcarriers (12 features)
    # Improves subtle go_phim vs dung_yen separation by focusing on highest-SNR multipath bins
    sub_p25, sub_p75 = np.percentile(xd, [25.0, 75.0], axis=0)
    sub_iqr = sub_p75 - sub_p25
    order = np.argsort(sub_iqr)
    top12_idx = order[-12:]
    bot12_idx = order[:12]
    top_x = xd[:, top12_idx]
    top_iqr = sub_iqr[top12_idx]
    bot_iqr = sub_iqr[bot12_idx]

    top_vel = np.sqrt(np.mean(np.diff(top_x, axis=0) ** 2, axis=1)) if n > 1 else np.zeros(1, dtype=np.float32)
    top_acc = np.sqrt(np.mean(np.diff(top_x, n=2, axis=0) ** 2, axis=1)) if n > 2 else np.zeros(1, dtype=np.float32)
    top_c = top_x - np.mean(top_x, axis=0, keepdims=True)
    top_zcr = np.mean(np.diff(np.signbit(top_c), axis=0), axis=0) if n > 1 else np.zeros(12, dtype=np.float32)
    tp05, tp50, tp95 = np.percentile(top_vel, [5.0, 50.0, 95.0])

    extra.extend([
        float(np.mean(top_iqr)),
        float(np.max(top_iqr)),
        float(np.std(top_iqr)),
        float(np.mean(top_iqr) / (np.mean(bot_iqr) + 1e-5)),
        float(np.mean(top_vel)),
        float(tp95),
        float(tp95 - tp05),
        float((tp95 - tp50) / (tp50 - tp05 + 1e-5)),
        float(np.mean(top_acc)),
        float(np.percentile(top_acc, 95.0)),
        float(np.mean(top_zcr)),
        float(np.std(top_zcr)),
    ])

    # E. Sub-band Zero-Crossing Rate & Autocorrelation Periodicity (16 features)
    min_lag = max(3, int(round(0.22 * fs)))
    max_lag = max(min_lag + 2, int(round(0.85 * fs)))
    for sub in np.array_split(xd, 8, axis=1):
        mask = (sub > 0.005).astype(np.float32)
        cnt = np.maximum(mask.sum(axis=1), 1.0)
        band = (sub * mask).sum(axis=1) / cnt
        bc = band - float(np.mean(band))
        zcr = float(np.mean(np.diff(np.signbit(bc)))) if n > 1 else 0.0
        ac = _max_autocorr(bc, min_lag, max_lag)
        extra.extend([zcr, ac])

    # F. Cross-Subcarrier SVD Coherence & Spectral Ratios (6 features)
    xd_c = xd - np.mean(xd, axis=0, keepdims=True)
    try:
        sv = np.linalg.svd(xd_c, full_matrices=False, compute_uv=False)
        sv_sq = sv * sv
        sv_sum = float(np.sum(sv_sq))
        if sv_sum > 1e-10:
            sv1_ratio = float(sv_sq[0] / sv_sum)
            sv12_ratio = float(np.sum(sv_sq[:2]) / sv_sum)
            sv_p = sv_sq / sv_sum
            nz = sv_p > 1e-12
            sv_ent = float(-np.sum(sv_p[nz] * np.log2(sv_p[nz])))
        else:
            sv1_ratio = sv12_ratio = sv_ent = 0.0
    except Exception:
        sv1_ratio = sv12_ratio = sv_ent = 0.0

    dev = np.sqrt(np.mean(xd_c * xd_c, axis=1))
    freqs = np.fft.rfftfreq(n, d=1.0 / fs)
    dev_psd = np.abs(np.fft.rfft(dev - float(np.mean(dev)))) ** 2
    valid_spec = freqs >= 0.3
    if np.any(valid_spec):
        sp = dev_psd[valid_spec]
        peak_to_mean = float(np.max(sp) / (np.mean(sp) + 1e-8))
    else:
        peak_to_mean = 0.0
    p_low = float(np.sum(dev_psd[(freqs >= 0.3) & (freqs < 1.5)]))
    p_mid = float(np.sum(dev_psd[(freqs >= 1.5) & (freqs < 4.0)]))
    p_high = float(np.sum(dev_psd[(freqs >= 4.0) & (freqs <= 10.0)]))

    extra.extend([
        sv1_ratio,
        sv12_ratio,
        sv_ent,
        peak_to_mean,
        p_low / (p_mid + 1e-5),
        p_mid / (p_high + 1e-5),
    ])

    all_feats = np.concatenate([base_feats, np.asarray(extra, dtype=np.float32)])
    return np.nan_to_num(all_feats, nan=0.0, posinf=0.0, neginf=0.0)

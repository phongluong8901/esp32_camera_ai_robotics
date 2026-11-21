"""Train and evaluate V1 (Standard) and V2 (Pro) Wi-Fi CSI Human Activity Recognition models."""

from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path
from typing import Any, Callable

import joblib
import numpy as np
from sklearn.ensemble import (
    ExtraTreesClassifier,
    HistGradientBoostingClassifier,
    RandomForestClassifier,
    VotingClassifier,
)
from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
from sklearn.neural_network import MLPClassifier
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import RobustScaler, StandardScaler

from csi_common import (
    STRIDE,
    STRIDE_V2,
    WINDOW_SIZE,
    extract_features,
    extract_features_v2,
)


def build_windows(
    amps: np.ndarray,
    rssi: np.ndarray,
    ts_ms: np.ndarray,
    win_size: int,
    stride: int,
    feat_fn: Callable[[np.ndarray, np.ndarray, np.ndarray], np.ndarray] = extract_features,
) -> list[np.ndarray]:
    rows: list[np.ndarray] = []
    n = len(amps)
    for start in range(0, n - win_size + 1, stride):
        end = start + win_size
        rows.append(feat_fn(amps[start:end], rssi[start:end], ts_ms[start:end]))
    return rows


def load_dataset(
    dataset_dir: Path,
    win_size: int,
    stride: int,
    feat_fn: Callable[[np.ndarray, np.ndarray, np.ndarray], np.ndarray] = extract_features,
    trim_edges: int = 0,
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray, list[str]]:
    files = sorted(dataset_dir.glob("*.npz"))
    if not files:
        raise FileNotFoundError(
            f"Khong tim thay file .npz nao trong {dataset_dir}. "
            "Hay chay collect.py hoac thu tren web truoc!"
        )

    x_train_list: list[np.ndarray] = []
    y_train_list: list[str] = []
    x_test_list: list[np.ndarray] = []
    y_test_list: list[str] = []
    rec_counts: Counter[str] = Counter()

    for f in files:
        with np.load(f, allow_pickle=False) as data:
            amps = data["amps"]
            rssi = data["rssi"]
            ts_ms = data["ts_ms"]
            label = str(data["label"].item())

        # In V2, trim transition packets at the very beginning/end of long recordings
        if trim_edges > 0 and len(amps) > (win_size * 2 + trim_edges * 2 + 20):
            amps = amps[trim_edges:-trim_edges]
            rssi = rssi[trim_edges:-trim_edges]
            ts_ms = ts_ms[trim_edges:-trim_edges]

        n = len(amps)
        if n < win_size * 2 + 20:
            print(f"[BO QUA] {f.name}: qua it mau ({n} goi)")
            continue

        # Time-contiguous split with a full WINDOW_SIZE gap so train/test windows never overlap
        split_idx = int(n * 0.75)
        test_start = split_idx + win_size
        if split_idx < win_size or (n - test_start) < win_size:
            split_idx = n - win_size * 2
            test_start = split_idx + win_size

        tr_wins = build_windows(
            amps[:split_idx], rssi[:split_idx], ts_ms[:split_idx], win_size, stride, feat_fn
        )
        te_wins = build_windows(
            amps[test_start:], rssi[test_start:], ts_ms[test_start:], win_size, stride, feat_fn
        )
        if not tr_wins or not te_wins:
            print(f"[BO QUA] {f.name}: khong du cua so train/test")
            continue

        x_train_list.extend(tr_wins)
        y_train_list.extend([label] * len(tr_wins))
        x_test_list.extend(te_wins)
        y_test_list.extend([label] * len(te_wins))
        rec_counts[label] += n

    if not x_train_list or not x_test_list:
        raise RuntimeError("Khong co du cua so hop le de train/test.")

    labels = sorted(set(y_train_list) | set(y_test_list))
    if len(labels) < 2:
        raise RuntimeError(f"Can it nhat 2 nhan khac nhau de train (hien co: {labels}).")

    print("Tong goi hop le theo tung nhan:", dict(rec_counts))
    return (
        np.vstack(x_train_list),
        np.asarray(y_train_list),
        np.vstack(x_test_list),
        np.asarray(y_test_list),
        labels,
    )


def build_candidates() -> dict[str, Any]:
    """[V1] Original candidate models."""
    return {
        "ExtraTrees": ExtraTreesClassifier(
            n_estimators=300,
            min_samples_leaf=2,
            class_weight="balanced",
            random_state=42,
            n_jobs=-1,
        ),
        "RandomForest": RandomForestClassifier(
            n_estimators=300,
            min_samples_leaf=2,
            class_weight="balanced",
            random_state=42,
            n_jobs=-1,
        ),
        "MLP": make_pipeline(
            StandardScaler(),
            MLPClassifier(
                hidden_layer_sizes=(64, 32),
                alpha=1e-2,
                max_iter=500,
                random_state=42,
            ),
        ),
    }


def build_candidates_v2() -> dict[str, Any]:
    """[V2 Pro] Denoised + multi-scale feature models including HistGB and Soft-Voting Ensemble."""
    et = ExtraTreesClassifier(
        n_estimators=400,
        max_features="sqrt",
        min_samples_leaf=1,
        class_weight="balanced_subsample",
        random_state=42,
        n_jobs=-1,
    )
    rf = RandomForestClassifier(
        n_estimators=400,
        max_features="sqrt",
        min_samples_leaf=1,
        class_weight="balanced_subsample",
        random_state=42,
        n_jobs=-1,
    )
    hgb = HistGradientBoostingClassifier(
        max_iter=200,
        learning_rate=0.06,
        max_leaf_nodes=31,
        min_samples_leaf=5,
        l2_regularization=0.5,
        random_state=42,
    )
    mlp = make_pipeline(
        RobustScaler(),
        MLPClassifier(
            hidden_layer_sizes=(128, 64),
            alpha=3e-2,
            max_iter=600,
            random_state=42,
        ),
    )
    ensemble = VotingClassifier(
        estimators=[("et", et), ("rf", rf), ("hgb", hgb), ("mlp", mlp)],
        voting="soft",
        weights=[2, 1, 2, 1],
    )
    return {
        "ExtraTrees_V2": et,
        "RandomForest_V2": rf,
        "HistGB_V2": hgb,
        "MLP_V2": mlp,
        "Ensemble_V2": ensemble,
    }


def print_confusion(cm: np.ndarray, labels: list[str]) -> None:
    col_w = max(max(len(lbl) for lbl in labels), 6) + 2
    header = " " * col_w + "".join(f"{lbl:>{col_w}}" for lbl in labels)
    print("\nConfusion Matrix (hang = thuc te, cot = du doan):")
    print(header)
    for lbl, row in zip(labels, cm):
        cells = "".join(f"{val:>{col_w}d}" for val in row)
        print(f"{lbl:<{col_w}}{cells}")


def run_training(dataset_dir: Path, out_path: Path, version: str = "v1") -> dict[str, Any]:
    ver = version.lower()
    is_v2 = ver == "v2"
    feat_fn = extract_features_v2 if is_v2 else extract_features
    stride = STRIDE_V2 if is_v2 else STRIDE
    trim = 12 if is_v2 else 0
    cand_fn = build_candidates_v2 if is_v2 else build_candidates

    print(f"\n==================== TRAIN {ver.upper()} ====================")
    x_tr, y_tr, x_te, y_te, labels = load_dataset(
        dataset_dir, WINDOW_SIZE, stride, feat_fn=feat_fn, trim_edges=trim
    )
    print(f"So dac trung: {x_tr.shape[1]} | Cua so Train: {len(x_tr)} | Cua so Test: {len(x_te)}")

    best_name = ""
    best_acc = -1.0
    best_pred = None

    print(f"\nDanh gia cac model [{ver.upper()}] tren tap Test:")
    for name, clf in cand_fn().items():
        clf.fit(x_tr, y_tr)
        pred = clf.predict(x_te)
        acc = float(accuracy_score(y_te, pred))
        print(f"  - {name:<16s}: Accuracy = {acc * 100:.2f}%")
        if acc > best_acc or (abs(acc - best_acc) < 1e-6 and "Ensemble" in name):
            best_acc = acc
            best_name = name
            best_pred = pred

    print(f"\n==> [{ver.upper()}] Model tot nhat: {best_name} (Test Accuracy = {best_acc * 100:.2f}%)")
    print(classification_report(y_te, best_pred, labels=labels, zero_division=0))
    cm = confusion_matrix(y_te, best_pred, labels=labels)
    print_confusion(cm, labels)

    per_class: dict[str, float] = {}
    for idx, lbl in enumerate(labels):
        row_sum = int(cm[idx].sum())
        per_class[str(lbl)] = round(float(cm[idx, idx]) / row_sum, 4) if row_sum > 0 else 0.0

    # Refit winning architecture on full dataset for best live performance
    x_all = np.vstack([x_tr, x_te])
    y_all = np.concatenate([y_tr, y_te])
    final_clf = cand_fn()[best_name]
    final_clf.fit(x_all, y_all)
    final_labels = [str(x) for x in final_clf.classes_]

    payload = {
        "model": final_clf,
        "version": ver,
        "labels": final_labels,
        "window_size": WINDOW_SIZE,
        "model_name": best_name,
        "test_acc": round(float(best_acc), 4),
        "per_class": per_class,
        "train_windows": int(len(x_tr)),
        "test_windows": int(len(x_te)),
    }
    joblib.dump(payload, out_path)
    print(f"Da luu model [{ver.upper()}] vao {out_path}")
    return payload


def main() -> None:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    ml_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description="Train model nhan dien hanh dong Wi-Fi CSI")
    parser.add_argument("--dataset", type=Path, default=ml_dir / "dataset")
    parser.add_argument(
        "--version",
        choices=["v1", "v2", "both"],
        default="both",
        help="Chon phien ban thuat toan train (v1, v2 hoac both de so sanh)",
    )
    args = parser.parse_args()

    if args.version in ("v1", "both"):
        res1 = run_training(args.dataset, ml_dir / "model.joblib", "v1")
    else:
        res1 = None

    if args.version in ("v2", "both"):
        res2 = run_training(args.dataset, ml_dir / "model_v2.joblib", "v2")
    else:
        res2 = None

    if res1 and res2:
        print("\n================ SO SANH V1 vs V2 (PRO) ================")
        print(f"  V1 (Cu)  : {res1['model_name']:<16s} -> Test Acc = {res1['test_acc'] * 100:.2f}%")
        print(f"  V2 (Pro) : {res2['model_name']:<16s} -> Test Acc = {res2['test_acc'] * 100:.2f}%")
        print("========================================================")


if __name__ == "__main__":
    main()

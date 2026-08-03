"""Unilateral S-parameter synthesis and Touchstone (.s2p) writing."""
from __future__ import annotations

from pathlib import Path
from typing import Sequence

FREQ_UNITS = {"Hz", "kHz", "MHz", "GHz"}


def db_to_linear(db: float) -> float:
    return 10.0 ** (db / 20.0)


def build_s2p_rows(
    gain_db_vs_freq: Sequence[tuple[float, float]],
    input_return_loss_db: float,
    output_return_loss_db: float,
) -> list[tuple[float, float, float, float, float]]:
    """Returns rows of (freq, s11_mag, s21_mag, s12_mag, s22_mag), 0 deg phase each."""
    s11 = db_to_linear(-abs(input_return_loss_db))
    s22 = db_to_linear(-abs(output_return_loss_db))
    rows = []
    for freq, gain_db in gain_db_vs_freq:
        rows.append((freq, s11, db_to_linear(gain_db), 0.0, s22))
    return rows


def write_s2p(
    path: Path,
    part_number: str,
    gain_db_vs_freq: Sequence[tuple[float, float]],
    freq_unit: str = "GHz",
    input_return_loss_db: float = 20.0,
    output_return_loss_db: float = 20.0,
) -> None:
    if freq_unit not in FREQ_UNITS:
        raise ValueError(f"unsupported freq_unit '{freq_unit}'")

    rows = build_s2p_rows(gain_db_vs_freq, input_return_loss_db, output_return_loss_db)
    lines = [
        f"! {part_number} - synthesized unilateral S-parameters (rf-sim amplifier-generator)",
        f"# {freq_unit.upper()} S MA R 50",
    ]
    for freq, s11, s21, s12, s22 in rows:
        lines.append(f"{freq:.6g} {s11:.6f} 0.0 {s21:.6f} 0.0 {s12:.6f} 0.0 {s22:.6f} 0.0")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")

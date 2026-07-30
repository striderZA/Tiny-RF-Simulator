"""Load and validate amplifier generator parameter files."""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from touchstone import FREQ_UNITS


class ParamsError(ValueError):
    """Raised when a parameters JSON file fails validation."""


REQUIRED_FIELDS = ("part_number", "manufacturer", "gain_db_vs_freq")


def load_params(path: Path) -> dict[str, Any]:
    """Load and validate a params JSON file. Raises ParamsError on any problem."""
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ParamsError(f"{path.name}: could not read/parse JSON ({exc})") from exc

    if not isinstance(raw, dict):
        raise ParamsError(f"{path.name}: root must be a JSON object")

    for field in REQUIRED_FIELDS:
        if field not in raw:
            raise ParamsError(f"{path.name}: missing required field '{field}'")

    part_number = raw["part_number"]
    manufacturer = raw["manufacturer"]
    if not isinstance(part_number, str) or not part_number.strip():
        raise ParamsError(f"{path.name}: 'part_number' must be a non-empty string")
    if not isinstance(manufacturer, str) or not manufacturer.strip():
        raise ParamsError(f"{path.name}: 'manufacturer' must be a non-empty string")

    gain_points = raw["gain_db_vs_freq"]
    if not isinstance(gain_points, list) or len(gain_points) < 2:
        raise ParamsError(
            f"{path.name}: 'gain_db_vs_freq' needs at least 2 [freq, gain_dB] points"
        )

    parsed_points: list[tuple[float, float]] = []
    for i, point in enumerate(gain_points):
        if not isinstance(point, list) or len(point) != 2:
            raise ParamsError(f"{path.name}: gain_db_vs_freq[{i}] must be a [freq, gain_dB] pair")
        freq, gain = point
        if not isinstance(freq, (int, float)) or not isinstance(gain, (int, float)):
            raise ParamsError(f"{path.name}: gain_db_vs_freq[{i}] values must be numeric")
        parsed_points.append((float(freq), float(gain)))

    for i in range(1, len(parsed_points)):
        if parsed_points[i][0] <= parsed_points[i - 1][0]:
            raise ParamsError(
                f"{path.name}: gain_db_vs_freq frequencies must be strictly increasing"
            )

    raw["gain_db_vs_freq"] = parsed_points
    raw.setdefault("freq_unit", "GHz")
    if raw["freq_unit"] not in FREQ_UNITS:
        raise ParamsError(f"{path.name}: unsupported freq_unit '{raw['freq_unit']}'")
    raw.setdefault("input_return_loss_db", 20.0)
    raw.setdefault("output_return_loss_db", 20.0)
    return raw

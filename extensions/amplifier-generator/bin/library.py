"""Schema-v2 component JSON writer for the amplifier generator."""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any

PARAM_FIELDS = ("nf_dB", "oip3_dBm", "p1db_dBm", "oip2_dBm", "gain_dB")


def build_definition(params: dict[str, Any], sparam_filename: str) -> dict[str, Any]:
    definition: dict[str, Any] = {
        "schema_version": 2,
        "type": "amplifier",
        "part_number": params["part_number"],
        "manufacturer": params["manufacturer"],
    }
    if params.get("description"):
        definition["description"] = params["description"]
    definition["parameters"] = {k: params[k] for k in PARAM_FIELDS if k in params}
    definition["data_files"] = [{"type": "s_parameters", "path": sparam_filename}]
    if params.get("notes"):
        definition["notes"] = params["notes"]
    return definition


def next_available_stem(directory: Path, part_number: str) -> str:
    """Returns a stem such that neither <stem>.json nor <stem>.s2p exists in directory."""
    candidate = part_number
    n = 2
    while (directory / f"{candidate}.json").exists() or (directory / f"{candidate}.s2p").exists():
        candidate = f"{part_number}-{n}"
        n += 1
    return candidate


def write_library_entry(library_root: Path, params: dict[str, Any]) -> tuple[Path, Path]:
    """Writes the schema-v2 JSON under library_root/<manufacturer>/ and reserves a
    matching .s2p path (not written here — caller writes the Touchstone data).
    Returns (json_path, sparam_path).
    """
    vendor_dir = library_root / params["manufacturer"]
    vendor_dir.mkdir(parents=True, exist_ok=True)
    stem = next_available_stem(vendor_dir, params["part_number"])
    json_path = vendor_dir / f"{stem}.json"
    sparam_path = vendor_dir / f"{stem}.s2p"
    definition = build_definition(params, sparam_path.name)
    json_path.write_text(json.dumps(definition, indent=2) + "\n", encoding="utf-8")
    return json_path, sparam_path

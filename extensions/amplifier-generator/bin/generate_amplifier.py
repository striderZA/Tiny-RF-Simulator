#!/usr/bin/env python3
"""Amplifier data-file generator external tool.

Dispatches on action_label:
  "Amplifier: New Params Template..." -> scaffold a params JSON template
  "Amplifier: Build from Params..."   -> synthesize .s2p + schema-v2 JSON
                                          for every pending params file
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from library import write_library_entry  # noqa: E402
from params import ParamsError, load_params  # noqa: E402
from touchstone import write_s2p  # noqa: E402

TEMPLATE_ACTION = "Amplifier: New Params Template..."
BUILD_ACTION = "Amplifier: Build from Params..."

TEMPLATE_CONTENT = {
    "schema_version": 1,
    "part_number": "REPLACE-ME",
    "manufacturer": "REPLACE-ME",
    "description": "",
    "freq_unit": "GHz",
    "gain_db_vs_freq": [[1.0, 20.0], [2.0, 20.0]],
    "nf_dB": 2.0,
    "oip3_dBm": 30.0,
    "p1db_dBm": 18.0,
    "oip2_dBm": 50.0,
    "input_return_loss_db": 20.0,
    "output_return_loss_db": 20.0,
    "notes": "Fill in real datasheet values, then run 'Amplifier: Build from Params...'",
}


def input_dir(project_root: Path) -> Path:
    return project_root / "rf-sim-generator-input" / "amplifier"


def library_dir(project_root: Path) -> Path:
    return project_root / "rf-sim-libraries" / "amplifiers"


def next_template_path(directory: Path) -> Path:
    directory.mkdir(parents=True, exist_ok=True)
    n = 1
    while True:
        candidate = directory / f"params-{n}.json"
        if not candidate.exists():
            return candidate
        n += 1

def next_processed_path(directory: Path, filename: str) -> Path:
    candidate = directory / filename
    if not candidate.exists():
        return candidate

    original = Path(filename)
    n = 2
    while True:
        candidate = directory / f"{original.stem}-{n}{original.suffix}"
        if not candidate.exists():
            return candidate
        n += 1



def run_template_action(project_root: Path) -> dict:
    target = next_template_path(input_dir(project_root))
    target.write_text(json.dumps(TEMPLATE_CONTENT, indent=2) + "\n", encoding="utf-8")
    return {
        "result_type": "template_created",
        "message": f"Template written to {target}",
    }


def run_build_action(project_root: Path) -> dict:
    directory = input_dir(project_root)
    directory.mkdir(parents=True, exist_ok=True)
    processed_dir = directory / "processed"

    pending = sorted(p for p in directory.glob("*.json") if p.is_file())

    built: list[str] = []
    skipped: list[str] = []

    for params_path in pending:
        try:
            params = load_params(params_path)
        except ParamsError as exc:
            skipped.append(str(exc))
            continue

        json_path, sparam_path = write_library_entry(library_dir(project_root), params)
        write_s2p(
            sparam_path,
            params["part_number"],
            params["gain_db_vs_freq"],
            freq_unit=params["freq_unit"],
            input_return_loss_db=params["input_return_loss_db"],
            output_return_loss_db=params["output_return_loss_db"],
        )

        processed_dir.mkdir(parents=True, exist_ok=True)
        params_path.rename(next_processed_path(processed_dir, params_path.name))
        built.append(json_path.name)

    if not pending:
        message = "No pending parameter files found in rf-sim-generator-input/amplifier/."
    else:
        message = f"Built {len(built)} part(s), skipped {len(skipped)}."
        if built:
            message += " Built: " + ", ".join(built) + "."
        if skipped:
            message += " Skipped: " + " | ".join(skipped)

    return {
        "result_type": "build_report",
        "message": message,
        "built": built,
        "skipped": skipped,
    }


def parse_args(argv: list[str]) -> tuple[Path, Path]:
    request_path = Path(argv[argv.index("--request") + 1])
    result_path = Path(argv[argv.index("--result") + 1])
    return request_path, result_path


def main() -> int:
    request_path, result_path = parse_args(sys.argv[1:])
    request = json.loads(request_path.read_text(encoding="utf-8"))
    action_label = request.get("action_label", "")
    project_root = Path(request["project_root"])

    if action_label == TEMPLATE_ACTION:
        result = run_template_action(project_root)
    elif action_label == BUILD_ACTION:
        result = run_build_action(project_root)
    else:
        sys.stderr.write(f"unknown action_label: {action_label!r}\n")
        return 1

    result_path.parent.mkdir(parents=True, exist_ok=True)
    result_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

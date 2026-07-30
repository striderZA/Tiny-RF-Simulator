import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "bin"))

from generate_amplifier import (  # noqa: E402
    BUILD_ACTION,
    TEMPLATE_ACTION,
    input_dir,
    library_dir,
    run_build_action,
    run_template_action,
)


class RunTemplateActionTests(unittest.TestCase):
    def test_writes_incrementing_template_files(self):
        with TemporaryDirectory() as tmp_str:
            project_root = Path(tmp_str)
            first = run_template_action(project_root)
            second = run_template_action(project_root)
            self.assertEqual(first["result_type"], "template_created")
            files = sorted(p.name for p in input_dir(project_root).glob("*.json"))
            self.assertEqual(files, ["params-1.json", "params-2.json"])
            self.assertNotEqual(first["message"], second["message"])


class RunBuildActionTests(unittest.TestCase):
    def write_params(self, project_root: Path, filename: str, content: dict) -> Path:
        directory = input_dir(project_root)
        directory.mkdir(parents=True, exist_ok=True)
        path = directory / filename
        path.write_text(json.dumps(content), encoding="utf-8")
        return path

    def test_no_pending_files_reports_zero(self):
        with TemporaryDirectory() as tmp_str:
            project_root = Path(tmp_str)
            result = run_build_action(project_root)
            self.assertEqual(result["built"], [])
            self.assertEqual(result["skipped"], [])
            self.assertIn("No pending", result["message"])

    def test_valid_file_builds_and_moves_to_processed(self):
        with TemporaryDirectory() as tmp_str:
            project_root = Path(tmp_str)
            self.write_params(project_root, "params-1.json", {
                "part_number": "AM1143",
                "manufacturer": "Anatech Electronics",
                "gain_db_vs_freq": [[1.0, 20.0], [2.0, 21.0]],
            })
            result = run_build_action(project_root)
            self.assertEqual(len(result["built"]), 1)
            self.assertEqual(result["skipped"], [])

            json_path = library_dir(project_root) / "Anatech Electronics" / "AM1143.json"
            sparam_path = library_dir(project_root) / "Anatech Electronics" / "AM1143.s2p"
            self.assertTrue(json_path.exists())
            self.assertTrue(sparam_path.exists())

            processed = input_dir(project_root) / "processed" / "params-1.json"
            self.assertTrue(processed.exists())
            self.assertFalse((input_dir(project_root) / "params-1.json").exists())

    def test_malformed_file_is_skipped_not_fatal(self):
        with TemporaryDirectory() as tmp_str:
            project_root = Path(tmp_str)
            self.write_params(project_root, "bad.json", {"part_number": "X"})
            result = run_build_action(project_root)
            self.assertEqual(result["built"], [])
            self.assertEqual(len(result["skipped"]), 1)
            # malformed file stays in place (not moved to processed/)
            self.assertTrue((input_dir(project_root) / "bad.json").exists())

    def test_rerun_without_cleanup_increments_instead_of_overwriting(self):
        with TemporaryDirectory() as tmp_str:
            project_root = Path(tmp_str)
            params = {
                "part_number": "AM1143",
                "manufacturer": "Anatech Electronics",
                "gain_db_vs_freq": [[1.0, 20.0], [2.0, 21.0]],
            }
            self.write_params(project_root, "params-1.json", params)
            run_build_action(project_root)
            self.write_params(project_root, "params-2.json", params)
            run_build_action(project_root)
            vendor_dir = library_dir(project_root) / "Anatech Electronics"
            names = sorted(p.name for p in vendor_dir.glob("*.json"))
            self.assertEqual(names, ["AM1143-2.json", "AM1143.json"])


if __name__ == "__main__":
    unittest.main()

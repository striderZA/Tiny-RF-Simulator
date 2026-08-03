import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "bin"))

from library import build_definition, next_available_stem, write_library_entry  # noqa: E402


class BuildDefinitionTests(unittest.TestCase):
    def test_includes_only_supplied_parameters(self):
        params = {
            "part_number": "AM1143",
            "manufacturer": "Anatech Electronics",
            "nf_dB": 1.5,
            "p1db_dBm": 20.0,
        }
        definition = build_definition(params, "AM1143.s2p")
        self.assertEqual(definition["schema_version"], 2)
        self.assertEqual(definition["type"], "amplifier")
        self.assertEqual(definition["parameters"], {"nf_dB": 1.5, "p1db_dBm": 20.0})
        self.assertEqual(
            definition["data_files"], [{"type": "s_parameters", "path": "AM1143.s2p"}]
        )
        self.assertNotIn("description", definition)

    def test_parameters_key_present_but_empty_when_no_optional_fields_supplied(self):
        params = {"part_number": "X", "manufacturer": "Y"}
        definition = build_definition(params, "X.s2p")
        self.assertIn("parameters", definition)
        self.assertEqual(definition["parameters"], {})


class NextAvailableStemTests(unittest.TestCase):
    def test_returns_bare_part_number_when_free(self):
        with TemporaryDirectory() as tmp_str:
            self.assertEqual(next_available_stem(Path(tmp_str), "AM1143"), "AM1143")

    def test_increments_when_json_exists(self):
        with TemporaryDirectory() as tmp_str:
            tmp = Path(tmp_str)
            (tmp / "AM1143.json").write_text("{}", encoding="utf-8")
            self.assertEqual(next_available_stem(tmp, "AM1143"), "AM1143-2")

    def test_increments_when_only_sparam_file_exists(self):
        with TemporaryDirectory() as tmp_str:
            tmp = Path(tmp_str)
            (tmp / "AM1143.s2p").write_text("", encoding="utf-8")
            self.assertEqual(next_available_stem(tmp, "AM1143"), "AM1143-2")


class WriteLibraryEntryTests(unittest.TestCase):
    def test_writes_json_under_manufacturer_subdir_and_reserves_sparam_path(self):
        with TemporaryDirectory() as tmp_str:
            library_root = Path(tmp_str) / "amplifiers"
            params = {"part_number": "AM1143", "manufacturer": "Anatech Electronics"}
            json_path, sparam_path = write_library_entry(library_root, params)
            self.assertTrue(json_path.exists())
            self.assertEqual(json_path.parent, library_root / "Anatech Electronics")
            self.assertEqual(sparam_path.name, "AM1143.s2p")
            self.assertFalse(sparam_path.exists())
            saved = json.loads(json_path.read_text(encoding="utf-8"))
            self.assertEqual(saved["part_number"], "AM1143")


if __name__ == "__main__":
    unittest.main()

import json
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "bin"))

from params import ParamsError, load_params  # noqa: E402


class LoadParamsTests(unittest.TestCase):
    def write(self, tmp: Path, content: dict) -> Path:
        path = tmp / "params.json"
        path.write_text(json.dumps(content), encoding="utf-8")
        return path

    def test_valid_params_round_trips_with_defaults(self):
        with TemporaryDirectory() as tmp_str:
            tmp = Path(tmp_str)
            path = self.write(tmp, {
                "part_number": "AM1143",
                "manufacturer": "Anatech Electronics",
                "gain_db_vs_freq": [[1.0, 20.0], [2.0, 21.0]],
            })
            result = load_params(path)
            self.assertEqual(result["part_number"], "AM1143")
            self.assertEqual(result["gain_db_vs_freq"], [(1.0, 20.0), (2.0, 21.0)])
            self.assertEqual(result["freq_unit"], "GHz")
            self.assertEqual(result["input_return_loss_db"], 20.0)
            self.assertEqual(result["output_return_loss_db"], 20.0)

    def test_missing_required_field_raises(self):
        with TemporaryDirectory() as tmp_str:
            tmp = Path(tmp_str)
            path = self.write(tmp, {
                "part_number": "AM1143",
                "gain_db_vs_freq": [[1.0, 20.0], [2.0, 21.0]],
            })
            with self.assertRaises(ParamsError):
                load_params(path)

    def test_too_few_gain_points_raises(self):
        with TemporaryDirectory() as tmp_str:
            tmp = Path(tmp_str)
            path = self.write(tmp, {
                "part_number": "AM1143",
                "manufacturer": "Anatech",
                "gain_db_vs_freq": [[1.0, 20.0]],
            })
            with self.assertRaises(ParamsError):
                load_params(path)

    def test_non_monotonic_frequency_raises(self):
        with TemporaryDirectory() as tmp_str:
            tmp = Path(tmp_str)
            path = self.write(tmp, {
                "part_number": "AM1143",
                "manufacturer": "Anatech",
                "gain_db_vs_freq": [[2.0, 20.0], [1.0, 21.0]],
            })
            with self.assertRaises(ParamsError):
                load_params(path)

    def test_invalid_json_raises(self):
        with TemporaryDirectory() as tmp_str:
            tmp = Path(tmp_str)
            path = tmp / "params.json"
            path.write_text("not json", encoding="utf-8")
            with self.assertRaises(ParamsError):
                load_params(path)


if __name__ == "__main__":
    unittest.main()

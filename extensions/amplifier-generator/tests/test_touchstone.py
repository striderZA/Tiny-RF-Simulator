import math
import sys
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "bin"))

from touchstone import build_s2p_rows, db_to_linear, write_s2p  # noqa: E402


class DbToLinearTests(unittest.TestCase):
    def test_zero_db_is_unity(self):
        self.assertAlmostEqual(db_to_linear(0.0), 1.0)

    def test_twenty_db_is_ten(self):
        self.assertAlmostEqual(db_to_linear(20.0), 10.0)

    def test_negative_twenty_db_is_tenth(self):
        self.assertAlmostEqual(db_to_linear(-20.0), 0.1)


class BuildS2pRowsTests(unittest.TestCase):
    def test_row_count_matches_input_points(self):
        rows = build_s2p_rows([(1.0, 20.0), (2.0, 21.0)], 20.0, 20.0)
        self.assertEqual(len(rows), 2)

    def test_s21_magnitude_matches_gain(self):
        rows = build_s2p_rows([(1.0, 20.0)], 20.0, 20.0)
        freq, s11, s21, s12, s22 = rows[0]
        self.assertAlmostEqual(freq, 1.0)
        self.assertAlmostEqual(s21, db_to_linear(20.0))

    def test_s11_s22_from_return_loss(self):
        rows = build_s2p_rows([(1.0, 20.0)], 15.0, 25.0)
        _, s11, _, s12, s22 = rows[0]
        self.assertAlmostEqual(s11, db_to_linear(-15.0))
        self.assertAlmostEqual(s22, db_to_linear(-25.0))
        self.assertEqual(s12, 0.0)


class WriteS2pTests(unittest.TestCase):
    def test_writes_header_and_one_row_per_point(self):
        with TemporaryDirectory() as tmp_str:
            path = Path(tmp_str) / "AM1143.s2p"
            write_s2p(path, "AM1143", [(1.0, 20.0), (2.0, 21.0)], freq_unit="GHz")
            text = path.read_text(encoding="utf-8")
            lines = [ln for ln in text.splitlines() if ln.strip()]
            self.assertTrue(lines[0].startswith("!"))
            self.assertEqual(lines[1], "# GHZ S MA R 50")
            data_lines = lines[2:]
            self.assertEqual(len(data_lines), 2)
            # freq s11 ang s21 ang s12 ang s22 ang = 9 tokens
            self.assertEqual(len(data_lines[0].split()), 9)

    def test_rejects_unsupported_freq_unit(self):
        with TemporaryDirectory() as tmp_str:
            path = Path(tmp_str) / "AM1143.s2p"
            with self.assertRaises(ValueError):
                write_s2p(path, "AM1143", [(1.0, 20.0), (2.0, 21.0)], freq_unit="THz")


if __name__ == "__main__":
    unittest.main()

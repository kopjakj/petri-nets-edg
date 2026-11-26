"""Small failure-detection tests for the new independent UART replay checker."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("verify_records", Path(__file__).with_name("verify_records.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
check_trace = module.check_trace


class ReplayTests(unittest.TestCase):
    def setUp(self):
        self.net = {"name": "sink", "m0": [1],
                    "T": [{"name": "consume", "pre": [0], "post": []}]}
        self.row = {"name": "sink", "polName": "grouped-rr", "seed": 1,
                    "qdepth": 2, "end": "dead", "fires": 1,
                    "counts": [1], "final": [0], "markings": ["1", "0"]}
        self.raw = ("0.001\tNET sink np=1 nt=1 POL grouped-rr SEED 1 JIT 1 Q 2\n"
                    "0.002\tT 0 consume pre 0 post\n"
                    "0.003\tM0 1\n0.004\tSTART\n0.005\tINIT_BEGIN\n"
                    "0.006\tINIT_READY\n0.007\tF 0\n").encode()

    def test_valid_sink(self):
        self.assertEqual(check_trace(self.raw, self.row, self.net), {(1,), (0,)})

    def test_double_spending_rejected(self):
        with self.assertRaisesRegex(ValueError, "Illegal grant"):
            check_trace(self.raw + b"0.008\tF 0\n", self.row, self.net)

    def test_anomaly_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unexpected UART"):
            check_trace(self.raw + b"0.008\t!! choice error\n", self.row, self.net)

    def test_topology_change_rejected(self):
        with self.assertRaisesRegex(ValueError, "Declared topology"):
            check_trace(self.raw.replace(b"pre 0 post", b"pre 0 post 0"), self.row, self.net)

    def test_incomplete_initialization_rejected(self):
        with self.assertRaisesRegex(ValueError, "Initialization"):
            check_trace(self.raw.replace(b"0.006\tINIT_READY\n", b""), self.row, self.net)

    def test_fake_deadlock_rejected(self):
        row = copy.deepcopy(self.row)
        row.update(fires=0, counts=[0], final=[1], markings=["1"])
        with self.assertRaisesRegex(ValueError, "end classification"):
            check_trace(self.raw.replace(b"0.007\tF 0\n", b""), row, self.net)


if __name__ == "__main__":
    unittest.main()

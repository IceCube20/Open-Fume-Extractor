import csv
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def layout(path):
    with path.open() as source:
        return {row[0].strip(): (row[1].strip(), row[2].strip(), int(row[3].strip(), 0), int(row[4].strip(), 0))
                for row in csv.reader(line for line in source if line.strip() and not line.lstrip().startswith('#'))}


class P4PartitionTests(unittest.TestCase):
    def test_layouts_have_two_five_mib_slots_and_keep_nvs(self):
        original = layout(ROOT/'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/partitions.csv')
        for mib in (16, 32):
            with self.subTest(flash=mib):
                parts = layout(ROOT/f'tools/boards/master_p4_{mib}mb.csv')
                self.assertEqual(parts['nvs'], original['nvs'])
                self.assertEqual(parts['app0'][2], 0x10000)
                for name in ('app0', 'app1'):
                    self.assertEqual(parts[name][3], 5*1024*1024)
                    self.assertEqual(parts[name][2] % 0x10000, 0)
                regions = sorted((p[2], p[2]+p[3]) for p in parts.values())
                self.assertTrue(all(start >= previous_end for (_, previous_end), (start, _) in zip(regions, regions[1:])))
                self.assertEqual(regions[-1][1], mib*1024*1024)
                self.assertEqual(parts['otadata'], original['otadata'])
                self.assertNotEqual(parts['spiffs'][2], original['spiffs'][2])

    def test_s3_stays_unchanged(self):
        original = layout(ROOT/'OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/partitions.csv')
        self.assertEqual(original['app0'][3], 0x330000)
        self.assertEqual(original['app1'][3], 0x330000)
        self.assertEqual(original['spiffs'][2:], (0x6B0000, 0x940000))


if __name__ == '__main__':
    unittest.main()

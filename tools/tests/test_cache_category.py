"""The sidebar patch must preserve vanilla categories and reject unknown shapes."""
import sys
import struct
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from build_lineages_cache_category import CATEGORY, LOCALE, array, cache_records, extend_root, key, props, text, fnv
from part_substitutes import parse


class CacheCategoryTest(unittest.TestCase):
    def test_shipping_resources_do_not_override_vanilla_roots(self):
        records = cache_records()
        self.assertEqual(len(records), 3)
        self.assertFalse(any(name.startswith('0x6EDC12D4!') for name in records))
        locale = records[f'0x02FABF01!0x{LOCALE:08X}.0x02FAC0B6']
        self.assertTrue(locale.startswith(b'\xef\xbb\xbf'))
        self.assertIn(b'Lineages Cache', locale)
        self.assertIn(b'Shared Creations', locale)

    def test_label_references_bundled_locale(self):
        field = parse(props(text(0x744717C2, 'Lineages Cache')))[0x744717C2]
        self.assertEqual(struct.unpack('<II', field[2][:8]), (LOCALE, fnv('Lineages Cache')))
        self.assertEqual(field[2][8:].decode('utf-16le').rstrip('\0'), 'Lineages Cache')

    def test_preserves_categories_and_does_not_duplicate(self):
        original = props(array(0x744717C0, 0x20, [key(17), key(29)], 12))
        patched = extend_root(original)
        fields = parse(patched)
        self.assertEqual(fields[0x744717C0][2], key(17) + key(29) + key(CATEGORY))
        self.assertEqual(extend_root(patched), patched)

    def test_unknown_root_fields_are_not_silently_discarded(self):
        unknown = props(array(0x744717C0, 0x20, [], 12), array(1234, 0x20, [], 12))
        with self.assertRaises(ValueError):
            extend_root(unknown)

    def test_rejects_wrong_key_stride(self):
        malformed = props(array(0x744717C0, 0x20, [b'1234'], 4))
        with self.assertRaises(ValueError):
            extend_root(malformed)


if __name__ == '__main__':
    unittest.main()

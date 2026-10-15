import unittest

from gen_chinese_font import CHARS, generate_font_data, generate_header


def edge_pixels_are_clear(data, size):
    bytes_per_row = (size + 7) // 8

    for x in range(size):
        mask = 0x80 >> (x % 8)
        byte_index = x // 8
        if data[byte_index] & mask:
            return False
        if data[(size - 1) * bytes_per_row + byte_index] & mask:
            return False

    for y in range(size):
        row = y * bytes_per_row
        if data[row] & 0x80:
            return False
        if data[row + bytes_per_row - 1] & (0x80 >> ((size - 1) % 8)):
            return False

    return True


class ChineseFontGenerationTests(unittest.TestCase):
    def test_generates_small_and_large_glyphs_with_padding(self):
        fonts = generate_font_data()

        self.assertIn(16, fonts)
        self.assertIn(24, fonts)
        self.assertIn(48, fonts)
        self.assertEqual(32, len(fonts[16]["底"]))
        self.assertEqual(72, len(fonts[24]["底"]))
        self.assertEqual(288, len(fonts[48]["底"]))
        self.assertTrue(edge_pixels_are_clear(fonts[16]["底"], 16))
        self.assertTrue(edge_pixels_are_clear(fonts[24]["底"], 24))
        self.assertTrue(edge_pixels_are_clear(fonts[48]["底"], 48))

    def test_header_declares_every_unicode_character(self):
        header = generate_header(generate_font_data())

        self.assertIn(f"#define CHINESE_CHAR_COUNT {len(CHARS)}", header)

    def test_generated_font_contains_dashboard_input_character(self):
        fonts = generate_font_data()

        for size in (16, 24, 48):
            self.assertIn("入", fonts[size])

    def test_bench_labels_have_nonempty_padded_glyphs(self):
        fonts = generate_font_data()

        for size in (16, 24, 48):
            for char in set("遥控联调模拟禁用"):
                with self.subTest(size=size, char=char):
                    self.assertIn(char, tuple(fonts[size]))
                    self.assertTrue(any(fonts[size][char]))
                    self.assertTrue(edge_pixels_are_clear(fonts[size][char], size))


if __name__ == "__main__":
    unittest.main()

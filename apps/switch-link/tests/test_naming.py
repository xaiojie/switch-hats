import unittest

from switch_link import naming


class NamingTest(unittest.TestCase):
    def test_ascii_names_are_untouched(self):
        for name in ('plain.txt', 'Album 2026 - 01.mp3', 'a.b.c'):
            self.assertEqual(naming.encode_component(name), name)
            self.assertEqual(naming.decode_component(name), name)

    def test_chinese_name_is_percent_encoded_on_the_card(self):
        self.assertEqual(naming.encode_component('中文.txt'), '%E4%B8%AD%E6%96%87.txt')
        self.assertEqual(naming.encode_component('周杰伦 - 七里香.mp3'),
                         '%E5%91%A8%E6%9D%B0%E4%BC%A6 - %E4%B8%83%E9%87%8C%E9%A6%99.mp3')

    def test_round_trip(self):
        for name in ('中文.txt', '周杰伦 - 七里香.mp3', '日本語', 'café.txt', '中.mp3'):
            encoded = naming.encode_component(name)
            self.assertTrue(encoded.isascii(), encoded)
            self.assertEqual(naming.decode_component(encoded), name)

    def test_literal_percent_names_are_not_mangled(self):
        # 卡上本来就叫 "50%25.txt" 的文件不应该被翻译成 "50%.txt"
        self.assertEqual(naming.encode_component('50%25.txt'), '50%25.txt')
        self.assertEqual(naming.decode_component('50%25.txt'), '50%25.txt')
        self.assertEqual(naming.decode_component('100%.txt'), '100%.txt')

    def test_broken_escape_is_left_alone(self):
        self.assertEqual(naming.decode_component('%zz.txt'), '%zz.txt')
        self.assertEqual(naming.decode_component('%E4%B8'), '%E4%B8')

    def test_paths_encode_per_component(self):
        self.assertEqual(naming.encode_path('/music/中文/歌.mp3'),
                         '/music/%E4%B8%AD%E6%96%87/%E6%AD%8C.mp3')
        self.assertEqual(naming.decode_path('/music/%E4%B8%AD%E6%96%87/%E6%AD%8C.mp3'),
                         '/music/中文/歌.mp3')

    def test_too_long_encoded_name_is_rejected(self):
        with self.assertRaises(naming.NameTooLong):
            naming.storage_component('中' * 80)


if __name__ == '__main__':
    unittest.main()

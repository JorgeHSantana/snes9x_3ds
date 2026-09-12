import os
import tempfile
import unittest

import frame_probe


class CaptureCompleteTests(unittest.TestCase):
    def test_rejects_missing_and_partially_written_png(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "capture.png")
            self.assertFalse(frame_probe.capture_complete(path))
            with open(path, "wb") as capture:
                capture.write(b"\x89PNG\r\n\x1a\npartial")
            self.assertFalse(frame_probe.capture_complete(path))

    def test_accepts_png_only_after_iend_chunk(self):
        with tempfile.TemporaryDirectory() as directory:
            path = os.path.join(directory, "capture.png")
            with open(path, "wb") as capture:
                capture.write(b"\x89PNG\r\n\x1a\n")
                capture.write(b"\x00\x00\x00\x00IEND\xaeB\x60\x82")
            self.assertTrue(frame_probe.capture_complete(path))


if __name__ == "__main__":
    unittest.main()

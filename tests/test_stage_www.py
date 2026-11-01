import gzip
import importlib.util
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "stage_www.py"
spec = importlib.util.spec_from_file_location("stage_www", SCRIPT)
stage_www = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage_www)


class StageWwwTest(unittest.TestCase):
    def test_compresses_text_assets_and_copies_the_rest(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "src"
            staging = Path(tmp) / "staging"
            (source / "www" / "js").mkdir(parents=True)
            (source / "www" / "index.html").write_text("<html></html>")
            (source / "www" / "js" / "app.js").write_text("console.log('hi');")
            (source / "www" / "logo.webp").write_bytes(b"\x00\x01binary")

            stage_www.stage(source, staging)

            self.assertEqual(gzip.decompress((staging / "www" / "index.html.gz").read_bytes()), b"<html></html>")
            self.assertEqual(gzip.decompress((staging / "www" / "js" / "app.js.gz").read_bytes()), b"console.log('hi');")
            self.assertEqual((staging / "www" / "logo.webp").read_bytes(), b"\x00\x01binary")
            self.assertFalse((staging / "www" / "index.html").exists())

    def test_output_is_reproducible_and_replaces_stale_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "src"
            staging = Path(tmp) / "staging"
            source.mkdir()
            (source / "a.css").write_text("body{}")

            stage_www.stage(source, staging)
            first = (staging / "a.css.gz").read_bytes()
            (staging / "stale.txt").write_text("old")
            stage_www.stage(source, staging)

            self.assertEqual((staging / "a.css.gz").read_bytes(), first)
            self.assertFalse((staging / "stale.txt").exists())


if __name__ == "__main__":
    unittest.main()

import base64
import importlib.util
from pathlib import Path
import tempfile
import unittest
import wave

spec=importlib.util.spec_from_file_location('capture_audio',Path(__file__).resolve().parents[1]/'tools/capture_audio.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)

class Protocol(unittest.TestCase):
    def test_exact_samples_and_interleaved_metrics(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/'game.wav';cap=module.Capture(output)
            cap.accept('CAPTURE_BEGIN rate=11025 bits=16 baud=460800 duration_ms=40000')
            pcm=b'\x00\x80\xff\x7f\x01\x00\xff\xff'
            cap.accept('CAPTURE_ROW 0 '+base64.b64encode(pcm[:4]).decode())
            cap.accept('AUTO t=10 listening->bite-wait')
            cap.accept('CAPTURE_ROW 2 '+base64.b64encode(pcm[4:]).decode())
            self.assertTrue(cap.accept('CAPTURE_END samples=4'))
            with wave.open(str(output)) as wav:
                self.assertEqual(wav.getframerate(),11025)
                self.assertEqual(wav.readframes(4),pcm)
    def test_missing_packet_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            cap=module.Capture(Path(folder)/'game.wav')
            cap.accept('CAPTURE_BEGIN rate=11025 bits=16 baud=460800 duration_ms=40000')
            with self.assertRaises(ValueError): cap.accept('CAPTURE_ROW 256 AAA=')
            cap.close()
            self.assertFalse(cap.output.exists())
    def test_explicit_prefix_gap_without_padding(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/'game.wav';cap=module.Capture(output,True)
            cap.accept('CAPTURE_BEGIN rate=11025 bits=16 baud=460800 duration_ms=40000')
            cap.accept('CAPTURE_ROW 256 AAA=')
            self.assertEqual(cap.start_offset,256)
            self.assertTrue(cap.accept('CAPTURE_END samples=257'))
            with wave.open(str(output)) as wav: self.assertEqual(wav.getnframes(),1)
    def test_prefix_permission_does_not_allow_middle_gap(self):
        with tempfile.TemporaryDirectory() as folder:
            cap=module.Capture(Path(folder)/'game.wav',True)
            cap.accept('CAPTURE_BEGIN rate=11025 bits=16 baud=460800 duration_ms=40000')
            cap.accept('CAPTURE_ROW 256 AAA=')
            with self.assertRaises(ValueError): cap.accept('CAPTURE_ROW 258 AAA=')
            cap.close()
    def test_two_packet_prefix_gap_without_padding(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/'game.wav';cap=module.Capture(output,True)
            cap.accept('CAPTURE_BEGIN rate=11025 bits=16 baud=460800 duration_ms=40000')
            cap.accept('CAPTURE_ROW 512 AAA=')
            self.assertEqual(cap.start_offset,512)
            with self.assertRaises(ValueError): cap.accept('CAPTURE_ROW 514 AAA=')
            cap.accept('CAPTURE_ROW 513 AAA=')
            self.assertTrue(cap.accept('CAPTURE_END samples=514'))
            with wave.open(str(output)) as wav: self.assertEqual(wav.getnframes(),2)
    def test_larger_prefix_still_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            cap=module.Capture(Path(folder)/'game.wav',True)
            cap.accept('CAPTURE_BEGIN rate=11025 bits=16 baud=460800 duration_ms=40000')
            with self.assertRaises(ValueError): cap.accept('CAPTURE_ROW 768 AAA=')
            cap.close()
    def test_incomplete_end_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            cap=module.Capture(Path(folder)/'game.wav')
            cap.accept('CAPTURE_BEGIN rate=11025 bits=16 baud=460800 duration_ms=40000')
            cap.accept('CAPTURE_ROW 0 AAA=')
            with self.assertRaises(ValueError): cap.accept('CAPTURE_END samples=256')
            cap.close()
            self.assertFalse(cap.output.exists())

if __name__=='__main__':unittest.main()

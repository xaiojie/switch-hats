"""Generate original two-second sine tones for decoder tests (requires lameenc)."""
import array
import math
import pathlib
import sys
import lameenc

out = pathlib.Path(sys.argv[1])
out.parent.mkdir(parents=True, exist_ok=True)
pcm = array.array('h')
for n in range(44100 * 2):
    pcm.extend((int(9000 * math.sin(n * 2 * math.pi * 440 / 44100)),) * 2)
encoder = lameenc.Encoder()
encoder.set_bit_rate(128)
encoder.set_in_sample_rate(44100)
encoder.set_channels(2)
encoder.set_quality(2)
out.write_bytes(encoder.encode(pcm.tobytes()) + encoder.flush())
print(f'Generated original test tone: {out} ({out.stat().st_size} bytes)')

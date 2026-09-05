# VapourSynth — SVP RIFE / VapourSynth Filter path.
# Append after SmoothFps / RIFE. Input is usually YUV420P8.

import vapoursynth as vs
core = vs.core

# `clip` is whatever SVP just produced.
clip = core.resize.Bicubic(clip, format=vs.RGB24, matrix_in_s="709")
clip = core.dlss5.NR(clip, style=1, preset=0, intensity=1.0, tone=1.0, structure=1.5, skin=2.0, automask=1)
clip = core.resize.Bicubic(clip, format=vs.YUV420P8, matrix_s="709")
clip.set_output()

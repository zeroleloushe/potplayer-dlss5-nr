# Paste AFTER SVP interpolation (SVSmoothFps / MBlockFps / InterFrame).
# AviSynth Filter already inserted AvsFilterSource() + SVP for you.
#
# Example generated script ends with:
#   SVSmoothFps(...)
# Append:

ConvertToRGB32()
DLSS5NR(style=1, preset=0, intensity=1.0, tone=1.0, structure=1.5, skin=2.0, automask=true)
# ConvertToYV12()  # uncomment if the renderer rejects RGB32

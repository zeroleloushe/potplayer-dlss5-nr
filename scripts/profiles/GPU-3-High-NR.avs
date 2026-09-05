# GPU-3-High + DLSS 5 NR. SVP interpolates to 60 after NR on source fps.
# Best on 1080p 24fps. Skip this on 4K if it stutters — use GPU-3-High or GPU-NR.

svp=3		# 1 - low | 2 - medium (default) | 3 - high
alg=3		# 1 - simple | 2 - standard (default) | 3 - complex
fim=3		# 1 - average | 2 - adaptive (default) | 3 - uniform
bf=0		# 0 - off (default) | 1 - blending frames
gpu=1		# 0 - off (default) | 1 - first gpu | 2 - second gpu
fps=60		# 60 (default) | 2 - x2 fps | 50 ... 120 - target fps

sh=0		# 0 - off (default) | 1 - avisynth sharpness
uhd=1		# 0 - off (default) | 1 - ultra hd video support
up60=0		# 0 - off (default) | 1 - source support up to 60 fps
demo=0		# 0 - off (default) | 1 - svp demo mode
nr=1		# 0 - off | 1 - DLSS 5 Neural Rendering
smooth=1	# 0 - no interpolation | 1 - SVP to fps=

import("svp.avs")

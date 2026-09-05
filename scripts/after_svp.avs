# Portable SVP packs (zub35/7sh3) do NOT autoload plugins.
# 1. Copy DLSS5NR.dll + nvngx.dll_pot.dll next to svpflow1.dll
# 2. Next to the other LoadPlugin lines:
#      LoadPlugin("DLSS5NR.dll")
#      SetFilterMTMode("DLSS5NR", 3)
# 3. MUST sit BEFORE Prefetch(), not after. Prefetch-then-filter = Access Violation.

ConvertToRGB32()
DLSS5NR(style=1, preset=0, intensity=1.0, tone=1.0, structure=1.5, skin=2.0, automask=true)
# ConvertToYV12()
Prefetch(threads)

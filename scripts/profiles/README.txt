Copy ALL of these into:
  C:\Users\leloushe\PotPlayer\AviSynth\

Files:
  svp.avs              <- replace the existing one (required)
  GPU-3-High.avs       <- replace: SVP only, 60 fps (no DLSS)
  GPU-NR.avs           <- new: DLSS 5 only, no interpolation
  GPU-3-High-NR.avs    <- new: DLSS 5 + SVP to 60
  CPU-3-High-NR.avs    <- new: same but SVP on CPU (if GPU fights NR)

In PotPlayer pick the script the same way you pick GPU-3-High today.

Which one to use:
  1080p 24fps movie     GPU-3-High-NR
  4K / 60fps / stutter  GPU-3-High  (SVP only)  or  GPU-NR  (DLSS only)
  want original 24fps   GPU-NR

# libnoise 1.0

Coherent noise library by Jason Bevins, LGPL 2.1 (`COPYING.txt`). Used by the fuzzy skin noise types
(Perlin, Billow, Ridged multifractal, Voronoi), as in OrcaSlicer.

Source: OrcaSlicer's dependency package https://github.com/SoftFever/Orca-deps-libnoise/archive/refs/tags/1.0.zip
(SHA256 96ffd6cc47898dd8147aab53d7d1b1911b507d9dbaecd5613ca2649468afd8b6), which is libnoise 1.0 from
https://sourceforge.net/projects/libnoise/ without its `doc` directory. Copied here unchanged: `src/` as `libnoise/`
(so the headers are included as `<libnoise/...>`), without the Win32 DLL files.

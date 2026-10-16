# Science notes

The derivations, measured values, design rationale and references behind TerrainGen's models. The
headers state what each function and type does; where a model rests on the literature they point
here by section name, as in `See SCIENCE.md, "Crater datums and blends"`. Sections follow the
library's modules.

## Util

### Surface cells

`SurfaceCells` partitions a surface into cells so features can be generated per cell, each from its
own random substream, and any region regenerated identically regardless of how it is tiled. Each
surface type supplies its own cells: square cells on a plane now, near-equal-area cells on
cube-sphere faces or shape models later. Feature generators use only this interface, so they do
not depend on the surface's geometry.

### Cube-sphere cells

`CubeSphereCells` splits the six faces of a cube into n x n cells along equiangular coordinates
(Ronchi et al. 1996), the angle from the face's centre along each axis, so cells are near squares
of about the same area.

For a cell side L, n = ceil(R pi / (2 L)), so cells are at most L across at a face's centre and a
little smaller towards its corners. Cell (i, j) is column i mod n and row j of face i / n (+x, -x,
+y, -y, +z, -z). Areas are the exact solid angles of the cells, and `SamplePoint` maps uniform
(u, v) to points uniform over the cell, so features are drawn with the right density everywhere.

`CellsCovering` returns every cell reaching into the region and some next to them: the cells of
points sampled over the region a quarter cell apart, with their `Neighbours` one ring out.
`Neighbours` continues past a face's edge by taking the cells of points spread over each step past
the edge on the face's extended plane.

Reference:
- C. Ronchi, R. Iacono and P. S. Paolucci, "The 'Cubed Sphere': a new method for the solution of
  partial differential equations in spherical geometry", Journal of Computational Physics
  124:93-114, 1996. https://doi.org/10.1006/jcph.1996.0047

### Surfaces

`Surface` is the shape a terrain lies on: a plane, or a sphere such as a planet or moon. It says
what surface coordinates mean and measures distances and directions between them, so features
defined in a local frame around their centre (crater profiles, boulders, rays) work on either.

- On a plane, surface coordinates are metres in a projected frame, and the local frame of a point
  is that frame shifted to it.
- On a sphere of radius R, surface coordinates are longitude and latitude in degrees (east and
  north positive), as a geographic raster's transform gives them, and the local frame of a point is
  its azimuthal equidistant projection: east and north in metres, with the distance and azimuth
  from the point exact.

It is a value type rather than a class hierarchy, so the plane path stays inline in inner loops.

### LRU cache sharding

`LruCache` spreads keys over 16 shards, each with its own lock and an even share of the capacity,
so threads working on different keys rarely wait for one another. A shard always keeps its newest
value, so values larger than a sixteenth of the capacity can exceed it by up to one value per
shard. A value is computed outside the lock; if two threads compute the same key at once, both get
the first value stored.

### Exponential integral

`ExponentialIntegral` evaluates E_n(x) by its power series for 0 < x <= 1 and by its continued
fraction, evaluated with Lentz's method, for x > 1.

Reference:
- W. H. Press, S. A. Teukolsky, W. T. Vetterling and B. P. Flannery, "Numerical Recipes", 3rd ed.,
  Cambridge University Press, 2007, section 6.3.

## Raster

### Gaussian blur

`GaussianBlur` truncates the kernel at three standard deviations and applies it separably along
rows and columns. Cells holding the raster's no-data value keep it and do not contribute to their
neighbours: the kernel is renormalized over valid cells (normalized convolution), which also keeps
the edges of the raster from being pulled towards zero.

Gaussian smoothing of a surface with standard deviation sigma is the solution of linear diffusion
of that surface for time t with diffusivity kappa where sigma^2 = 2 kappa t, the model of
hillslope and crater degradation by Culling (1960) and Fassett and Thomson (2014).

References:
- H. Knutsson and C.-F. Westin, "Normalized and differential convolution", Proc. IEEE Conference
  on Computer Vision and Pattern Recognition, 515-523, 1993.
  https://doi.org/10.1109/CVPR.1993.341081
- W. E. H. Culling, "Analytical theory of erosion", Journal of Geology 68(3):336-344, 1960.
  https://doi.org/10.1086/626663

### Cubic convolution

`ResampleMethod::Cubic` is Keys cubic convolution with a = -0.5 over the sixteen surrounding
cells.

Reference: R. Keys, "Cubic convolution interpolation for digital image processing", IEEE
Transactions on Acoustics, Speech, and Signal Processing 29(6):1153-1160, 1981.
https://doi.org/10.1109/TASSP.1981.1163711

### GeoTIFF projections

TerrainGen does not interpret projections: a raster read from a GeoTIFF keeps its CRS by carrying
its raw GeoKey tags (`GeoKeys`) to the GeoTIFF it is written to, while the pixel-to-world
transform travels in the raster's `GeoTransform`. Reprojection needs a GDAL-backed reader.

`GeographicSphereKeys` describes longitude and latitude in degrees on a sphere, such as a planet
or moon: a user-defined geographic CRS (GTModelTypeGeoKey 2, GeographicTypeGeoKey,
GeogGeodeticDatumGeoKey and GeogEllipsoidGeoKey 32767), angular unit degree (9102), and
semi-major and semi-minor axes both the radius, with pixel-is-area rasters (sections 7.4 and 7.5
of the standard).

With PixelIsPoint, the reader shifts the origin half a pixel so it names the corner of the first
pixel, as GDAL does, and the writer shifts it back.

References:
- OGC GeoTIFF Standard 1.1, OGC 19-008r4, 2019.
  https://docs.ogc.org/is/19-008r4/19-008r4.html
- GDAL GTiff driver: GDAL_NODATA tag, GDAL_METADATA band SCALE and OFFSET, and PixelIsPoint
  handling. https://gdal.org/en/stable/drivers/raster/gtiff.html

### TIFF reading and writing

The reader handles classic TIFF and BigTIFF in either byte order, and reads only the first image,
which is the full-resolution image of a cloud-optimized GeoTIFF. It supports strips and tiles,
chunky and planar band layouts, 8- to 64-bit integer and 32- and 64-bit float samples, no
compression, LZW, Deflate and PackBits, and horizontal and floating-point predictors. Other
compressions, such as JPEG, ZSTD and LERC, are reported as unsupported; `gdal_translate -co
COMPRESS=DEFLATE` converts them.

The writer writes tiled, little-endian float32 files; compressed files use the floating-point
predictor, which suits elevation data.

LZW follows TIFF 6.0 section 13: MSB-first 9- to 12-bit codes, Clear 256, EndOfInfo 257, and the
code width increasing one code early. Old-style (LSB-first) LZW is not supported. Deflate data is
zlib-wrapped (TIFF compression 8 and 32946) and decoded and encoded with miniz. PackBits follows
TIFF 6.0 section 9.

References:
- Aldus Corporation, "TIFF Revision 6.0", 1992.
  https://www.itu.int/itudoc/itu-t/com16/tiff-fx/docs/tiff6.pdf
- BigTIFF file format, LibTIFF documentation.
  https://libtiff.gitlab.io/libtiff/specification/bigtiff.html
- Adobe, "Adobe Photoshop TIFF Technical Note 3", 2005 (floating-point predictor).
  http://chriscox.org/TIFFTN3d1.pdf
- T. A. Welch, "A Technique for High-Performance Data Compression", Computer 17(6):8-19, 1984.
  https://doi.org/10.1109/MC.1984.1659158
- P. Deutsch, "DEFLATE Compressed Data Format Specification version 1.3", RFC 1951, 1996.
  https://www.rfc-editor.org/rfc/rfc1951
- P. Deutsch and J.-L. Gailly, "ZLIB Compressed Data Format Specification version 3.3",
  RFC 1950, 1996. https://www.rfc-editor.org/rfc/rfc1950
- Adobe, "TIFF Specification Supplement 2", 2002 (Deflate in TIFF).

## Noise

### Hashing

`PcgHash` is a PCG-RXS-M-XS output permutation applied to one LCG step. `Pcg3dHash` maps three
32-bit words to three well-mixed words. Both are stateless and integer-only, so they give
bit-identical results on CPUs, GPUs and WebAssembly. Noise kernels hash lattice coordinates and the
seed with `pcg3d` instead of a permutation table, so any seed works without tables and the kernels
port directly to GPU shading languages. `HashToUnitFloat` keeps the top 24 bits of a hash, which a
`float` represents exactly.

References:
- M. E. O'Neill, "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for
  Random Number Generation", Harvey Mudd College, HMC-CS-2014-0905, 2014.
- M. Jarzynski and M. Olano, "Hash Functions for GPU Rendering", Journal of Computer Graphics
  Techniques 9(3), 2020 (the `pcg` and `pcg3d` hashes). https://jcgt.org/published/0009/03/02/

### Counter-based random numbers

`Philox4x32` is Philox4x32-10, a counter-based generator: every output is a bijection of its
counter under the key, so any element of any stream can be computed directly, in any order, on any
thread or tile. It passes TestU01 BigCrush. It is integer-only, so results are bit-identical on
every platform and GPU; GPUs without 64-bit integers form the 64-bit products from 16-bit halves,
with the same result.

Reference: J. K. Salmon, M. A. Moraes, R. O. Dror and D. E. Shaw, "Parallel Random Numbers: As
Easy as 1, 2, 3", Proc. SC '11, 2011. https://doi.org/10.1145/2063384.2063405 (Random123).

### Perlin noise

Gradient noise (Perlin 1985) interpolates the dot products of pseudo-random unit gradients at the
cell corners with the offsets to those corners, using the quintic fade 6t^5 - 15t^4 + 10t^3 of
Perlin (2002). The fade has zero first and second derivatives at t = 0 and t = 1, so noise is C^2
continuous across cell edges. Perlin's permutation table is replaced by a stateless `pcg3d` hash of
(cell, seed) (Jarzynski and Olano 2020); see "Hashing".

Perlin (2002) replaced random gradients with a small fixed set to avoid clumping. In 3D these are
his twelve gradients, the vectors from the centre of a cube to the midpoints of its edges, chosen
by the low four bits of the hash with four of them repeated to make sixteen, as in his reference
implementation. In 2D the analogue is eight unit gradients: the four axis directions and the four
diagonals, normalized so every gradient has unit length.

Noise is zero at every lattice point. In 2D, with unit gradients, its magnitude is at most
sqrt(2)/2, reached at cell centres, so the result is scaled by sqrt(2) to span [-1, 1]. In 3D, over
2e7 random samples it stays within 0.9995 of zero, with an RMS of 0.27; it is clamped to [-1, 1] so
that no rare corner case can leave it.

The kernels take the integer cell and the fraction within it separately, which keeps sub-cell
precision far from the origin, where a single `float` coordinate would run out of mantissa bits;
the double-precision entry points split before narrowing, so noise stays exact on a planet's
surface.

References:
- K. Perlin, "An Image Synthesizer", SIGGRAPH '85, Computer Graphics 19(3):287-296, 1985.
  https://doi.org/10.1145/325165.325247
- K. Perlin, "Improving Noise", ACM Transactions on Graphics 21(3):681-682, 2002 (SIGGRAPH 2002).
  https://doi.org/10.1145/566654.566636
- K. Perlin, "Improved Noise reference implementation", 2002.
  https://mrl.cs.nyu.edu/~perlin/noise/
- M. Jarzynski and M. Olano, "Hash Functions for GPU Rendering", JCGT 9(3), 2020.

### Simplex noise

The plane is skewed onto a lattice of equilateral triangles; each of the three corners of the
containing triangle contributes (0.5 - r^2)^4 dot(g, d) for its gradient g and offset d. Compared
with Perlin's gradient noise it needs three corners instead of four and has fewer axis-aligned
artefacts. Gradients use the same eight unit directions and `pcg3d` corner hash as `PerlinNoise2`.
With these gradients the raw amplitude never exceeds 0.010081 (found by dense search over a
simplex), so the result is scaled by 99 to span almost [-1, 1].

Perlin's simplex noise patent (US 6,867,776) expired in January 2022.

References:
- K. Perlin, "Noise Hardware", in Real-Time Shading, SIGGRAPH 2001 Course Notes, 2001.
- S. Gustavson, "Simplex Noise Demystified", Linkoping University, 2005.

### Value noise

Each lattice point gets a pseudo-random value in [-1, 1) from `pcg3d`, and values are blended with
Perlin's quintic fade. Cheaper than gradient noise but blobbier, with visible lattice alignment;
mostly useful as a baseline or a soft mask.

Reference: D. R. Peachey, "Building Procedural Textures", in D. S. Ebert et al., Texturing and
Modeling: A Procedural Approach, 3rd ed., Morgan Kaufmann, 2003 (value noise).

### Worley noise

Every lattice cell holds one feature point at a pseudo-random position from `pcg3d`. The 5 x 5
cells around the point are searched, which makes F1 exact (it never exceeds sqrt(2), and points
outside the window are at least 2 away) and F2 exact except in rare configurations where the
second-nearest point lies more than two cells away.

As a fractal basis, Worley distances are mapped linearly from [0, bound] to [-1, 1], where the
bound is the largest possible distance with one feature point per cell: sqrt(2) for F1, and
sqrt(5) for F2 and F2 - F1.

Reference: S. Worley, "A Cellular Texture Basis Function", SIGGRAPH '96, 291-294, 1996.
https://doi.org/10.1145/237170.237267

### Fractals

Octave i has wavelength `wavelength / lacunarity^i` and spectral weight `gain^i`. Choosing
`gain = lacunarity^(-H)` gives a surface with Hurst exponent H (`GainFromHurst`); H near 1 is
smooth, lower H is rougher at short baselines. Lunar topography is self-affine over limited
baselines with baseline-dependent Hurst exponents (Rosenburg et al. 2011), which is what makes H
the natural roughness control here.

Each octave uses its own seed, a pseudo-random sub-cell lattice offset from `pcg3d` and a lattice
turned by the golden angle (about 137.5 degrees) from the octave before. Without the offset every
octave's lattice would share the origin, leaving a visible fixed point; without the turn the
creases that axis-aligned gradients leave along lattice lines would line up from octave to octave
into a grid, most visibly in ridged and billow fractals. In 2D the turn is accumulated by
multiplication rather than `cos` and `sin`, whose last bits differ between platforms, so results
stay bit-identical everywhere. In 3D the turn is the golden angle about the axis
(1, 2, 3) / sqrt(14), which lies along no lattice axis.

Every fractal type is normalized by its largest possible value so the bound
[-amplitude, amplitude] holds for any parameters:
- `Fbm`: sum(w_i n_i) / sum(w_i).
- `Billow`: fBm of 2|n_i| - 1.
- `Ridged`: Musgrave's ridged multifractal, s_i = (offset - |n_i|)^2 weighted by the previous
  octave's signal (feedback gain 2, clamped to [0, 1]), rescaled from [0, 1] to [-1, 1].
- `HybridMultifractal`: Musgrave's hybrid multifractal with (n_i + offset) signals and the running
  weight clamped to [0, 1], divided by (1 + offset) * sum(w_i).

References:
- B. B. Mandelbrot and J. W. Van Ness, "Fractional Brownian Motions, Fractional Noises and
  Applications", SIAM Review 10(4):422-437, 1968. https://doi.org/10.1137/1010093
- F. K. Musgrave, C. E. Kolb and R. S. Mace, "The Synthesis and Rendering of Eroded Fractal
  Terrains", SIGGRAPH '89, Computer Graphics 23(3):41-50, 1989.
  https://doi.org/10.1145/74334.74337
- F. K. Musgrave, "Procedural Fractal Terrains", in D. S. Ebert et al., Texturing and Modeling:
  A Procedural Approach, 3rd ed., Morgan Kaufmann, 2003 (ridged and hybrid multifractals).
- M. A. Rosenburg et al., "Global Surface Slopes and Roughness of the Moon from the Lunar
  Orbiter Laser Altimeter", Journal of Geophysical Research 116, E02001, 2011.
  https://doi.org/10.1029/2010JE003716

### Domain warping

A layer's domain warp displaces the point at which its fractal is evaluated by independent warp
fields, one per axis: 4-octave Perlin fBm with the layer's warp wavelength and amplitude, the first
seeded from the layer's seed and a frozen salt, each further one with `PcgHash` of the one before.

Reference: I. Quilez, "Domain Warping", 2002. https://iquilezles.org/articles/warp/

## Random

### Poisson sampling

`SamplePoisson` uses the multiplication method for means below 10 and Hormann's transformed
rejection with squeeze (PTRS) above.

References:
- W. Hormann, "The Transformed Rejection Method for Generating Poisson Random Variables",
  Insurance: Mathematics and Economics 12(1):39-45, 1993.
  https://doi.org/10.1016/0167-6687(93)90997-4

### Normal sampling

`SampleNormal`, `StandardNormal` and the normal and log-normal kinds of `ValueDistribution` draw
normal deviates with the Box-Muller transform: sqrt(-2 ln u1) cos(2 pi u2) for independent
uniforms u1 and u2.

References:
- G. E. P. Box and M. E. Muller, "A Note on the Generation of Random Normal Deviates", Annals of
  Mathematical Statistics 29(2):610-611, 1958.

## Meshes

### Boulder shapes

`GenerateBoulderShape` builds a shape from a `BoulderStyle`'s `solid` subdivided, every vertex
moved only along its ray from the origin:
1. stretched to axes 1 : b : c, b from `intermediate` and c from `shortAxis`;
2. rounded toward the ellipsoid of those axes by a share drawn from `rounding` (0 keeps the
   solid's edges and corners, 1 is the ellipsoid);
3. roughened by 3D Perlin noise (`PerlinNoise3`), `noiseOctaves` octaves halving in wavelength
   from `noiseWavelength` (in long-axis lengths), changing the radius by up to `noiseAmplitude`;
4. cut by `fractures` planes, flat fracture faces at a share drawn from `fractureDepth` of the
   shape's reach along each plane's normal, whose normals lie within `maxFractureTilt` (0 to 1,
   the largest |z| of the unit normal) of horizontal, so a slab is cut about its rim only.

Because the solid is convex about the origin and every step moves vertices along their rays, the
mesh can never fold over itself. The fracture planes are fitted to the solid's original corners,
which come first at every subdivision level, so every level shows the same shape in finer detail.

`DefaultBoulderStyles` mixes blocks and slabs of broken boulder most of all, then rounded,
weathered boulders, wedges, pyramids and shards, so that a field reads as broken boulder rather
than as pebbles. The mix, the rounding, the noise and the fractures are chosen for their look,
after Chrono::Planet's boulder library; the axis ratios are centred on the mean shape of impact
fragments, a : b : c = 2 : sqrt(2) : 1, which surveys of lunar and asteroid boulders also find.

References:
- A. Fujiwara, G. Kamimoto and A. Tsukamoto, "Expected shape distribution of asteroids obtained
  from laboratory impact experiments", Nature 272:602-603, 1978.
  https://doi.org/10.1038/272602a0
- T. Michikami et al., "Fragment shapes in impact experiments ranging from cratering to
  catastrophic disruption", Icarus 264:316-330, 2016.
  https://doi.org/10.1016/j.icarus.2015.09.038

### Subdivision and icospheres

`Subdivide` splits each triangle into four at its edges' midpoints, as in Loop's subdivision
without its smoothing. Midpoints are shared between the triangles on either side of an edge, so a
closed mesh stays closed, and new vertices are appended after the old ones, so a coarser level's
vertices are a prefix of every finer level's: a shape computed per vertex agrees across the levels
of detail.

`Icosphere` subdivides the icosahedron with every new vertex pushed out to the unit sphere, giving
10 * 4^levels + 2 vertices and 20 * 4^levels nearly equilateral triangles, which take displacement
more evenly than a latitude-longitude sphere.

References:
- C. Loop, "Smooth Subdivision Surfaces Based on Triangles", M.S. thesis, University of Utah, 1987
  (the 1-to-4 split).
- J. R. Baumgardner and P. O. Frederickson, "Icosahedral discretization of the two-sphere", SIAM
  Journal on Numerical Analysis 22(6):1107-1115, 1985. https://doi.org/10.1137/0722066

### Rotation quaternions

`RotationQuaternion` follows Shepperd's method, choosing the largest of the trace and the diagonal
entries to divide by for numerical stability.

References:
- S. W. Shepperd, "Quaternion from rotation matrix", Journal of Guidance and Control
  1(3):223-224, 1978. https://doi.org/10.2514/3.55767b

### Mesh file formats

`WritePly` writes binary little-endian PLY. `WriteGlb` and `WriteGlbScene` write binary glTF 2.0;
glTF is y-up, so a point (x, y, z) in TerrainGen's z-up frame is written as (x, z, -y). Scene nodes
compose scale, rotation and translation as glTF does, so a field of boulders sharing a few shapes
stores each shape once.

References:
- G. Turk, "The PLY Polygon File Format", 1994. http://paulbourke.net/dataformats/ply/
- The Khronos Group, "glTF 2.0 Specification", 2021, sections 3.5 (nodes) and 3.7 (meshes).
  https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html

## Distribution

### Size-frequency distributions

A size-frequency distribution is held as its cumulative form N(>=D), the number of features per
m^2 (or per m^2 per Gyr for a production function) with diameter at least D, which decreases
monotonically. Evaluating outside the diameters a fit was measured over throws unless
extrapolation has been allowed, because published fits are only valid where they were measured.

Terminology follows Robbins et al. (2018).

References:
- S. J. Robbins et al., "Revised Recommended Methods for Analyzing Crater Size-Frequency
  Distributions", Meteoritics & Planetary Science 53(4):891-931, 2018.
  https://doi.org/10.1111/maps.12990

### Golombek-Rapp boulder sizes

Boulder sizes after Golombek and Rapp (1997): the cumulative fractional area covered by boulders
at least D metres across is F(D) = k exp(-q D), with q = qa + qb / k per metre, where k, the total
fractional area covered by boulders, sets how rocky the ground is (`coverage`). The defaults are
qa = 1.79 and qb = 0.152 per metre.

As a number density, -dN/dD = k q exp(-q D) / (pi D^2 / 4) per m^2 per metre, so
N(>=D) = (4 k q / pi) E2(q D) / D per m^2, with E2 the exponential integral of order 2. Lunar
surfaces have k of about 0.005 to 0.03 away from fresh craters (0.02 is Chrono::Planet's lunar
default); Mars landing sites range up to about 0.2.

References:
- M. P. Golombek and D. Rapp, "Size-frequency distributions of rocks on Mars and Earth analog
  sites: Implications for future landed missions", Journal of Geophysical Research 102(E2):
  4117-4129, 1997. https://doi.org/10.1029/96JE03319
- M. Golombek et al., "Rock size-frequency distributions on Mars and implications for Mars
  Exploration Rover landing safety and operations", Journal of Geophysical Research 108(E12),
  2003. https://doi.org/10.1029/2002JE002035

## Layers

### Crater generation

`GenerateCraters` groups diameters into bands [2^k, 2^(k+1)) metres, clipped to [`dMin`, `dMax`).
Band k uses cells of side `cellSizeFactor` * 2^(k+1); each cell draws its crater count from a
Poisson distribution with the density's expected count over the cell, then each crater's diameter,
position and age, all from random substreams keyed by (`seed`, k, cell). Consequently:
- any region, however tiled, contains the same craters: generating tiles and joining them equals
  generating their union;
- bands lying wholly inside [`dMin`, `dMax`) do not depend on `dMin` or `dMax`, so lowering `dMin`
  adds smaller craters without moving larger ones;
- ages come from a separate substream, so changing the age model never moves a crater.

Crater IDs hash (`seed`, band, cell, index) with Philox and are stable under all of the above.
Craters are ordered by band, then cell, then index within the cell. Manual craters get a stable ID
hashed from their position and diameter with `Philox4x32` where they have none, so the same crater
gets the same ID in every run.

Margins (`marginRadii`) keep craters centred outside a region whose rims or ejecta reach into it;
`CraterLayer` sets them from the profile's reach. Craters near the edge of a region can only be
judged by spacing against the craters the list contains, so for results that join across tiles,
spacing thins a list that extends `SpacingReach` beyond the region and crops afterwards.

Spacing example: a `diameterRatio` of 1.5 and a `separation` of 0.5 forbid a crater from lying
within one radius of a crater between two thirds and one and a half times its size, while small
craters may still sit inside large ones.

A hard cap (`maxCraters`) ranks the whole list, so a capped region no longer equals its tiles
joined. Raising `dMin` to `MinDiameterForCount` instead bounds the number of craters while keeping
the distribution's shape above it, so tiles still join. `minResolvedDiameter` lets a renderer skip
bands too small to resolve at its spacing: craters at or above it are the same as without
skipping.

### Fades and blends

`BlendShape` falls from 1 to 0 across a band. `Smoothstep` is the cubic 3t^2 - 2t^3, with a
continuous first derivative; `Smootherstep` the quintic 6t^5 - 15t^4 + 10t^3, with continuous
first and second derivatives; `Gaussian` a Gaussian of sigma a third of the band, rescaled to end
at zero.

`ResolutionFade` fades features that are too small to resolve at the sample spacing rather than
cutting them, which keeps levels of detail consistent: a crater sampled at a coarse and at a fine
spacing changes continuously instead of appearing or vanishing at one spacing.

References:
- D. S. Ebert, F. K. Musgrave, D. Peachey, K. Perlin and S. Worley, "Texturing and Modeling: A
  Procedural Approach", 3rd ed., Morgan Kaufmann, 2003 (smoothstep).
- K. Perlin, "Improving Noise", ACM Transactions on Graphics 21(3):681-682, 2002 (quintic
  smootherstep). https://doi.org/10.1145/566654.566636

### Crater datums and blends

`ApplyCraters` applies craters in turn. Without a datum, each crater's profile is added to the
terrain. With a datum, the datum is fitted (`FitCraterDatum`) to the ground beneath the craters
(the ground given, otherwise the raster as passed in, interpolated bilinearly) plus the relief of
the craters before it (`DatumGround`). The terrain inside the crater is replaced by the datum plus
the profile, weighted by `interiorReplacement` times the `CraterBlend` weight; beyond the blend the
profile is added. A blend starting inside the rim and ending outside it smooths the join between
the cut interior and the draped rim and ejecta.

Craters off the raster still count toward the datums of craters on it, so callers pass the craters
within `DatumMargin` of the raster. A plane datum follows regional slopes, the approach of Martin,
Parkes and Dunstan (2014), DLES terrain crafting (Foreman et al. 2025) and Furlong et al. (2019).

Each crater's change is weighted by its `ResolutionWeight`, so craters too small to resolve at the
sample spacing fade out. Every cell depends only on the craters reaching it and their datums, so
with a ground that covers the footprints a tile of a larger raster gets the same cells as the
larger raster; without one, footprints beyond the raster's edge are fitted to the cells available.
Datums are fitted, and rows applied, in parallel, each band of rows applying the craters in the
same order, so the result does not depend on the number of threads.

A datum fitted to the ground plus the relief of the craters before it cuts a crater that lands in
an earlier crater's bowl relative to the bowl, while its datum still depends only on the ground and
the craters near it, so it is the same on every grid, tile and point. The fit lattice (7 x 7 points
over the fit footprint) depends only on the crater, so the datum is the same at every level of
detail.

References:
- S. Martin, S. Parkes and M. Dunstan, "Modeling cratered surfaces with real and synthetic terrain
  for testing planetary landers", IEEE Transactions on Aerospace and Electronic Systems
  50(4):2916-2928, 2014. https://doi.org/10.1109/TAES.2014.120282
- C. Foreman, J. Kincaid, E. Z. Crues, D. Fenn and A. Joshi, "Digital Lunar Exploration Sites
  (DLES) Terrain Crafting", IEEE Aerospace Conference, 2025.
  https://doi.org/10.1109/AERO63441.2025.11068736
- P. M. Furlong, U. Wong, M. Allan et al., "Planetary Rover Simulation for Lunar Exploration
  Missions", IEEE Aerospace Conference, 2019. https://doi.org/10.1109/AERO.2019.8741780

### Crater spacing

`SpacingConflicts` is Matern type II hard-core thinning: a crater is removed whenever a conflicting
crater takes precedence, even if that crater is itself removed, so each decision depends only on
craters within reach and does not depend on the order of the list. Manual craters are never
marked, so real craters are not thinned even when two of them conflict.

Reference: B. Matern, "Spatial Variation", Lecture Notes in Statistics 36, Springer, 1986.

### Irregular crater outlines

A crater's rim lies at R s(theta) in direction theta, and its whole profile is stretched by that
factor along each direction. s is the product of
- an ellipse of ellipticity e (long over short rim-to-rim diameter) at a random or preferred
  heading, keeping the crater's area, for the share `elongatedShare` of selected craters;
- 1 + sum over harmonics k of A_k cos(k theta + phi_k), for the share `roughShare`: harmonics
  `minHarmonic` to `maxHarmonic` with amplitudes falling as k^-`spectralSlope` and scaled so the
  outline's RMS deviation from a circle is `roughness` radii, at random phases, as Du et al.
  (2024) describe rim outlines by their power spectra.

Bottke et al. (2000) found 5.4% of 932 lunar mare craters (2.3 to 89 km) elliptical, with
ellipticity at least 1.1 and at most 2.23, a share that did not change with size, and about 5% of
kilometre-sized craters on Mars, Venus and the Moon; `elongatedShare` defaults to the lunar 5.4%.
The default ellipticity (1.1 to 1.5) and the `PowerLaw` roughness and spectrum are illustrative,
not fitted.

`RimRoughnessModel::Du2024` draws rims from the fitted spectra of fresh lunar rim crests of Du et
al. (2024, Table 2, rim crest radial distance): per crater, the spectrum's breakpoints from their
diameter relations (1 to 140 km; smaller and larger craters take the 1 and 140 km values),
scattered by their 1-sigma misfits and with noise of 0.55 in log10 power on each harmonic when
`scatter`, turned into waves of amplitude sqrt(2 pi P_i) / 5000 at random phases (their eq. 4 with
the 5000 vertices their spectra were computed on). Harmonics 1 to 511 are kept, tabulated at 1024
angles. Outlines deviating by more than `maxDeviation` radii are scaled down to it, a safeguard of
this library: the fits' tails, drawn independently, can fold a rim.

References:
- W. F. Bottke, S. G. Love, D. Tytell and T. Glotch, "Interpreting the elliptical crater
  populations on Mars, Venus, and the Moon", Icarus 145:108-121, 2000.
  https://doi.org/10.1006/icar.1999.6323
- J. Du, D. A. Minton, A. M. Blevins, C. I. Fassett and Y. Huang, "Spectral analysis of the
  morphology of fresh lunar craters I: rim crest, floor, and rim flank outlines", JGR Planets
  129(11), 2024. https://doi.org/10.1029/2024JE008357

### Martin, Parkes and Dunstan profile

The simple-crater profile of Martin, Parkes and Dunstan (2014), developed for the PANGU planetary
surface simulator at the University of Dundee, is four connected polynomials in x = 2 r / D - 1
(-1 at the centre, 0 at the rim, 1 at two radii). With K = H_r0 - H_r + H, from the fresh rim
height H_r0 and current depth H and rim height H_r:

    h1 = K x^2 + 2K x + H_r0                    -1 <= x <= alpha   bowl (eq. 4)
    h2 = K (alpha + 1) / alpha x^2 + H_r        alpha <= x <= 0    inner rim (eq. 5)
    h3 = a x^3 + b x^2 + H_r                    0 <= x <= beta     outer rim
    h4 = 0.14 (D/2)^0.74 (x + 1)^-3 + F_c(x)    beta <= x <= 1     ejecta (eq. 9, McGetchin 1974)

with alpha = (H_r - H_r0) / K (eq. 6). A fresh crater (H_r = H_r0) has alpha = beta = 0 and a sharp
rim; degradation moves alpha inwards and beta outwards, rounding the rim as in Ross's model. The
terrain and plane heights T_r and P_r of the paper are zero here, because `ApplyCraters` places the
profile on a plane fitted to the crater.

Departures from the published equations, which give discontinuous profiles:
- beta = 3 (H_r0 - H_r) / (2K), with the sign of eq. 8 reversed so it is positive (as in
  synthterrain);
- h3 is synthterrain's cubic, flat at the rim and meeting h4 at beta, in place of eq. 7;
- F_c is the linear function that meets the paper's stated constraints, h4(0) = H_r and h4(1) = 0
  (the ejecta joins the rim and ends at the surrounding surface), in place of eq. 10;
- fresh and current depth and rim height come from the degradation state, rather than from
  Melosh's H_0 = 0.196 D^1.01 and H_r0 = 0.036 D^1.01 times an erosion factor
  (`FreshnessDegradation` provides that erosion factor).

The fractal roughness overlays and irregular rims of the paper are separate perturbations, not part
of this radial profile.

References:
- S. Martin, S. Parkes and M. Dunstan, "Modeling cratered surfaces with real and synthetic terrain
  for testing planetary landers", IEEE Transactions on Aerospace and Electronic Systems
  50(4):2916-2928, 2014. https://doi.org/10.1109/TAES.2014.120282
- T. R. McGetchin, M. Settle and J. W. Head, "Radial thickness variation in impact crater ejecta:
  Implications for lunar basin deposits", Earth and Planetary Science Letters 23, 1974.
- synthterrain (NASA Ames), `synthterrain.crater.profile.MPD_Crater`.

### Fassett and Thomson profile

`FassettThomsonProfile` is the fresh simple-crater profile of Fassett and Thomson (2014, eq. 4),
with the coefficients and variants used by synthterrain. With x = distance / radius and heights in
units of D:

    -0.181                                                    x <= 0.2  (Original, AtOneRadius)
    -0.228809953 + 0.227533882x + 0.083116795x^2 - 0.039499407x^3   interior
     0.188253307 - 0.187050452x + 0.01844746x^2 + 0.01505647x^3     exterior, to x = 1.5

The rim variants (`FassettThomsonRim`):
- `Original`: the published fits, which meet within 1e-5 D at 0.98 radii.
- `AtOneRadius`: the interior fit extended to 1.0 radii so the rim lies at the stated diameter, as
  synthterrain's default `FT_Crater`; leaves a 0.0076 D step at the rim.
- `Plateau`: Fassett et al. (2022) Table 3, synthterrain's `FTmod_Crater`: a flat rim at the fresh
  rim height (`state.fresh.rimHeightRatio`, 0.0368 D on the Moon) from 0.98 to 1.02 radii; leaves a
  0.0042 D step at 1.02 radii on the Moon.

Every variant flattens the interior at a floor `state.current.depthRatio` below the fresh rim
height `state.fresh.rimHeightRatio`, as Fassett et al. (2022) do for size-dependent fresh depths
with the lunar 0.0368 D (`LunarFreshCraterMorphometry`); a depth beyond the fit's floor leaves the
fit unchanged. The rim height is fixed by the fit, so degraded rim heights are not represented.

References:
- C. I. Fassett and B. J. Thomson, "Crater degradation on the lunar maria: Topographic diffusion
  and the rate of erosion on the Moon", JGR Planets 119:2255-2271, 2014.
  https://doi.org/10.1002/2014JE004698
- C. I. Fassett et al., JGR Planets 127, e2022JE007510, 2022 (Table 3).
- synthterrain (NASA Ames), `synthterrain.crater.profile`: `FT_Crater` and `FTmod_Crater`.
  https://github.com/NeoGeographyToolkit/synthterrain

### Complex crater profile

`ComplexCraterProfile` shapes simple craters up to a transition diameter with the simple profile
and complex ones from it. With x = r / R, depth d and rim height h from the crater's current state,
a complex crater has:
- a flat floor at h - d out to the floor radius `floorDiameter(D)` / 2;
- a concave wall from the floor to the rim, h - d + d t^2 with t from 0 at the floor's edge to 1
  at the rim, steepest just below the rim as terraced walls are;
- a central peak, a smooth dome h_p (1 - (r / r_b)^2)^2 of height `peakHeight` and base radius
  r_b, half `peakDiameter`, or from `peakVolume` (r_b^2 = 3 V / (pi h_p)), lowered with the crater
  as it wears (by its depth now over its depth when fresh);
- an exterior falling from the rim as x^-`ejectaExponent`, the ejecta thickness law of McGetchin
  et al. (1973), shifted to reach zero at `reachRadii`.

Morphometric relations are published as power laws in kilometres (`KilometrePowerLaw`). Craters
larger than `peakFitMaxDiameter`, the largest the peak relations were fitted to, keep the peak
height of that size and a base radius in proportion to the diameter, as peaks stop growing (Pike
1977) rather than follow the power laws out of range.

References:
- R. J. Pike, "Size-dependence in the shape of fresh impact craters on the Moon", in Impact and
  Explosion Cratering, D. J. Roddy, R. O. Pepin and R. B. Merrill (eds), Pergamon, 489-510, 1977
  (floor diameters and the transition).
- W. S. Hale and R. A. F. Grieve, "Volumetric analysis of complex lunar craters: implications for
  basin ring formation", Proc. Lunar Planet. Sci. Conf. 13, JGR 87:A65-A76, 1982 (central peak
  heights and volumes).
- D. Stoffler et al., "Cratering history and lunar chronology", Reviews in Mineralogy and
  Geochemistry 60:519-596, 2006, Table 5.1 (the relations above, collected).
  https://doi.org/10.2138/rmg.2006.60.05
- T. R. McGetchin, M. Settle and J. W. Head, "Radial thickness variation in impact crater ejecta:
  implications for lunar basin deposits", EPSL 20:226-236, 1973.

### Crater degradation

`FreshnessDegradation` scales fresh depth and rim height by one erosion factor, the erosion factor
of Martin, Parkes and Dunstan (2014, PANGU). The default cutoff depth ratio 0.04 is the visibility
threshold of Fassett et al. (2022), after Mahanti et al. (2018). Physically, degradation is fast
when fresh and slows with age; `DiffusionDegradation` captures that.

`DiffusedProfile` wears a fresh crater profile by topographic diffusion, dh/dt = kappa del^2 h
(Culling 1960), as Fassett and Thomson (2014) model crater degradation and synthterrain implements
it. An isolated crater on level ground stays axisymmetric as it diffuses, so the 2D equation
reduces to the radial dh/dt = kappa (h'' + h' / r), and in units of the diameter D it depends only
on the dimensionless diffusion age tau = kappa t / D^2 (`CraterState::diffusion`): a larger crater
at the same kappa t looks fresher, by the square of its size. For each fresh shape the profile
solves it once, implicitly (backward Euler on a finite-volume radial grid of `cells` cells out to
`reachRadii` radii, held at 0 there, solved by the Thomas algorithm), and tabulates the shape at 0
and at tau from 1e-5 to 0.5 in geometric steps; heights are interpolated linearly in distance and
in log tau. Craters older than the table keep its last, nearly flat shape.

The fresh shape comes from the fresh profile at the crater's fresh morphometry
(`CraterState::fresh`), assumed self-similar for simple craters (computed once at a 1 km reference
diameter for each fresh morphometry) and computed at each diameter for complex ones.

References:
- W. E. H. Culling, "Analytical theory of erosion", Journal of Geology 68(3):336-344, 1960.
  https://doi.org/10.1086/626663
- C. I. Fassett and B. J. Thomson, "Crater degradation on the lunar maria: Topographic diffusion
  and the rate of erosion on the Moon", JGR Planets 119:2255-2271, 2014.
  https://doi.org/10.1002/2014JE004698
- synthterrain (NASA Ames), `synthterrain.crater.diffusion.diffuse_d_over_D`, an explicit 2D
  solution over a grid two diameters across, which inspired this one.
  https://github.com/NeoGeographyToolkit/synthterrain

### Secondary craters

`SingerMaxSecondaryDiameter` is the largest secondary crater at range R from the centre of a
primary of diameter D, after the generalized fit of Singer et al. (2020, Table 3) to six lunar
secondary fields from 0.83 to 660 km: d = a R^-b with a = 0.022 D^1.96 and -b = -0.14 ln D, D and
R in km. They find it within about +-50% of each field's 99th-percentile envelope. Singer et al.
measure range from the primary; it is taken here from its centre. For primaries over 1 km the
largest secondary shrinks with range, and `SingerRangeForSize` inverts the fit.

`SecondaryOnsetRange` is how far from its centre a primary throws its first secondaries: its
radius plus a number of diameters that Singer et al. (2020) find about 1.3 for primaries of 31 km
and more (Orientale, Copernicus, Kepler) and about 1.8 for those of 0.8 to 3 km; between 3 and
31 km it is interpolated linearly in log diameter, and held beyond the measured sizes.

At range R from a primary of diameter D, the secondaries at least d across number, per square
metre, n(R) (d / (`countFraction` D))^-`sizeSlope` for d below Singer et al.'s largest secondary
there and none above it, between the onset range and an outer range (`outerRange`, or by default
the longest typical ray of Elliott et al. (2018), 1.6 times their median ray length
6.59 (D / 2)^1.27 km, `ElliottMedianRayLength`). Following Chapman's (1968) model of secondary
families, n falls as r^-2 with r = 1 + (R - onset) / D, and the cumulative size slope is about -4
(Shoemaker 1965, as Chapman cites it). How many secondaries there are is not fixed by these
sources: `onsetCount`, the number at least `countFraction` D across in an area the size of the
primary at the onset, must be given (Chapman's own provisional value, about 3 between D/25 and
D/15, overpredicts the craters he observed about 150-fold).

Secondaries take their primary's age, are shaped by the layer's own profile and degradation, and
are placed uniformly in direction; their alignment along rays and radial elongation are not
modelled. They are drawn per primary in polar cells keyed to its ID, size band, ring and sector.

References:
- K. N. Singer, B. L. Jolliff and W. B. McKinnon, "Lunar secondary craters and estimated ejecta
  block sizes reveal a scale-dependent fragmentation trend", JGR Planets 125, e2019JE006313,
  2020. https://doi.org/10.1029/2019JE006313
- C. R. Chapman, "Interpretation of the diameter-frequency relation for lunar craters
  photographed by Rangers VII, VIII, and IX", Icarus 8:1-22, 1968.
  https://doi.org/10.1016/0019-1035(68)90058-4
- J. R. Elliott, Y.-H. Huang, D. A. Minton and A. M. Freed, "The length of lunar crater rays
  explained using secondary crater scaling", Icarus 312:231-246, 2018.
  https://doi.org/10.1016/j.icarus.2018.04.015

### Boulder ejecta

`BoulderEjecta` adds the blocks thrown out by craters that dug through the regolith into boulder:
craters smaller than the regolith thickness over the excavation depth ratio (the depth of
excavation is about a tenth of the diameter, Melosh 1989) throw none. When the surface gravity g
and regolith thickness H are given, a crater's blocks reach no further than Bart and Melosh's
(2010b, eq. 9) greatest throw over a regolith blanket, (138.5 / g) (D / H)^0.7086 m from the
centre (`BoulderThrowDistance`).

References:
- J. A. Watkins, C. I. Fassett et al., "Boulder distributions around young, small lunar impact
  craters and implications for regolith production rates and landing site safety", JGR Planets
  124, 2019. https://doi.org/10.1029/2019JE005963
- G. D. Bart and H. J. Melosh, "Distributions of boulders ejected from lunar craters", Icarus
  209:337-357, 2010. https://doi.org/10.1016/j.icarus.2010.05.023
- G. D. Bart and H. J. Melosh, "Impact into lunar regolith inhibits high-velocity ejection of
  large blocks", JGR Planets 115, E08004, 2010, eq. 9. https://doi.org/10.1029/2009JE003441
- H. J. Melosh, "Impact Cratering: A Geologic Process", Oxford University Press, 1989 (depth of
  excavation about a tenth of the diameter).

### Boulder thinning

`BoulderLayer` draws candidate boulders per cell in size bands like craters, from
`BoulderCandidates`: the boulders between craters at their richest (the richest patches on
bedrock), plus fresh crater blocks when enabled, so that no point has more boulders of any size
than the candidates. Each candidate is then kept with probability n(D; x) / n_candidates(D), from a
uniform keyed by its ID: exact thinning of an inhomogeneous Poisson process. Each boulder's height,
burial, yaw and lean come from its ID too. Everything depends only on position, so boulders are the
same in every grid, tile and point query; bands too small to resolve at the spacing are not
generated, and the boulders of the bands kept are the same as at any finer spacing. Golombek and
Rapp (1997) give the default sizes (`GolombekRappSfd`).

### Roughness

Roughness is given as Shepard et al. (2001) do: the RMS slope over a baseline, the RMS height
difference between points `baseline` apart divided by `baseline`, and the Hurst exponent, how that
RMS height difference grows with the baseline (as baseline^H). Rougher at fine scales is a smaller
H. `RoughnessLayer` sums octaves of Perlin noise with amplitudes growing as wavelength^H, scaled
with the measured structure function of Perlin noise (`PerlinStructureFunction`) so that the RMS
slope over the baseline is the one asked for. On a sphere each octave's lattice is turned by a
uniform random rotation, drawn as a unit quaternion (Shoemake 1992).

References:
- M. K. Shepard, B. A. Campbell, M. H. Bulmer, T. G. Farr, L. R. Gaddis and J. J. Plaut, "The
  roughness of natural terrain: A planetary and remote sensing perspective", JGR Planets
  106(E12):32777-32795, 2001. https://doi.org/10.1029/2000JE001429
- K. Shoemake, "Uniform random rotations", in D. Kirk (ed.), Graphics Gems III, Academic Press,
  124-132, 1992.

### Exposure

`ExposureField` tells how far the ground at a point is bedrock rather than soil from the shape of
the ground alone: loose material moves downhill and settles in lows, so rises and steep ground are
left bare. A point standing above the mean of eight points a reach around it is bedrock, as is a
slope too steep to hold soil; each grades from soil to bedrock with a smoothstep between its two
thresholds, and the larger share wins. Level ground can be bedrock too, such as a swept lava
floor, which the shape cannot reveal, so `floorCover` of it is bedrock in noise patches.

It changes no height. Layers that place material, such as boulders, and renderers ask it where the
bedrock is, so they agree. The thresholds (`ExposureParams`) are a rule of thumb, not a
measurement of any site; they follow Chrono::Planet's `ChExposureField`.

### Perlin structure functions

`PerlinStructureFunction` is the structure function of `PerlinNoise2` at a lag of r lattice units:
the mean squared difference between values r apart, over positions, directions and seeds. It grows
as 0.7724 r^2 for small lags and levels off at 0.18312, twice the variance, from about 4 lattice
units; in between it is interpolated from a table computed by Monte Carlo to about 0.1% (2^26
pairs per lag, tabulated as the value over r^2 at r = 2^(k/4)).

`Perlin3StructureFunction` is the structure function of `PerlinNoise3` at a lag of r lattice units
in a random 3D direction, as a surface cutting the lattice at a random angle sees it: about
0.4941 r^2 for small lags, levelling off at 0.14520 from about 4 lattice units, interpolated in
between from a table computed by Monte Carlo (2^24 pairs per lag).

`RoughnessLayer` scales its octaves with these so that the RMS slope over the baseline is the one
asked for; on a sphere each octave's 3D lattice is turned by a uniform random rotation (Shoemake
1992) and calibrated with `Perlin3StructureFunction`. Each octave fades out at spacings too coarse
to resolve it, so coarse levels of detail see the same surface low-passed.

### Bedforms

`BedformLayer` adds fields of wind-blown bedforms, ripples or dunes: each lattice cell may hold a
field with a ragged edge, whose crests run across its wind, wander along their length, and rise
along a gentle upwind face and fall down a lee face, C1 continuous and zero in the troughs. Where
fields overlap, their relief is their mean weighted by cover, so sand is shared out rather than
piled. Everything is a function of position, so grids, tiles and point queries agree.

It follows Chrono::Planet's bedform layer, with its constants: field radii 0.4 to 0.7 lattice
cells with edges 30% ragged, thinning over their outer 35%, and crests wandering over 5
wavelengths. On a sphere, fields are drawn in `CubeSphereCells`, each with a chance in proportion
to its area.

Sand settles in lows: with the exposure field (`BedformOptions::exposure`), a field whose centre is
on bedrock is left out, the likelier the barer the ground there.

### Boulder models

`BoulderModelThreshold` decides which boulders become 3D models of their own rather than relief in
the heights: those at least `minDiameter` metres across, or spanning at least `minSamples` samples
at the spacing evaluated. Boulders too small to resolve are left out (`ResolutionFade`), boulders
from there up to the threshold are drawn into the heights, and boulders above it are models.
Neither set means no models. With `minSamples`, a boulder drawn into a coarse level of detail
becomes a model at a finer one.

Ejecta blocks (`BoulderEjecta`) are densest at the rim and thin outward, and are broken down over
time, so few are left around old craters. Where the ejecta of several craters overlap, the largest
share counts.

`PlacementAtSite` moves placements on a sphere into the east, north and up frame of a site on the
datum sphere, so a scene of boulders about a site keeps metre-scale coordinates (glTF stores single
precision). Boulders placed by a `BoulderLayer` on a sphere have their placements in their own
local frames.

### Secondary crater depth

A "secondaries" layer (`SecondaryLayerSpec`) shapes its craters with the profile of the crater
layer before it at a fixed depth-to-diameter ratio, 0.11 by default: the average Pike and Wilhelms
(1978) found for simple lunar secondaries, as Singer et al. (2020) report it.

References:
- K. N. Singer, B. L. Jolliff and W. B. McKinnon, "Lunar secondary craters and estimated ejecta
  block sizes reveal a scale-dependent fragmentation trend", JGR Planets 125, e2019JE006313,
  2020. https://doi.org/10.1029/2019JE006313
- R. J. Pike and D. E. Wilhelms, 1978, as cited by Singer et al. (2020).

## Filters

### Rim smoothing

Fresh profiles have sharp rim crests; smoothing them is a localized linear diffusion
(sigma^2 = 2 kappa t; Culling 1960), the process that rounds rims as craters degrade (Fassett and
Thomson 2014). `SmoothCraterRims` takes every crater's smoothed copy from the terrain as it was
before any smoothing and applies the blends in list order, so each cell depends only on the
terrain within a fixed distance and tiles with a margin equal the whole.

References:
- W. E. H. Culling, "Analytical theory of erosion", Journal of Geology 68(3):336-344, 1960.
  https://doi.org/10.1086/626663
- C. I. Fassett and B. J. Thomson, "Crater degradation on the lunar maria: Topographic diffusion
  and the rate of erosion on the Moon", JGR Planets 119:2255-2271, 2014.
  https://doi.org/10.1002/2014JE004698

## Albedo

### Albedo detail

The map's roughness is measured once, on the whole map, from the structure function of the
logarithm of its brightness (the mean of its bands), D(l) = <(ln a(p + l) - ln a(p))^2>, at lags of
1, 2, 4 and 8 cells along rows and columns. For a fractal D(l) grows as l^(2H), so a least squares
line through log D against log l gives the Hurst exponent H (clamped to [0, 1]). Octave i of the
detail has wavelength c / 2^(i+1) for cell size c and RMS sqrt(D(c) / 2) 2^(-(i+1)H), carrying the
measured roughness one octave down at a time; the albedo is multiplied by e to the sum, so it stays
positive and the bands keep their ratios.

### Optical maturity

Lucey et al.'s (2000, eq. 1) optical maturity parameter (OMAT) of a surface with 750 nm reflectance
r750 and 950 nm reflectance r950 is its distance from an origin (x0, y0) in the plane of
reflectance and ratio, sqrt((r750 - x0)^2 + (r950/r750 - y0)^2). The origin is the hypothetical
hypermature end-member, very dark and very red: 0.08 and 1.19 for Clementine data, and 0.01 and
1.26 for John Adams' directional-hemispherical laboratory spectra, which their Table 1 of returned
soils uses; they note other calibrations need their own. Higher is less mature: very mature soils
score about 0.09 to 0.12, and immature ejecta near crater rims 0.29 to 0.42 (Grier et al. 2001).

In Lucey et al.'s (2000, Fig. 1) plane, iron content is the angle about the origin and maturity
the distance from it, orthogonal trends, so at fixed iron content the 750 nm reflectance is
x0 + OMAT cos(theta_Fe): it moves from x0 in proportion to the maturity parameter.
`ReflectanceAtMaturity` therefore gives x0 + (reflectance - x0) maturity / reference. Below x0,
which the relation does not cover, x0 is taken as the reflectance and the reflectance is kept. The
relation is for 750 nm; it is applied to other bands as an approximation.

References:
- P. G. Lucey, D. T. Blewett, G. J. Taylor and B. R. Hawke, "Imaging of lunar surface maturity",
  JGR 105(E8):20377-20386, 2000. https://doi.org/10.1029/1999JE001110
- J. A. Grier, A. S. McEwen, P. G. Lucey, M. Milazzo and R. G. Strom, "Optical maturity of ejecta
  from large rayed lunar craters", JGR 106(E12):32847-32862, 2001.
  https://doi.org/10.1029/1999JE001160

### Ray retention

How long a crater keeps the immature, bright material that makes its ejecta and rays stand out
follows Werner and Medvedev (2010): rayed craters of 5 to 50 km follow a 750 Myr isochrone, and
those under about 1 km a 5 Myr one, with retention falling gradually between 1 and 5 km
(interpolated here as a power law of diameter between the two). Their rayed craters over 50 km lie
between the 750 Myr and 2 Gyr isochrones, compositional rays lasting longer; `largeMyr` keeps
750 Myr for them by default.

Reference: S. C. Werner and S. Medvedev, "The Lunar rayed-crater population - Characteristics of
the spatial distribution and ray retention", EPSL 295:147-158, 2010.
https://doi.org/10.1016/j.epsl.2010.03.036

### Oblique impacts

How oblique impacts shape a crater's rays follows Herrick and Forsberg-Taylor (2003) and Anderson
et al. (2003). Each crater gets an impact angle theta from the horizontal, distributed as Shoemaker
(1962) found for random approach directions, P(< theta) = sin^2 theta (so theta = asin sqrt(u)),
and a downrange direction, `heading` (radians counter-clockwise from east) or uniform at random.

- Below `asymmetricDegrees` (45 on the Moon) the ejecta blanket is asymmetric. Ejecta leave faster
  downrange: Anderson et al. measured downrange ejection speeds 20 to 40% above uprange ones at 30
  degrees, and none at 90, so the ratio is drawn from `downrangeSpeedRatio`. A ray at angle d from
  downrange is ejected at a speed proportional to 1 + a cos d, with (1 + a) / (1 - a) the ratio,
  and as ballistic range grows with the square of the speed (Elliott et al. 2018, eq. 5), its
  length is multiplied by (1 + a cos d)^2 / (1 + a^2 / 2), which keeps the mean over directions.
- Below `forbiddenDegrees` (25) a forbidden zone forms uprange: rays within `zoneHalfWidthDegrees`
  of uprange are dropped.
- Below `butterflyDegrees` (10) the ejecta take a butterfly pattern: rays near downrange are
  dropped too.

Herrick and Forsberg-Taylor give the angles from the share of 86 mare craters showing each trait
(29% with a forbidden zone, 5% a butterfly). Neither source measures the zones' width, so
`zoneHalfWidthDegrees` is illustrative, and holding the speed ratio for every angle below
`asymmetricDegrees` simplifies Anderson et al.'s single measured angle.

References:
- R. R. Herrick and N. K. Forsberg-Taylor, "The shape and appearance of craters formed by oblique
  impact on the Moon and Venus", Meteoritics and Planetary Science 38:1551-1578, 2003.
  https://doi.org/10.1111/j.1945-5100.2003.tb00001.x
- J. L. B. Anderson, P. H. Schultz and J. T. Heineck, "Asymmetry of ejecta flow during oblique
  impacts using three-dimensional particle image velocimetry", JGR 108(E8), 5094, 2003.
  https://doi.org/10.1029/2003JE002075
- E. M. Shoemaker, "Interpretation of lunar craters", in Z. Kopal (ed.), Physics and Astronomy of
  the Moon, Academic Press, 283-359, 1962 (as Herrick and Forsberg-Taylor cite it).

### Crater rays

The rays of a crater are narrow streaks of immature material and secondary craters running out
from the rim, after Elliott et al. (2018), who mapped the rays of 26 lunar craters of 10 m to 84 km
diameter (their Table 2).

- Count: they average 10.7 rays (6 to 20); the default is normal with that mean and the standard
  deviation of their Table 2 counts, 3.1, clamped to 6 to 20 and rounded.
- Length: the median ray of a crater of radius R km is `lengthCoefficient` R^`lengthExponent` km
  long (their eq. 2: 6.59 R^1.27, fitted to craters over about 100 m radius). Each ray's length is
  the median times e to `lengthSpread`: by default uniform between the means, over the 15 craters
  of Table 2 they fitted, of ln(min / median) and ln(max / median), -0.356 and 0.468.
- Width: the polar angle a ray spans 2 radii from the centre, where continuous ejecta breaks into
  rays, averages 22 degrees with a standard deviation of 8.3; the default is normal with those,
  clamped to 2 standard deviations either side. A ray keeps that angle inside 2 radii and the
  width it has there beyond them, with a cross-section falling as (1 - (d / h)^2)^2 to its edge at
  half-width h.
- Strength: an excess of optical maturity over the background of `excess` at the rim, falling
  linearly to 0 at the ray's end. The default, 0.048, is Elliott et al.'s OMAT of Tycho's rays
  4 radii out (0.25) over Lucey et al.'s (2000) Tycho background (0.202).
- Breaks: rays are chains of secondary clusters with gaps; value noise along the ray, of
  wavelength `patchRadii` radii, takes up to `patchiness` of the strength away. Both defaults are
  illustrative, not measured.

Azimuths are uniform at random. Oblique impacts lengthen rays downrange and clear them uprange,
which Elliott et al. note is the largest source of spread in a crater's ray lengths; their median
is about what a vertical impact would give.

Reference: J. R. Elliott, Y.-H. Huang, D. A. Minton and A. M. Freed, "The length of lunar crater
rays explained using secondary crater scaling", Icarus 312:231-246, 2018.
https://doi.org/10.1016/j.icarus.2018.04.015

### Crater albedo

A fresh crater's material is less mature than the ground around it, so brighter. In optical
maturity (OMAT, `OpticalMaturity`) units over the background's `backgroundMaturity`:
- inside the rim it is `interiorExcess` higher;
- outside it, on the continuous ejecta, `ejectaExcess` higher at the rim, falling with distance r
  as the ejecta's thickness does, (r / R)^-`ejectaExponent` for crater radius R (McGetchin et al.
  1973: B = 3 for lunar craters), shifted to reach 0 at `ejectaReachRadii`;
- along its rays as described above.

The defaults are Tycho's (Lucey et al. 2000, Table 4): interior 0.334, ejecta 0.278 and background
0.202 OMAT, Tycho's ejecta value taken at the rim. Continuous ejecta reaches about a radius,
perhaps two, beyond the rim, with under 10% of it beyond 3 radii (Melosh 1989, as Grier et al.
2001 summarize), hence a reach of 3 radii.

The excess fades with age t as max(0, 1 - t / T)^`fadeExponent`, with T the crater's retention
time. How immaturity fades with age is not measured in these sources; the shape is configurable, a
straight fade by default. Excesses of overlapping craters add, and the total is capped at
`maxMaturity`, the top of Grier et al.'s range for immature ejecta near crater rims (0.29 to
0.42). The albedo then moves with maturity at fixed iron content (`ReflectanceAtMaturity`).

References:
- P. G. Lucey, D. T. Blewett, G. J. Taylor and B. R. Hawke, "Imaging of lunar surface maturity",
  JGR 105(E8):20377-20386, 2000. https://doi.org/10.1029/1999JE001110
- J. A. Grier, A. S. McEwen, P. G. Lucey, M. Milazzo and R. G. Strom, "Optical maturity of ejecta
  from large rayed lunar craters", JGR 106(E12):32847-32862, 2001.
  https://doi.org/10.1029/1999JE001160
- T. R. McGetchin, M. Settle and J. W. Head, "Radial thickness variation in impact crater ejecta:
  implications for lunar basin deposits", EPSL 20:226-236, 1973.
  https://doi.org/10.1016/0012-821X(73)90162-3
- B. R. Hawke et al., "The origin of lunar crater rays", Icarus 170:1-16, 2004: rays are bright
  from immature material, from material of contrasting composition, or both; only immaturity is
  modelled here. https://doi.org/10.1016/j.icarus.2004.02.013

### Synthetic albedo

`SyntheticAlbedo` varies a base colour in hue, saturation and value with noise and converts it to
red, green and blue by Smith's (1978) hexcone model. The default value, 0.110, is the 750 nm
reflectance of mature Apollo 12 mare soil 12070 (Lucey et al. 2000, Table 1); their highland soils
range from about 0.19 to 0.41.

References:
- A. R. Smith, "Color gamut transform pairs", SIGGRAPH '78, Computer Graphics 12(3):12-19, 1978.
  https://doi.org/10.1145/965139.807361
- P. G. Lucey et al. (2000); see "Optical maturity".

## Bodies

### Body radii

`MeanRadius` returns the volumetric mean radius from NASA's planetary fact sheets: the Moon
1737.4 km, Mars 3389.5 km and Mercury 2439.7 km. Their equatorial and polar radii are 1738.1 and
1736.0 km, 3396.2 and 3376.2 km, and 2440.5 and 2438.3 km.

Reference:
- D. R. Williams, NASA Space Science Data Coordinated Archive, Planetary Fact Sheets (Moon, Mars
  and Mercury), last updated 11 January 2024. https://nssdc.gsfc.nasa.gov/planetary/factsheet/

### Lunar chronology

Neukum et al. (2001, eq. 5) give the cumulative density of craters at least 1 km across on a
lunar surface T Gyr old as N1(T) = 5.44e-14 (e^(6.93 T) - 1) + 8.38e-4 T per km^2. The linear
term is the present-day rate, which equals 10^a_0 of the Neukum production function. Ages are
accepted up to 4.6 Gyr.

Reference:
- G. Neukum, B. A. Ivanov and W. K. Hartmann, "Cratering Records in the Inner Solar System in
  Relation to the Lunar Reference System", Space Science Reviews 96:55-86, 2001.
  https://doi.org/10.1023/A:1011989004263

### Lunar production function

`NeukumProductionFunction` is the Neukum et al. (2001) lunar production function (eq. 2, table 1
"new" N(D)), valid from 10 m to 300 km, as a production rate per m^2 per Gyr at the present-day
flux.

a_0 is log10(8.38e-4) = -3.076756, as in synthterrain; the paper prints -3.0876, which
craterstats treats as a typo for -3.0768. Combined with the Neukum chronology in a
`ProductionSurface` it reproduces the large-crater values of the DSNE 3.5 Ga mare table (NASA
SLS-SPEC-159 Revision I, Table 3.4.1.1-1).

`ExtendedNeukumProductionFunction` extends it below 10 m as a cumulative power law of slope
-2.85, continuous at 10 m, and valid from 2.5 to 300000 m. Oetting et al. (2025) measured a mean
cumulative slope of -2.85 for craters of at most 10 m on young impact melt deposits, fitted by
maximum likelihood on unbinned data, and concluded that production functions can be extended down
to 2.5 m.

References:
- G. Neukum, B. A. Ivanov and W. K. Hartmann (2001); see "Lunar chronology".
- A. Oetting et al., "Slopes of Lunar Crater Size-Frequency Distributions on Exterior Impact Melt
  Deposits of Young Craters", JGR Planets 130, e2024JE008589, 2025.
  https://doi.org/10.1029/2024JE008589

### Lunar equilibrium

Trask's (1966) lunar equilibrium is N(>=D) = 10^-1.1 D^-2 (the same in km/km^2 and m/m^2). The
DSNE uses it as the upper limit on the density of craters smaller than about 100-200 m on any
surface.

Hartmann's (1984) lunar equilibrium is N(>=D) = 10^-1.14 D_km^-1.83 per km^2, as given by Fassett
et al. (2022, eq. 3b).

References:
- N. J. Trask, in Ranger VIII and IX Part II, JPL Technical Report 32-800, 1966.
- NASA SLS-SPEC-159 (DSNE) Revision I, Section 3.4.1.1.
- C. I. Fassett et al., "Topographic Diffusion Revisited", JGR Planets 127, 2022.
  https://doi.org/10.1029/2022JE007510

### Lunar surfaces and steady-state ages

`LunarSurface` follows the DSNE construction of a lunar surface: the crater density is the lesser
of the `ExtendedNeukumProductionFunction` surface of the given age and `TraskEquilibrium`, valid
from 2.5 m to 300 km, with `SteadyStateAges` from the same two distributions.

In equilibrium, craters of a size are erased as fast as they form, so their count is the
production rate times their lifetime: tau(D) = n_eq(D) / n_prod(D), from the incremental
equilibrium density and present-day production rate (as in synthterrain, which uses 0.1 dex bins
rather than the derivative). The lifetime is capped at the surface age; craters whose lifetime
exceeds it are production limited and all retained.

Ages follow the formation rate of the Neukum et al. (2001) chronology over [0, min(tau(D), T)],
which is nearly uniform for short lifetimes and weighted towards the early bombardment for
production-limited craters, with no switch between the two regimes.

Reference:
- NASA SLS-SPEC-159 (DSNE) Revision I, Section 3.4.1.1.

### Fresh lunar craters

Fresh lunar craters have a rim height of 0.0368 diameters (Fassett et al. 2022, Table 3; Stopar
et al. 2017 find about 0.04 with no size trend from 65 m to 10 km).

Their initial rim-to-floor depth/diameter ratio (Fassett et al. 2022, Table 2) is 0.10 up to
10 m, 0.11 below 40 m, 0.13 below 100 m, 0.15 below 200 m, 0.17 below 400 m and 0.21 above. The
values above 40 m follow Stopar et al. (2017); those below are extrapolated.

Craters become complex at 15 km (Pike 1977); Kruger et al. (2018) find that transitional and
complex craters differ between the maria and the highlands. Complex craters have depth
1.044 D^0.301 (Pike 1974) and rim height 0.236 D^0.399 (Pike 1977), D and both in km. Their
shape has floor diameter 0.187 D^1.249 (Pike 1977, fitted 20 to 125 km), central peak height
0.589e-3 D^1.969 and volume 0.987e-7 D^5.078 km^3 (Hale and Grieve 1982, fitted 17 to 51 km, so
held at 51 km beyond), D in km, collected in Stoffler et al. (2006, Table 5.1).

References:
- C. I. Fassett et al., "Topographic Diffusion Revisited: Small Crater Lifetime on the Moon and
  Implications for Volatile Exploration", JGR Planets 127, e2022JE007510, 2022.
  https://doi.org/10.1029/2022JE007510
- J. D. Stopar et al., Icarus 298, 2017 (fresh-crater d/D by size; also DSNE Table 3.4.1.2-1).
- R. J. Pike, "Depth/diameter relations of fresh lunar craters: revision from spacecraft data",
  Geophysical Research Letters 1:291-294, 1974. https://doi.org/10.1029/GL001i007p00291
- R. J. Pike, "Size-dependence in the shape of fresh impact craters on the Moon", in Impact and
  Explosion Cratering, Pergamon, 489-510, 1977.
- T. Kruger, S. Hergarten and T. Kenkmann, "Deriving morphometric parameters and the
  simple-to-complex transition diameter from a high-resolution, global database of fresh lunar
  impact craters (D >= ~3 km)", JGR Planets 123:2667-2690, 2018.
  https://doi.org/10.1029/2018JE005545
- D. Stoffler et al., "Cratering history and lunar chronology", Reviews in Mineralogy and
  Geochemistry 60:519-596, 2006, Table 5.1.

### Lunar crater diffusivity

Fassett and Thomson (2014) find a lunar diffusivity of 5.5 m^2/Myr on average for 800 m to 5 km
mare craters over the last 3 Gyr; `FassettThomson2014Diffusivity` applies it to every size. With
it a 1 km crater keeps about 52% of its depth after 3 Gyr and a 300 m crater about 7%.

Fassett et al. (2022, Figure 2) find a size-dependent diffusivity needed to hold the Hartmann
equilibrium of small craters, kappa = 3.6 D^0.87 m^2/Myr with D in km, fitted above 5 m and held
at its 5 m value below (where production is less certain); for the Trask equilibrium it is
kappa = 3.1 D^1.05 m^2/Myr. They find it 2 to 4 times uncertain, and extrapolated to the
kilometre scale it gives 2.8 to 3.2 m^2/Myr against Fassett and Thomson's (2014) 5.5.

Fassett and Thomson (2014, eq. 6) calibrate the diffusion age kappa t (m^2) against crater age t
in Gyr for 800 m to 5 km mare craters: 435.83 t^5 - 3621.2 t^4 + 11204 t^3 - 16811 t^2 +
17546 t, faster in the last 0.7 Gyr than before; it is clamped at 0. It does not depend on size,
so it suits kilometre-scale craters.

References:
- C. I. Fassett and B. J. Thomson, "Crater degradation on the lunar maria: Topographic diffusion
  and the rate of erosion on the Moon", JGR Planets 119:2255-2271, 2014.
  https://doi.org/10.1002/2014JE004698
- C. I. Fassett, R. A. Beyer, A. N. Deutsch, M. Hirabayashi, C. J. Leight, P. Mahanti et al.,
  "Topographic diffusion revisited: small crater lifetime on the Moon and implications for
  volatile exploration", JGR Planets 127, e2022JE007510, 2022.
  https://doi.org/10.1029/2022JE007510

### Lunar crater specifications

Moore's terrain-specific cumulative distributions for Lunar Roving Vehicle trafficability (H. J.
Moore, USGS "Section C", c. 1970), N per m^2 with D in metres: rough mare 10^-1 D^-2 below 100 m
and 10 D^-3 above; smooth mare 10^-1 D^-2 below 40 m, then 10^0.602 D^-3, 10^-2.038 D^-1.68 and
10 D^-3; uplands 10^-1 D^-2 below 1 km and 10^2 D^-3 above; heavily cratered 10^-1 D^-2 up to
10 km. The published coefficients are rounded, so segments agree at their joins only to within
0.2%; the presets accept joins within 0.5%.

The VIPER environmental specification (VIPER-MSE-SPEC-001, 2021) as used by synthterrain is
N = 29174 D^-1.92 per km^2 for D <= 80 m and 156228 D^-2.389 above (D in m). It is kept for
comparison with synthterrain; its segments disagree by a factor of 1.46 at 80 m, and Allan et al.
(2017) found it predicts too many craters against a NAC image.

### Lunar boulders

NASA SP-8023, the lunar surface design model, gives in its Figure 7 the blocks per m^2 at least D
metres across between craters on smooth mare, rough mare and upland terrains, which its authors
drew as a compromise between the Surveyor block counts of Shoemaker and Morris and of Choate. The
curves are published only as a figure. The tables were digitized from a 300 dpi scan of it,
reading each curve's crossings and fitting the straight segments; they agree with the printed
curves to about 0.03 in log10 N. They span 0.03 mm to about 6 to 9 m (to 10^-6 blocks per m^2);
the maximum curve steepens from about -1.8 to -3.1 at 0.24 m and the nominal from about -2.1 to
-3.2 near 0.85 m, while the minimum keeps a slope of about -2.55.

Its Figure 9 gives the blocks per m^2 in the annulus from the rim to two crater radii of blocky
craters on the same terrains (after H. J. Moore), the model for the ejecta of craters that
reached bedrock. It was digitized the same way, to about 0.03 in log10 N. The tables span from
where each curve leaves the top of the figure (10^-2 blocks per m^2, at 0.85 m for the nominal
and minimum curves and 1.4 m for the maximum) to 10^-6 blocks per m^2 (7 to 24 m). None is a
power law: the maximum curve steepens from a slope of about -2.1 to -4.5.

synthterrain offers three power-law boulder distributions, N per m^2 with D in metres, kept for
comparison; its sources carry no citation for them: 0.0001 D^-1.75457 ("low blockiness", from
synthetic_moon), 0.002 D^-2.6607 ("high blockiness", from synthetic_moon, named Haworth) and
0.0003 D^-2.482 (the VIPER Environmental Specification, VIPER-MSE-SPEC-001, 2021).

References:
- NASA, "Lunar Surface Models", Space Vehicle Design Criteria (Environment), NASA SP-8023, 1969,
  sections 3.4.2 and 3.4.3, Figures 7 and 9. https://ntrs.nasa.gov/citations/19700009596
- E. M. Shoemaker and E. C. Morris, "Physical characteristics of the lunar regolith determined
  from Surveyor television observations", Radio Science 5(2):129-155, 1970.
  https://doi.org/10.1029/RS005i002p00129
- synthterrain (NASA Ames, R. Beyer et al.), `synthterrain.boulder.functions`.
  https://github.com/NeoGeographyToolkit/synthterrain

### Martian chronology and production function

Ivanov's (2001) Martian chronology gives the cumulative density of craters at least 1 km across on
a surface T Gyr old as N1(T) = 2.68e-14 (e^(6.93 T) - 1) + 4.13e-4 T per km^2. The linear term
is the present-day rate, 10^a_0 of the production function. Ages are accepted up to 4.6 Gyr.

Ivanov's (2001) Martian production function, derived from the lunar one of Neukum et al. (2001),
is a degree-11 polynomial in log D, valid from 15 m to 362 km. `MarsSurface` scales it by the
chronology with no erosion and no equilibrium cap, and draws crater ages uniformly over the
surface's age, which suits surfaces younger than about 3 Gyr, when the flux was nearly steady.

Reference:
- B. A. Ivanov, "Mars/Moon cratering rate ratio estimates", Space Science Reviews 96:87-104, 2001
  (coefficients as in craterstats, G. Michael, https://github.com/ggmichael/craterstats).

### Fresh Martian craters

Fresh Martian craters become complex at 7 km, where Garvin et al. (2003) start their complex fits
(Pike 1980 finds the transition between 3 and 8 km). With D in km for the fits:
- depth over diameter is 0.23, the mean of new, dated decametre-scale impacts (Daubar et al.
  2014), or Garvin et al.'s (2003) simple-crater fit d = 0.21 D^0.81 where that is shallower (from
  about 0.6 km), and their complex fit d = 0.36 D^0.49 from 7 km;
- rim height over diameter is 0.03, that of the most pristine 10 m to 1.2 km craters at InSight's
  site (Sweeney et al. 2018), or Garvin et al.'s simple fit h = 0.04 D^0.31 where that is lower
  (from about 1.5 km), their complex fit h = 0.02 D^0.84, and h = 0.12 D^0.35 above 100 km.

Complex craters have central peaks of height 0.04 D^0.51 and diameter 0.25 D^1.05 (Garvin et al.
2003, fitted 7 to 100 km, so held at 100 km beyond), D in km. No Martian fit of the floor's
diameter was found, so the lunar one of Pike (1977), 0.187 D^1.249, stands in.

References:
- I. J. Daubar, C. Atwood-Stone, S. Byrne, A. S. McEwen and P. S. Russell, "The morphology of
  small fresh craters on Mars and the Moon", JGR Planets 119:2620-2639, 2014.
  https://doi.org/10.1002/2014JE004671
- J. Sweeney et al., "Degradation of 100-m-scale rocky ejecta craters at the InSight landing
  site on Mars and implications for surface processes and erosion rates in the Hesperian and
  Amazonian", JGR Planets 123:2732-2759, 2018. https://doi.org/10.1029/2018JE005618
- J. B. Garvin, S. E. H. Sakimoto and J. J. Frawley, "Craters on Mars: global geometric
  properties from gridded MOLA topography", Sixth International Conference on Mars, abstract
  3277, 2003, Table 1. https://www.lpi.usra.edu/meetings/sixthmars2003/pdf/3277.pdf
- R. J. Pike, "Formation of complex impact craters: evidence from Mars and other planets",
  Icarus 43:1-19, 1980.

### Martian bedforms

Martian large wind ripples are metre-scale bedforms with sinuous crests and lee faces at the
angle of repose (Lapotre et al. 2016). The preset uses a 2 m wavelength, a height of a tenth of it
(an assumption: no measured height was found), a lee share giving a 30 degree lee slope, and
sinuous crests. The cover (15%) and field size (80 m) are not from a source.

Martian transverse aeolian ridges are symmetric ridges; the preset uses the typical size
Hugenholtz et al. (2017) measured on 2295 TARs in HiRISE terrain models: a 25.8 m wavelength and
1.3 m height. The cover (7%) and field size (250 m) are not from a source.

References:
- M. G. A. Lapotre et al., "Large wind ripples on Mars: a record of atmospheric evolution",
  Science 353(6294):55-58, 2016. https://doi.org/10.1126/science.aaf3206
- C. H. Hugenholtz, T. E. Barchyn and E. A. Boulding, "Morphology of transverse aeolian ridges
  (TARs) on Mars from a large sample", Icarus 286, 2017.

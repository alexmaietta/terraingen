# Science notes

The derivations, measured values, design rationale and references behind TerrainGen's models. The
headers state what each function and type does; where a model rests on the literature they point
here by section name, as in `See SCIENCE.md, "Crater datums and blends"`. Sections follow the
library's modules.

## Layers

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

References:
- C. I. Fassett and B. J. Thomson, "Crater degradation on the lunar maria: Topographic diffusion
  and the rate of erosion on the Moon", JGR Planets 119:2255-2271, 2014.
  https://doi.org/10.1002/2014JE004698
- C. I. Fassett et al., JGR Planets 127, e2022JE007510, 2022 (Table 3).
- synthterrain (NASA Ames), `synthterrain.crater.profile`: `FT_Crater` and `FTmod_Crater`.
  https://github.com/NeoGeographyToolkit/synthterrain

### Complex crater profile

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

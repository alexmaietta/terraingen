"""A stack of noise layers, each combined with the heights so far by its blend mode.

Rolling fBm, domain-warped ridged crests added on top (their octave gain set from a Hurst
exponent), and broad value noise as a mask that roughens some regions and calms others. The same
terrain as examples/config/example_terrain_layered.json, plus the warp.

    python example_terrain_layered.py [OUTPUT_DIR]
"""

import terraingen
from _common import hillshade, output_dir, write_png


def main():
    out = output_dir()
    rolling = terraingen.NoiseLayer(terraingen.FractalParams(seed=1, wavelength=800.0, amplitude=40.0))
    crests = terraingen.NoiseLayer(
        terraingen.FractalParams(
            seed=2,
            type=terraingen.FractalType.RIDGED,
            wavelength=250.0,
            amplitude=8.0,
            octaves=6,
            gain=terraingen.gain_from_hurst(0.7),
        ),
        blend=terraingen.LayerBlend.ADD,
        warp_amplitude=60.0,
        warp_wavelength=400.0,
    )
    mask = terraingen.NoiseLayer(
        terraingen.FractalParams(seed=3, basis=terraingen.NoiseBasis.VALUE, wavelength=1200.0, amplitude=1.0, octaves=2),
        blend=terraingen.LayerBlend.MASK,
    )
    terrain = terraingen.Terrain(terraingen.FlatSource(513, 513, 2.0), [terraingen.NoiseRelief([rolling, crests, mask])])

    evaluated = terraingen.evaluate(terrain)
    heights = evaluated.heights.values
    print(f"heights from {heights.min():.2f} to {heights.max():.2f} m")
    terraingen.write_geotiff(out / "example_terrain_layered.tif", evaluated.heights)
    print("wrote", write_png(out / "example_terrain_layered.png", hillshade(heights, 2.0, exaggeration=3.0)))


if __name__ == "__main__":
    main()

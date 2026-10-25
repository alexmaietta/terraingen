# SPDX-License-Identifier: BSD-3-Clause
"""Mix Hapke Photometry From a Material Map."""

import terraingen

PIXEL = 1.0


def mix_hapke_photometry_from_materials():
    hills = terraingen.NoiseLayer(seed=5, wavelength=120.0, amplitude=12.0, octaves=6)
    dunes = terraingen.DuneLayer(
        terraingen.DuneOptions(shape=terraingen.DuneShape.TRANSVERSE, wavelength=30.0, height=2.0)
    )
    terrain = terraingen.TerrainGenerator(terraingen.FlatSource(201, 201, PIXEL), [hills, dunes])
    grid = terraingen.RasterGrid.from_raster(terraingen.evaluate(terrain))
    # --8<-- [start:mix_hapke_photometry_from_materials]
    options = terraingen.MaterialOptions(dust_cover=0.3, dust_patch=40.0, seed=1)
    materials = terraingen.evaluate_materials(terrain, options, grid)  # soil, sand, dust, bedrock, rock
    photometry = terraingen.mars_pancam_photometry()  # Hapke parameters per material at 753 nm
    albedo, b, c, roughness = terraingen.evaluate_photometry(materials, photometry)
    # --8<-- [end:mix_hapke_photometry_from_materials]
    return materials, albedo.values, roughness.values


def test_mix_hapke_photometry_from_materials():
    materials, albedo, roughness = mix_hapke_photometry_from_materials()
    assert len(materials) == 5
    assert albedo.shape == (201, 201)
    assert 0.0 < albedo.min() < albedo.max() <= 1.0
    assert roughness.max() > roughness.min()


def figure(quick=False):
    """The single-scattering albedo, left, and roughness, right, that soil, dune sand and dust mix to."""
    import gallery
    import matplotlib.pyplot as plt

    _, albedo, roughness = mix_hapke_photometry_from_materials()
    fig, axes = plt.subplots(1, 2, figsize=(9.0, 3.9), layout="constrained")
    for ax, values, label in zip(
        axes, (albedo, roughness), ("Single-scattering albedo", "Roughness (degrees)"), strict=True
    ):
        image = ax.imshow(values, cmap="gray", extent=gallery.extent(values, PIXEL))
        ax.grid(False)
        fig.colorbar(image, ax=ax, label=label, shrink=0.85)
    return fig

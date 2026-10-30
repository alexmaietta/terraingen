# SPDX-License-Identifier: BSD-3-Clause
"""Roughen and Stretch Crater Rims."""

import terraingen

DIAMETER = 150.0
SPACING = 240.0
CENTERS = [((column - 1) * SPACING, (1 - row) * SPACING) for row in range(3) for column in range(3)]


def roughen_and_stretch_crater_rims():
    placed = [terraingen.Crater(x, y, DIAMETER, age_gyr=0.01) for x, y in CENTERS]
    # --8<-- [start:roughen_and_stretch_crater_rims]
    irregularity = terraingen.CraterIrregularity(
        enabled=True,
        seed=3,
        selection=terraingen.CraterSelection(manual=True),  # manual craters stay round otherwise
        roughness=terraingen.ValueDistribution.constant(0.06),  # RMS deviation, in radii
        elongated_share=0.5,
        ellipticity=terraingen.ValueDistribution.uniform(1.2, 1.6),  # long over short axis
    )
    apply = terraingen.CraterApplyOptions(irregularity=irregularity)
    craters = terraingen.CraterLayer.manual(placed, apply=apply)
    # --8<-- [end:roughen_and_stretch_crater_rims]
    outlines = [
        terraingen.irregular_outline(
            terraingen.Crater(x, y, DIAMETER, id=terraingen.manual_crater_id(x, y, DIAMETER)), irregularity
        )
        for x, y in CENTERS
    ]
    return craters, outlines


def test_roughen_and_stretch_crater_rims():
    _, outlines = roughen_and_stretch_crater_rims()
    assert len(outlines) == 9
    assert any(outline.ellipticity > 1.2 for outline in outlines)
    assert all(not outline.round for outline in outlines)


def figure(quick=False):
    """Nine identical young 150 m craters with rims roughened by 0.06 radii RMS, about half of them
    stretched into ellipses, hillshaded under a low sun."""
    import gallery
    import matplotlib.pyplot as plt

    craters, outlines = roughen_and_stretch_crater_rims()
    pixel = 4.0 if quick else 1.0
    size = int(3 * SPACING / pixel) + 1
    heights = (
        terraingen.Terrain(source=terraingen.FlatSource(size, size, pixel), layers=[craters])
        .evaluate()
        .values
    )
    labels = [f"ellipticity {outline.ellipticity:.2f}" for outline in outlines]
    fig, ax = plt.subplots(figsize=(6, 6), layout="constrained")
    gallery.show_crater_grid(ax, heights, pixel, labels, SPACING)
    return fig

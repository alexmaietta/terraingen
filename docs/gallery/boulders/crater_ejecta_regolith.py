# SPDX-License-Identifier: BSD-3-Clause

"""Only craters that dig through the regolith throw blocks: fresh craters 20 to 60 m across (gray
rims), each digging a tenth of its diameter deep, with blocks of 0.3 m and up sized by Golombek and
Rapp's model at 15% rock cover, drawn to scale and colored by the height they stand above the ground.
Left to right: no regolith limit, then 3 m and 5 m of regolith, through which only the larger craters
reach bedrock."""

import gallery
import matplotlib.pyplot as plt
from matplotlib.patches import Circle

import terraingen

WIDE = True
SECTION = "Crater ejecta"

HALF_SIZE_M = 100.0
CRATERS = [(-50.0, 45.0, 20.0), (40.0, 50.0, 40.0), (-35.0, -40.0, 60.0), (55.0, -45.0, 30.0)]
THICKNESSES_M = [None, 3.0, 5.0]


def figure(quick=False):
    region = (-HALF_SIZE_M, -HALF_SIZE_M, HALF_SIZE_M, HALF_SIZE_M)
    craters = terraingen.CraterLayer.manual(
        [terraingen.Crater(x, y, diameter, age_gyr=0.0) for x, y, diameter in CRATERS]
    )

    fig, axes = plt.subplots(1, 3, figsize=(14, 4.8), layout="constrained")
    for ax, thickness in zip(axes, THICKNESSES_M, strict=True):
        options = terraingen.BoulderLayerOptions(seed=4)
        sizes = options.sizes
        sizes.min_diameter = 0.3
        options.sizes = sizes
        ejecta = options.ejecta
        ejecta.min_crater_diameter = 10.0
        ejecta.distribution = None
        ejecta.coverage = 0.15
        ejecta.regolith_thickness = thickness
        ejecta.excavation_depth_ratio = 0.1
        options.ejecta = ejecta
        table = terraingen.BoulderLayer(options, craters).boulders(region).table

        for x, y, diameter in CRATERS:
            ax.add_patch(Circle((x, y), 0.5 * diameter, fill=False, color="#888888", lw=1.0))
            ax.text(x, y, f"{diameter:g} m", ha="center", va="center", fontsize=8, color="#555555")
        circles = gallery.show_boulders(
            ax, table, region, table["height"], cmap="magma_r", vmin=0.0, vmax=1.0, colorbar=False
        )
        ax.set_title("No regolith limit" if thickness is None else f"{thickness:g} m of regolith")
    fig.colorbar(circles, ax=axes, label="Height (m)", shrink=0.85)
    return fig

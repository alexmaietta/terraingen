# SPDX-License-Identifier: BSD-3-Clause
"""Write a Degradation Model of Your Own."""

import terraingen


def write_a_degradation_model():
    # --8<-- [start:write_a_degradation_model]
    class ShallowWithAge(terraingen.DegradationModel):
        """Craters 0.2 times as deep as wide when fresh, losing half that depth every Gyr."""

        def state(self, crater):
            fresh = terraingen.CraterMorphometry(0.2, 0.04)
            share = 0.5**crater.age_gyr
            now = terraingen.CraterMorphometry(0.2 * share, 0.04 * share)
            return terraingen.CraterState(fresh, now, 0.0)

    surface = terraingen.EquilibriumSurface(
        terraingen.extended_neukum_production_function(),
        terraingen.trask_equilibrium(),
        3.5,
        terraingen.neukum_lunar_chronology,
    )
    models = terraingen.CraterModels(
        surface.density(), surface.ages(), ShallowWithAge(), terraingen.MartinParkesDunstanProfile()
    )
    craters = terraingen.CraterLayer(models)
    table = craters.craters((-100.0, -100.0, 100.0, 100.0))  # with each crater's depth ratio now
    # --8<-- [end:write_a_degradation_model]
    return table, models


def test_write_a_degradation_model():
    table, models = write_a_degradation_model()
    assert len(table["diameter"]) > 0
    assert (table["depth_ratio"] <= 0.2).all()
    assert isinstance(models.degradation, terraingen.DegradationModel)


def figure(quick=False):
    """A 100 m crater's height over diameter as it ages on Martin, Parkes and Dunstan's profile, worn
    by the model, which halves its depth and rim every Gyr (left), and by the default diffusion
    degradation (right)."""
    import matplotlib.pyplot as plt
    import numpy as np

    _, models = write_a_degradation_model()
    diameter = 100.0
    radii = np.linspace(0.0, 2.0, 401)
    ages_gyr = [0.0, 0.5, 1.0, 2.0, 3.5]
    panels = {"Shallow with age": models, "Default (diffusion)": terraingen.default_crater_models()}
    colors = plt.get_cmap("viridis")(np.linspace(0.0, 0.85, len(ages_gyr)))
    fig, axes = plt.subplots(1, 2, figsize=(10, 3.8), layout="constrained", sharey=True)
    for ax, (title, crater_models) in zip(axes, panels.items(), strict=True):
        for age, color in zip(ages_gyr, colors, strict=True):
            state = crater_models.degradation.state(terraingen.Crater(0.0, 0.0, diameter, age_gyr=age))
            curve = [crater_models.profile.height(r * 0.5 * diameter, diameter, state) for r in radii]
            ax.plot(radii, np.asarray(curve) / diameter, lw=1.4, color=color, label=f"{age:g} Gyr")
        ax.axhline(0.0, color="#b5b5b3", lw=0.6)
        ax.set_xlabel("Distance from center (radii)")
        ax.set_title(title)
    axes[0].set_ylabel("Height / diameter")
    axes[1].legend(loc="lower right")
    return fig

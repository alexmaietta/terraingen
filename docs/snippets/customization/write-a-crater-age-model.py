# SPDX-License-Identifier: BSD-3-Clause
"""Write a Crater Age Model of Your Own."""

import terraingen

REGION = (-250.0, -250.0, 250.0, 250.0)


def write_a_crater_age_model():
    # --8<-- [start:write_a_crater_age_model]
    class ErasedBySize(terraingen.CraterAgeModel):
        """Craters last 10 Myr per meter of diameter, up to 3.5 Gyr, so small ones are always young."""

        def lifetime_at(self, diameter):
            return min(0.01 * diameter, 3.5)

        def sample_age_at(self, random, diameter, lifetime_gyr):
            return lifetime_gyr * random.next_double()  # optional; uniform when left out

    ages = ErasedBySize()
    models = terraingen.CraterModels(
        terraingen.PowerLawSfd(0.05, 2.0),
        ages,
        terraingen.FreshnessDegradation(ages, terraingen.lunar_fresh_crater_morphometry),
        terraingen.MartinParkesDunstanProfile(),
    )
    options = terraingen.CraterLayerOptions(
        seed=4, generation=terraingen.CraterGenerationOptions(d_min=4.0, d_max=200.0)
    )
    table = terraingen.CraterLayer(models, options).craters(REGION)  # with each crater's age
    # --8<-- [end:write_a_crater_age_model]
    return table


def test_write_a_crater_age_model():
    table = write_a_crater_age_model()
    assert len(table["diameter"]) > 0
    assert (table["age_gyr"] <= 0.01 * table["diameter"] + 1e-9).all()


def figure(quick=False):
    """Each crater's age by its diameter, under the lifetime that grows with size (left), and the
    craters drawn to scale and colored by age (right)."""
    import gallery
    import matplotlib.pyplot as plt
    import numpy as np

    table = write_a_crater_age_model()
    fig, (ages, field) = plt.subplots(1, 2, figsize=(10, 4.2), layout="constrained")
    ages.scatter(table["diameter"], table["age_gyr"], s=6, alpha=0.6)
    diameters = np.geomspace(4.0, 200.0, 100)
    ages.plot(diameters, np.minimum(0.01 * diameters, 3.5), color="#b5b5b3", lw=1.2, label="Lifetime")
    ages.set_xscale("log")
    ages.set_xlabel("Diameter (m)")
    ages.set_ylabel("Age (Gyr)")
    ages.legend(loc="upper left")
    gallery.show_craters(field, table, REGION, table["age_gyr"], label="Age (Gyr)")
    return fig

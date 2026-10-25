# SPDX-License-Identifier: BSD-3-Clause
"""Choose How Boulders Are Sized."""

import numpy as np

import terraingen

REGION = (-25.0, -25.0, 25.0, 25.0)


def choose_boulder_sizes():
    # --8<-- [start:choose_boulder_sizes]
    nominal = terraingen.BoulderSizes()  # NASA SP-8023's nominal intercrater curve
    rocky = terraingen.BoulderSizes(distribution=None, coverage=0.1)  # Golombek and Rapp, 10% cover
    power_law = terraingen.BoulderSizes(distribution=terraingen.PowerLawSfd(0.005, 2.5))
    tables = {}
    for name, sizes in {"SP-8023": nominal, "10% cover": rocky, "Power law": power_law}.items():
        sizes.min_diameter, sizes.max_diameter = 0.3, 4.0  # meters
        options = terraingen.BoulderLayerOptions(sizes=sizes, seed=4)
        tables[name] = terraingen.BoulderLayer(options).boulders(REGION).table
    # --8<-- [end:choose_boulder_sizes]
    return tables


def test_choose_boulder_sizes():
    tables = choose_boulder_sizes()
    for table in tables.values():
        assert len(table["diameter"]) > 0
        assert table["diameter"].min() >= 0.3 and table["diameter"].max() < 4.0
    assert len(tables["10% cover"]["diameter"]) > len(tables["SP-8023"]["diameter"])


def figure(quick=False):
    """How many boulders of each size the three choices place on 50 m, per square meter."""
    import matplotlib.pyplot as plt

    tables = choose_boulder_sizes()
    area = (REGION[2] - REGION[0]) * (REGION[3] - REGION[1])
    fig, ax = plt.subplots(figsize=(5.5, 4.0), layout="constrained")
    for name, table in tables.items():
        diameters = np.sort(table["diameter"])[::-1]
        count = len(diameters)
        ax.loglog(diameters, np.arange(1, count + 1) / area, lw=1.6, label=f"{name}, {count} boulders")
    ax.set_xlabel("Boulder diameter D (m)")
    ax.set_ylabel("Boulders at least D across per m$^2$")
    ax.set_xticks([0.3, 0.5, 1.0, 2.0, 4.0], ["0.3", "0.5", "1", "2", "4"])
    ax.set_xticks([], minor=True)
    ax.legend()
    return fig

# SPDX-License-Identifier: BSD-3-Clause
"""Choose a Crater Profile."""

import numpy as np

import terraingen

DIAMETER = 100.0
RADII = np.linspace(0.0, 2.0, 801)


def choose_a_crater_profile():
    # --8<-- [start:choose_a_crater_profile]
    profiles = {
        "Martin, Parkes and Dunstan": terraingen.MartinParkesDunstanProfile(),
        "Fassett and Thomson": terraingen.FassettThomsonProfile(),
        "Fassett and Thomson, plateau": terraingen.FassettThomsonProfile(
            terraingen.FassettThomsonRim.PLATEAU
        ),
    }
    fresh = terraingen.lunar_fresh_crater_morphometry(DIAMETER)
    state = terraingen.CraterState(fresh, fresh)  # fresh and current shape alike
    heights = {
        name: [profile.height(r * 0.5 * DIAMETER, DIAMETER, state) for r in RADII]
        for name, profile in profiles.items()
    }
    # --8<-- [end:choose_a_crater_profile]
    return profiles, heights


def test_choose_a_crater_profile():
    profiles, heights = choose_a_crater_profile()
    assert len(profiles) == 3
    for name, curve in heights.items():
        assert curve[0] < 0.0, name
        assert max(curve) > 0.0, name
    curves = list(heights.values())
    assert not np.allclose(curves[0], curves[1])


def figure(quick=False):
    """Fresh 100 m crater profiles as height over diameter, across the crater (left) and zoomed on
    the rim (right), where the two Fassett and Thomson rims part."""
    import matplotlib.pyplot as plt

    _, heights = choose_a_crater_profile()
    fig, (whole, rim) = plt.subplots(1, 2, figsize=(10, 3.8), layout="constrained", width_ratios=[1.6, 1])
    for (name, curve), style in zip(heights.items(), ("-", "-", "--"), strict=True):
        whole.plot(RADII, np.asarray(curve) / DIAMETER, lw=1.4, ls=style, label=name)
        rim.plot(RADII, np.asarray(curve) / DIAMETER, lw=1.4, ls=style)
    whole.axhline(0.0, color="#b5b5b3", lw=0.6)
    whole.set_xlabel("Distance from center (radii)")
    whole.set_ylabel("Height / diameter")
    whole.legend(loc="lower right")
    rim.set_xlim(0.85, 1.25)
    rim.set_ylim(0.0, 0.045)
    rim.set_xlabel("Distance from center (radii)")
    rim.set_title("At the rim")
    return fig

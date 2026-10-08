"""Cumulative size-frequency distributions (CSFDs) of the built-in crater models.

Each distribution gives N(>= D), the number of craters at least D metres across per square metre.
This example tabulates the lunar production on surfaces of several ages (Neukum's production and
chronology), the equilibria of Trask and Hartmann, the DSNE lunar surface (production capped at
equilibrium), Moore's terrain classes and the VIPER specification, and Mars. It writes the table as
CSV and a log-log plot (legend printed below).

    python example_distributions_csfd.py [OUTPUT_DIR]
"""

import numpy as np

import terraingen
from _common import colour_name, line_plot, output_dir, write_png


def curve(distribution, diameters):
    low, high = distribution.valid_range
    return np.array([distribution.cumulative(d) if low <= d <= high else np.nan for d in diameters])


def main():
    out = output_dir()
    diameters = np.geomspace(3.0, 2000.0, 60)
    production = terraingen.extended_neukum_production_function()
    distributions = {
        "production 0.1 Gyr": terraingen.ProductionSurface(production, 0.1),
        "production 1 Gyr": terraingen.ProductionSurface(production, 1.0),
        "production 3.8 Gyr": terraingen.ProductionSurface(production, 3.8),
        "Trask equilibrium": terraingen.trask_equilibrium(),
        "Hartmann equilibrium": terraingen.hartmann_equilibrium(),
        "lunar surface 3.8 Gyr": terraingen.lunar_surface(3.8).density,
        "Moore rough mare": terraingen.moore_distribution(terraingen.MooreTerrain.ROUGH_MARE),
        "Mars surface 3 Gyr": terraingen.mars_surface(3.0).density,
    }
    columns = {name: curve(sfd, diameters) for name, sfd in distributions.items()}

    rows = ["diameter_m," + ",".join(columns)]
    rows += [f"{d:.4g}," + ",".join(f"{columns[n][i]:.6g}" for n in columns) for i, d in enumerate(diameters)]
    (out / "example_distributions_csfd.csv").write_text("\n".join(rows) + "\n")

    print("N(>= D) per km^2:")
    print(f"{'':24s}" + "".join(f"{d:>10.0f} m" for d in (10, 100, 1000)))
    for name, sfd in distributions.items():
        values = curve(sfd, [10.0, 100.0, 1000.0]) * 1e6
        print(f"{name:24s}" + "".join(f"{v:12.4g}" for v in values))
    image = line_plot([(diameters, columns[n]) for n in columns], log_x=True, log_y=True)
    print("legend:", ", ".join(f"{colour_name(i)} {n}" for i, n in enumerate(columns)))
    print("wrote", write_png(out / "example_distributions_csfd.png", image))


if __name__ == "__main__":
    main()

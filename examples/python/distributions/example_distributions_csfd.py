"""Cumulative size-frequency distributions (CSFDs) of the built-in crater models.

Each distribution gives N(>= D), the number of craters at least D metres across per square metre.
This example tabulates the lunar production on surfaces of several ages (Neukum's production and
chronology), the equilibria of Trask and Hartmann, the DSNE lunar surface (production capped at
equilibrium), Moore's terrain classes and the VIPER specification, and Mars. It writes the table as
CSV and a log-log plot (legend printed below).

    python example_distributions_csfd.py [--viewer window|null] [--output-dir DIR] [--test]
"""

import pathlib
import sys

import numpy as np

import terraingen

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import _common
from _common import color_name, line_plot, write_png


def curve(distribution, diameters):
    low, high = distribution.valid_range
    return np.array([distribution.cumulative(d) if low <= d <= high else np.nan for d in diameters])


class Example(_common.Example):
    def generate(self):
        self.diameters = np.geomspace(3.0, 2000.0, 60)
        production = terraingen.extended_neukum_production_function()
        chronology = terraingen.neukum_lunar_chronology
        equilibrium = terraingen.trask_equilibrium()
        distributions = {
            "production 0.1 Gyr": terraingen.ProductionSurface(production, 0.1, chronology),
            "production 1 Gyr": terraingen.ProductionSurface(production, 1.0, chronology),
            "production 3.8 Gyr": terraingen.ProductionSurface(production, 3.8, chronology),
            "Trask equilibrium": equilibrium,
            "Hartmann equilibrium": terraingen.hartmann_equilibrium(),
            "lunar surface 3.8 Gyr": terraingen.MinimumSfd(
                [terraingen.ProductionSurface(production, 3.8, chronology), equilibrium]
            ),
            "Moore rough mare": terraingen.moore_distribution(terraingen.MooreTerrain.ROUGH_MARE),
            "Mars surface 3 Gyr": terraingen.ProductionSurface(
                terraingen.ivanov_mars_production_function(), 3.0, terraingen.ivanov_mars_chronology
            ),
        }
        self.columns = {name: curve(sfd, self.diameters) for name, sfd in distributions.items()}

        print("N(>= D) per km^2:")
        print(f"{'':24s}" + "".join(f"{d:>10.0f} m" for d in (10, 100, 1000)))
        self.table = {}
        for name, sfd in distributions.items():
            self.table[name] = curve(sfd, [10.0, 100.0, 1000.0]) * 1e6
            print(f"{name:24s}" + "".join(f"{v:12.4g}" for v in self.table[name]))
        print("legend:", ", ".join(f"{color_name(i)} {n}" for i, n in enumerate(self.columns)))

    def save(self, out):
        rows = ["diameter_m," + ",".join(self.columns)]
        rows += [
            f"{d:.4g}," + ",".join(f"{self.columns[n][i]:.6g}" for n in self.columns)
            for i, d in enumerate(self.diameters)
        ]
        (out / "example_distributions_csfd.csv").write_text("\n".join(rows) + "\n")
        image = line_plot([(self.diameters, self.columns[n]) for n in self.columns], log_x=True, log_y=True)
        print("wrote", write_png(out / "example_distributions_csfd.png", image))

    def test_final(self):
        assert len(self.columns) == 8 and all(len(c) == 60 for c in self.columns.values()), (
            "eight curves of 60 points"
        )
        young, middle, old = (self.table[f"production {age} Gyr"] for age in ("0.1", "1", "3.8"))
        assert np.all(young < middle) and np.all(middle < old), (
            "older surfaces have more craters at every size"
        )
        for name, values in self.columns.items():
            finite = values[np.isfinite(values)]
            assert len(finite) > 10 and np.all(finite > 0), f"{name} is positive over its range"
            assert np.all(np.diff(finite) <= 0), f"{name} falls with diameter"
        assert self.table["lunar surface 3.8 Gyr"][1] <= self.table["production 3.8 Gyr"][1], (
            "equilibrium caps the lunar surface"
        )


if __name__ == "__main__":
    _common.main(Example)

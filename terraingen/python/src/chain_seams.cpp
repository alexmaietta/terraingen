// SPDX-License-Identifier: BSD-3-Clause

#include "bindings.h"
#include "common.h"
#include "options.h"

#include "terraingen/sources/source_chain.h"

#include <nanobind/operators.h>

namespace terraingen::python {

void BindChainSeams(nb::module_& m) {
    BindEnum<ChainSeamMode>(m, "ChainSeamMode",
                            "How a source chain joins a source to the gaps of those before it.");
    auto seams = nb::class_<ChainSeams>(
        m, "ChainSeams",
        "How a source chain matches each source to the gaps of those before it: `mode`, the "
        "feather band's `width` and the `decay_length` over which height and slope fade out "
        "beyond an open edge, both in the sources' units (meters on a plane, degrees on a body).");
    BindKwargsInit<ChainSeams>(seams);
    seams.def_rw("mode", &ChainSeams::mode)
        .def_rw("width", &ChainSeams::width)
        .def_rw("decay_length", &ChainSeams::decayLength)
        .def("validate", &ValidateChainSeams, kValidateDoc)
        .def(nb::self == nb::self);
    m.def("validate_chain_seams", &ValidateChainSeams, "seams"_a, kValidateDoc);
    m.def("seam_reach", &SeamReach, "seams"_a,
          "The distance from a gap's edge, in the sources' units, over which `seams` change the "
          "later source.");
    m.def("seam_lattice", &SeamLattice, "grid"_a, "seams"_a,
          "The lattice a seam at the edge of a source covering `grid` is solved on: its posts, "
          "widened by the seam's reach and 3 more posts.");
    m.attr("SEAM_REACH_DECAYS") = kSeamReachDecays;
}

}  // namespace terraingen::python

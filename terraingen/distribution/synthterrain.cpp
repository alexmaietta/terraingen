#include "terraingen/distribution/synthterrain.h"

#include <stdexcept>

namespace terraingen {

PowerLawSfd SynthterrainBoulderDistribution(SynthterrainBoulders boulders) {
    switch (boulders) {
    case SynthterrainBoulders::InterCrater:
        return PowerLawSfd(0.0001, 1.75457);
    case SynthterrainBoulders::Haworth:
        return PowerLawSfd(0.002, 2.6607);
    case SynthterrainBoulders::ViperSpecification:
        return PowerLawSfd(0.0003, 2.482);
    }
    throw std::invalid_argument("Unknown synthterrain boulder distribution");
}

}  // namespace terraingen

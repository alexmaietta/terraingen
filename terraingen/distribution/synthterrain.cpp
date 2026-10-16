/// @file
/// @brief The boulder size distributions synthterrain offers.
#include "terraingen/distribution/synthterrain.h"

#include <stdexcept>

namespace terraingen {

std::shared_ptr<PowerLawSfd> SynthterrainBoulderDistribution(SynthterrainBoulders boulders) {
    switch (boulders) {
    case SynthterrainBoulders::InterCrater:
        return std::make_shared<PowerLawSfd>(0.0001, 1.75457);
    case SynthterrainBoulders::Haworth:
        return std::make_shared<PowerLawSfd>(0.002, 2.6607);
    case SynthterrainBoulders::ViperSpecification:
        return std::make_shared<PowerLawSfd>(0.0003, 2.482);
    }
    throw std::invalid_argument("Unknown synthterrain boulder distribution");
}

}  // namespace terraingen

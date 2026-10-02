#include "terraingen/features/craters/lunar/chronology.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

double NeukumLunarChronology(double ageGyr) {
    if (!(ageGyr >= 0.0 && ageGyr <= kMaxLunarAgeGyr)) {
        throw std::domain_error("Lunar surface age must be between 0 and 4.6 Gyr");
    }
    return (5.44e-14 * std::expm1(6.93 * ageGyr) + 8.38e-4 * ageGyr) / 1e6;
}

double NeukumLunarChronologyRate(double ageGyr) {
    if (!(ageGyr >= 0.0 && ageGyr <= kMaxLunarAgeGyr)) {
        throw std::domain_error("Lunar surface age must be between 0 and 4.6 Gyr");
    }
    return (5.44e-14 * 6.93 * std::exp(6.93 * ageGyr) + 8.38e-4) / 1e6;
}

}  // namespace terraingen

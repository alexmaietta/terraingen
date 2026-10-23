// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/profiles/fassett_thomson.h"

#include <cstdint>

namespace terraingen {

FassettThomsonProfile::FassettThomsonProfile(FassettThomsonRim rim) {
    kernel_.simple = CraterProfileKind::FassettThomson;
    kernel_.rim = static_cast<std::uint32_t>(rim);
}

double FassettThomsonProfile::Height(double distance, double diameter,
                                     const CraterState& state) const {
    return static_cast<double>(SimpleProfileHeight(kernel_, static_cast<float>(distance),
                                                   static_cast<float>(diameter),
                                                   KernelStateRecord(diameter, state)));
}

}  // namespace terraingen

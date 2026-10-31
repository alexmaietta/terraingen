// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/profiles/fassett_thomson.h"

#include <cstdint>

namespace terraingen {

FassettThomsonProfile::FassettThomsonProfile(FassettThomsonRim rim) {
    m_kernel.simple = CraterProfileKind::FassettThomson;
    m_kernel.rim = static_cast<std::uint32_t>(rim);
}

double FassettThomsonProfile::Height(double distance, double diameter,
                                     const CraterState& state) const {
    return static_cast<double>(SimpleProfileHeight(m_kernel, static_cast<float>(distance),
                                                   static_cast<float>(diameter),
                                                   KernelStateRecord(diameter, state)));
}

}  // namespace terraingen

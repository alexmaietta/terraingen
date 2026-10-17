/// @file
/// @brief `CellPosition`, where a cell kernel evaluates; a kernel header for C++ and Slang.
#pragma once

#include "terraingen/util/portable.h"

namespace terraingen {

/// A cell kernel's position: a cell centre or point measured from the layer's origin, in metres,
/// so single precision stays accurate far from the frame's origin.
struct CellPosition {
    float x = 0.0f;  ///< East of the origin.
    float y = 0.0f;  ///< North of the origin.
};

}  // namespace terraingen

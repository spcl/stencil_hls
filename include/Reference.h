/// @author    Johannes de Fine Licht (definelicht@inf.ethz.ch)
/// @date      March 2017
/// @copyright This software is copyrighted under the BSD 3-Clause License.

#pragma once

#include <vector>
#include "Stencil.h"

std::vector<Data_t> Reference(std::vector<Data_t> const &input,
                              const int timesteps);

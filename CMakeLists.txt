# Author:  Johannes de Fine Licht (johannes.definelicht@inf.ethz.ch)
# Created: March 2017
cmake_minimum_required(VERSION 2.8.12)
project(Stencil CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

# Target platform
set(STENCIL_PART_NAME "xcku115-flvb2104-2-e" CACHE STRING "Vivado HLS part name.") 
set(STENCIL_DSA_STRING "xilinx:tul-pcie3-ku115:2ddr:3.1" CACHE STRING "SDAccel DSA string")
set(STENCIL_KEEP_INTERMEDIATE OFF CACHE STRING "Keep intermediate SDAccel files")
set(STENCIL_TARGET_TIMING 4 CACHE STRING "Target timing for HLS in nanoseconds. Will not be used when building the kernel.")
set(STENCIL_TARGET_CLOCK 250 CACHE STRING "Target clock when building hardware kernel.")
set(STENCIL_USE_SINGLE_DIMM OFF CACHE STRING "Restrict to using a single DDR DIMM")

# Domain configuration
set(STENCIL_DATA_TYPE "float" CACHE STRING "Data type used for computations.")
set(STENCIL_DATA_WIDTH 4 CACHE STRING "Width of data path in number of elements.")
set(STENCIL_ROWS 256 CACHE STRING "Number of rows.")
set(STENCIL_COLS 256 CACHE STRING "Number of cols. Will be divided by data width * number of blocks.")
set(STENCIL_TIME 128 CACHE STRING "Number of timesteps to evaluate. Will be folded by the stencil depth.")
set(STENCIL_DEPTH 4 CACHE STRING "Number of consecutive compute elements, deciding the folding factor of the number of timesteps.")
set(STENCIL_BLOCKS 4 CACHE STRING "Number of tiles to divide the column into.")

# External dependencies
find_package(SDAccel REQUIRED)
if (NOT SDACCEL_FOUND)
  message(FATAL_ERROR "SDAccel not found.")
else()
  include_directories(${SDACCEL_INCLUDE_DIR})
endif()
include_directories(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/include ${SDACCEL_INCLUDE_DIRS} ${SDACCEL_ROOT_DIR}/Vivado_HLS/include)
find_package(Threads REQUIRED)
set(STENCIL_LIBS ${SDACCEL_LIBS})

# Compilation flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -ftemplate-depth-1024")

# Target dependencies 
set(STENCIL_HLS_DEPENDS
    include/Stencil.h.in
    src/Stencil.cpp
    include/hlslib/SDAccel.h
    include/hlslib/DataPack.h
    include/hlslib/Stream.h)

# Configure files 
configure_file(include/Stencil.h.in Stencil.h)
configure_file(scripts/Synthesis.tcl.in Synthesis.tcl)

# Synthesis
add_custom_target(synthesis
  COMMAND ${SDACCEL_VIVADO_HLS} -f Synthesis.tcl
  DEPENDS ${STENCIL_HLS_DEPENDS})

# Testing
enable_testing()
add_executable(Testbench test/Testbench.cpp src/Stencil.cpp)
target_link_libraries(Testbench ${STENCIL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
add_test(Testbench Testbench)

# SDAccel
add_executable(ExecuteKernel host/ExecuteKernel.cpp)
target_link_libraries(ExecuteKernel ${STENCIL_LIBS})
set(STENCIL_XOCC_FLAGS ${STENCIL_XOCC_FLAGS} 
  # Includes
  -I${CMAKE_BINARY_DIR}
  -I${CMAKE_SOURCE_DIR}/include
  # Source
  ${CMAKE_SOURCE_DIR}/src/Stencil.cpp
  # Flags
  --kernel Kernel 
  --xdevice ${STENCIL_DSA_STRING}
  --xp prop:kernel.Kernel.kernel_flags="${CMAKE_CXX_FLAGS} -DSTENCIL_SYNTHESIS -DHLSLIB_SYNTHESIS"
  --kernel_frequency=${STENCIL_TARGET_CLOCK})
if (NOT STENCIL_USE_SINGLE_DIMM)
  set(STENCIL_XOCC_FLAGS ${STENCIL_XOCC_FLAGS}
      --xp misc:map_connect=add.kernel.Kernel_1.M_AXI_GMEM0.core.OCL_REGION_0.M00_AXI
      --xp misc:map_connect=add.kernel.Kernel_1.M_AXI_GMEM1.core.OCL_REGION_0.M01_AXI
      --max_memory_ports all)
endif()
if (STENCIL_KEEP_INTERMEDIATE)
  set(STENCIL_XOCC_FLAGS ${STENCIL_XOCC_FLAGS} -s)
endif()
add_custom_target(kernel
  COMMAND ${SDACCEL_XOCC} -t hw ${STENCIL_XOCC_FLAGS}
  -o sdaccel_hw.xclbin
  DEPENDS ${STENCIL_HLS_DEPENDS})

# Author:  Johannes de Fine Licht (johannes.definelicht@inf.ethz.ch)
# Created: March 2017
cmake_minimum_required(VERSION 2.8.12)
project(Stencil CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

# Target platform
set(STENCIL_TARGET "TUL-KU115-XPR" CACHE STRING "Target board for benchmark.")
if (STENCIL_TARGET STREQUAL "TUL-KU115")
  set(STENCIL_PART_NAME "xcku115-flvb2104-2-e") 
  set(STENCIL_DSA_STRING "xilinx:tul-pcie3-ku115:2ddr:3.1")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTENCIL_TUL_KU115")
  set(STENCIL_DIMMS 2)
elseif (STENCIL_TARGET STREQUAL "TUL-KU115-XPR")
  set(STENCIL_PART_NAME "xcku115-flvb2104-2-e") 
  set(STENCIL_DSA_STRING "xilinx:xil-accel-rd-ku115:4ddr-xpr:3.2")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTENCIL_TUL_KU115_XPR")
  set(STENCIL_DIMMS 4)
elseif (STENCIL_TARGET STREQUAL "ADM-KU3")
  set(STENCIL_PART_NAME "xcku060-ffva1156-2-e") 
  set(STENCIL_DSA_STRING "xilinx:adm-pcie-ku3:2ddr:3.2")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTENCIL_ADM_KU3")
  set(STENCIL_DIMMS 2)
elseif (STENCIL_TARGET STREQUAL "ADM-KU3-XPR")
  set(STENCIL_PART_NAME "xcku060-ffva1156-2-e") 
  set(STENCIL_DSA_STRING "xilinx:adm-pcie-ku3:2ddr-xpr:3.2")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTENCIL_ADM_KU3_XPR")
  set(STENCIL_DIMMS 2)
elseif (STENCIL_TARGET STREQUAL "ADM-7V3")
  set(STENCIL_PART_NAME "xc7vx690tffg1157-2") 
  set(STENCIL_DSA_STRING "xilinx:adm-pcie-7v3:1ddr:3.0")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTENCIL_ADM_7V3")
  set(STENCIL_DIMMS 1)
else()
  message(FATAL_ERROR "Unknown target \"${STENCIL_TARGET}\".")
endif()
set(STENCIL_KEEP_INTERMEDIATE OFF CACHE STRING
    "Keep intermediate SDAccel files")

# Domain configuration
set(STENCIL_TIME 32 CACHE STRING "Number of timesteps.")
set(STENCIL_MEMORY_WIDTH 16 CACHE STRING "Width of memory port.")
set(STENCIL_KERNEL_WIDTH 4 CACHE STRING "Width of kernel data path.")
set(STENCIL_DEPTH 8 CACHE STRING "Depth of pipeline (determines halo size.)")
set(STENCIL_BLOCKS 4 CACHE STRING "Number of blocks.")
set(STENCIL_ROWS 8192 CACHE STRING "Number of rows.")
set(STENCIL_COLS 8192 CACHE STRING "Number of columns.")
set(STENCIL_TARGET_CLOCK 200 CACHE STRING "Target clock speed.")
set(STENCIL_HLS_TARGET_TIMING 5 CACHE STRING "Target timing of HLS.")

# Dependencies
find_package(SDAccel REQUIRED)
if (NOT SDACCEL_FOUND)
  message(FATAL_ERROR "SDAccel not found.")
else()
  include_directories(${SDACCEL_INCLUDE_DIR})
endif()
include_directories(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/include ${SDACCEL_INCLUDE_DIRS} ${SDACCEL_ROOT_DIR}/Vivado_HLS/include)
set(STENCIL_LIBS ${SDACCEL_LIBS})

# Compilation flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

# Dependencies
set(STENCIL_HLS_DEPENDS
    include/Stencil.h.in
    include/Reference.h
    src/Stencil.cpp
    src/Reference.cpp
    include/hlslib/SDAccel.h
    include/hlslib/DataPack.h
    include/hlslib/Utility.h
    include/hlslib/Stream.h)

# Configure files 
set(STENCIL_KERNEL_STRING
    "jacobi_${STENCIL_DATA_TYPE}_${STENCIL_KERNEL_WIDTH}_${STENCIL_KERNEL_DEPTH}_${STENCIL_DEPTH}_${STENCIL_ROWS}x${STENCIL_COLS}.xclbin")
configure_file(include/Stencil.h.in Stencil.h)
configure_file(scripts/Synthesis.tcl.in Synthesis.tcl)

# Synthesis
add_custom_target(synthesis
  COMMAND ${SDACCEL_VIVADO_HLS} -f Synthesis.tcl
  DEPENDS ${STENCIL_HLS_DEPENDS})

# Testing
enable_testing()
find_package(Threads)
if (Threads_FOUND)
  add_executable(Testbench src/Testbench.cpp src/Stencil.cpp src/Reference.cpp)
  target_link_libraries(Testbench ${STENCIL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
  add_test(Testbench Testbench)
else()
  message(WARNING "Threads not found. Testbench will be unavailable.")
endif()

# SDAccel
add_executable(ExecuteKernel.exe src/ExecuteKernel.cpp src/Reference.cpp)
target_link_libraries(ExecuteKernel.exe ${STENCIL_LIBS})
set(STENCIL_XOCC_FLAGS ${STENCIL_XOCC_FLAGS} 
  -O3
  # Includes
  -I${CMAKE_BINARY_DIR}
  -I${CMAKE_SOURCE_DIR}
  # Source
  ${CMAKE_SOURCE_DIR}/src/Stencil.cpp
  # Flags
  --kernel Jacobi 
  --xdevice ${STENCIL_DSA_STRING}
  --xp prop:kernel.${STENCIL_ENTRY_FUNCTION}.kernel_flags="${CMAKE_CXX_FLAGS} -DHLSLIB_SYNTHESIS -DSTENCIL_SYNTHESIS"
  --kernel_frequency ${STENCIL_TARGET_CLOCK})

# if (STENCIL_DIMMS GREATER 3)
#   set(STENCIL_XOCC_FLAGS ${STENCIL_XOCC_FLAGS}
#     --xp misc:map_connect=add.kernel.${STENCIL_ENTRY_FUNCTION}_1.M_AXI_GMEM2.core.OCL_REGION_0.M02_AXI
#     --xp misc:map_connect=add.kernel.${STENCIL_ENTRY_FUNCTION}_1.M_AXI_GMEM3.core.OCL_REGION_0.M03_AXI)
# endif()
# if (STENCIL_DIMMS GREATER 1)
#   set(STENCIL_XOCC_FLAGS ${STENCIL_XOCC_FLAGS}
#     --xp misc:map_connect=add.kernel.${STENCIL_ENTRY_FUNCTION}_1.M_AXI_GMEM0.core.OCL_REGION_0.M00_AXI
#     --xp misc:map_connect=add.kernel.${STENCIL_ENTRY_FUNCTION}_1.M_AXI_GMEM1.core.OCL_REGION_0.M01_AXI
#     --max_memory_ports all)
# endif()

add_custom_target(kernel
  COMMAND ${SDACCEL_XOCC} -t hw ${STENCIL_XOCC_FLAGS} -o
  ${STENCIL_KERNEL_STRING} 
  DEPENDS ${STENCIL_HLS_DEPENDS})

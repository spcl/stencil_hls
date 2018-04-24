# Author:  Johannes de Fine Licht (johannes.definelicht@inf.ethz.ch)
# Created: March 2017
cmake_minimum_required(VERSION 2.8.12)
project(Stencil C CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/hlslib/cmake)

# Target platform
set(STENCIL_PART_NAME "xcku115-flvb2104-2-e") 
set(STENCIL_DSA_STRING "xilinx:xil-accel-rd-ku115:4ddr-xpr:4.0")
set(STENCIL_DIMMS 2 CACHE STRING "Number of DDR DIMMs to target")
set(STENCIL_SINGLE_SLR OFF CACHE STRING "Only place and route on a single SLR of the KU115")

# User configuration
set(STENCIL_DATA_TYPE "float" CACHE STRING "Data type.")
set(STENCIL_TIME 32 CACHE STRING "Number of timesteps.")
set(STENCIL_MEMORY_WIDTH 16 CACHE STRING "Width of memory port.")
set(STENCIL_KERNEL_WIDTH 4 CACHE STRING "Width of kernel data path.")
set(STENCIL_DEPTH 8 CACHE STRING "Depth of pipeline (determines halo size.)")
set(STENCIL_BLOCKS 4 CACHE STRING "Number of blocks.")
set(STENCIL_ROWS 8192 CACHE STRING "Number of rows.")
set(STENCIL_COLS 8192 CACHE STRING "Number of columns.")
set(STENCIL_TARGET_CLOCK 300 CACHE STRING "Target clock speed.")
set(STENCIL_TIMING_UNCERTAINTY 1.08 CACHE STRING "Uncertainty on the timing allowed in HLS.")
set(STENCIL_KEEP_INTERMEDIATE ON CACHE STRING "Keep intermediate SDAccel files")
set(STENCIL_ADD_CORE OFF CACHE STRING "")                                
set(STENCIL_MULT_CORE OFF CACHE STRING "")  

# Internal
if(STENCIL_DIMMS AND (NOT (STENCIL_DIMMS EQUAL STENCIL_DIMMS_DEFAULT)))
  if((NOT STENCIL_DIMMS EQUAL 1) AND (NOT STENCIL_DIMMS EQUAL 2))
    message(FATAL_ERROR "Unsupported number of DIMMs: ${STENCIL_DIMMS} (must be 1 or 2).")
  endif()
  if(STENCIL_DIMMS GREATER STENCIL_DIMMS_MAX)
    message(FATAL_ERROR "Unsupported number of DIMMS for target ${STENCIL_TARGET}: ${STENCIL_DIMMS} (maximum is ${STENCIL_DIMMS_MAX})")
  endif()
  set(STENCIL_DIMMS_INTERNAL ${STENCIL_DIMMS})
else()
  set(STENCIL_DIMMS_INTERNAL ${STENCIL_DIMMS_DEFAULT})
endif()
if(STENCIL_DIMMS_INTERNAL EQUAL 1)
  set(STENCIL_ENTRY_FUNCTION "Jacobi")
elseif(STENCIL_DIMMS_INTERNAL EQUAL 2)
  set(STENCIL_ENTRY_FUNCTION "JacobiTwoDimms")
endif()
mark_as_advanced(STENCIL_DIMMS_INTERNAL)
mark_as_advanced(STENCIL_ENTRY_FUNCTION)

# Dependencies
find_package(SDAccel REQUIRED)
include_directories(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/hlslib/include ${SDAccel_INCLUDE_DIRS})
set(STENCIL_LIBS ${SDAccel_LIBRARIES})

# Compilation flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
if(STENCIL_ADD_CORE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTENCIL_ADD_CORE=${STENCIL_ADD_CORE}") 
endif() 
if(STENCIL_MULT_CORE) 
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTENCIL_MULT_CORE=${STENCIL_MULT_CORE}") 
endif() 

# Source
set(STENCIL_SRC
    src/Stencil.cpp
    src/Reference.cpp
    src/Memory.cpp)

# Configure files 
set(STENCIL_KERNEL_STRING
  "jacobi_${STENCIL_TARGET}_${STENCIL_DATA_TYPE}_c${STENCIL_TARGET_CLOCK}_w${STENCIL_KERNEL_WIDTH}_d${STENCIL_DEPTH}_${STENCIL_ROWS}x${STENCIL_COLS}_b${STENCIL_BLOCKS}_t${STENCIL_TIME}.xclbin")
configure_file(include/Stencil.h.in Stencil.h)
configure_file(scripts/Synthesis.tcl.in Synthesis.tcl)

# Restrict kernel to single SLR
configure_file(scripts/O_opt_design_pre.tcl.in data/sdx/xocc/O_opt_design_pre.tcl) 
configure_file(scripts/optMap.xml data/sdx/xocc/optMap.xml) 

# Synthesis
add_custom_target(synthesis
  COMMAND ${SDAccel_VIVADO_HLS} -f Synthesis.tcl
  DEPENDS ${STENCIL_HLS_DEPENDS})

# Library files
add_library(stencil ${STENCIL_SRC})
set(STENCIL_LIBS ${STENCIL_LIBS} stencil)

# Testing
enable_testing()
find_package(Threads)
if (Threads_FOUND)
  add_executable(Testbench src/Testbench.cpp)
  target_link_libraries(Testbench ${STENCIL_LIBS} ${CMAKE_THREAD_LIBS_INIT})
  add_test(Testbench Testbench)
else()
  message(WARNING "Threads not found. Testbench will be unavailable.")
endif()

# Stats
add_executable(Stats src/Stats.cpp)
target_link_libraries(Stats ${STENCIL_LIBS})

# SDAccel
add_executable(ExecuteKernel.exe src/ExecuteKernel.cpp)
target_link_libraries(ExecuteKernel.exe ${STENCIL_LIBS})
set(STENCIL_XOCC_FLAGS ${STENCIL_XOCC_FLAGS} 
  -t hw
  # Includes
  -I${CMAKE_BINARY_DIR}
  -I${CMAKE_SOURCE_DIR}/include
  -I${CMAKE_SOURCE_DIR}/hlslib/include
  # Source
  ${CMAKE_SOURCE_DIR}/src/Stencil.cpp
  ${CMAKE_SOURCE_DIR}/src/Memory.cpp
  # Flags
  --kernel ${STENCIL_ENTRY_FUNCTION} 
  --platform ${STENCIL_DSA_STRING}
  --xp prop:kernel.${STENCIL_ENTRY_FUNCTION}.kernel_flags="${CMAKE_CXX_FLAGS} -DHLSLIB_SYNTHESIS -DSTENCIL_SYNTHESIS"
  --kernel_frequency ${STENCIL_TARGET_CLOCK})
if(STENCIL_KEEP_INTERMEDIATE)
  set(STENCIL_XOCC_FLAGS ${STENCIL_XOCC_FLAGS} -s)
endif()
if (STENCIL_DIMMS_INTERNAL EQUAL 2)
  set(STENCIL_XOCC_FLAGS ${STENCIL_XOCC_FLAGS}
    --xp misc:map_connect=add.kernel.${STENCIL_ENTRY_FUNCTION}_1.M_AXI_GMEM0.core.OCL_REGION_0.M00_AXI
    --xp misc:map_connect=add.kernel.${STENCIL_ENTRY_FUNCTION}_1.M_AXI_GMEM1.core.OCL_REGION_0.M01_AXI
    --max_memory_ports all)
endif()
if(STENCIL_SINGLE_SLR)
  set(STENCIL_XOCC_FLAGS ${STENCIL_XOCC_FLAGS} -O4)
else()
  set(STENCIL_XOCC_FLAGS ${STENCIL_XOCC_FLAGS} -O3)
endif()

# Kernel build
add_custom_target(build_kernel
  COMMAND XILINX_PATH=${CMAKE_BINARY_DIR} ${SDAccel_XOCC} ${STENCIL_XOCC_FLAGS} -o ${STENCIL_KERNEL_STRING} 
  DEPENDS ${STENCIL_HLS_DEPENDS})

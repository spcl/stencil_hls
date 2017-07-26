# Author:  Johannes de Fine Licht (johannes.definelicht@inf.ethz.ch)
# Created: March 2017
cmake_minimum_required(VERSION 2.8.12)
project(Stencil C CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

# Target platform
set(STENCIL_PART_NAME "xcku115-flvb2104-2-e") 
set(STENCIL_DSA_STRING "xilinx:xil-accel-rd-ku115:4ddr-xpr:4.0")
set(STENCIL_DIMMS 2 CACHE STRING "Number of DDR DIMMs to target")
# set(STENCIL_TARGET "TUL-KU115" CACHE STRING "Target board.")
# if (STENCIL_TARGET STREQUAL "TUL-KU115")
#   set(STENCIL_PART_NAME "xcku115-flvb2104-2-e") 
#   set(STENCIL_DSA_STRING "xilinx:tul-pcie3-ku115:2ddr:3.1")
#   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTENCIL_TUL_KU115")
#   set(STENCIL_DIMMS_DEFAULT 2)
#   set(STENCIL_DIMMS_MAX 2)
# elseif (STENCIL_TARGET STREQUAL "TUL-KU115-XPR")
#   set(STENCIL_PART_NAME "xcku115-flvb2104-2-e") 
#   set(STENCIL_DSA_STRING "xilinx:xil-accel-rd-ku115:4ddr-xpr:3.2")
#   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTENCIL_TUL_KU115_XPR")
#   set(STENCIL_DIMMS_DEFAULT 2)
#   set(STENCIL_DIMMS_MAX 2) # Can potentially be 4
# elseif (STENCIL_TARGET STREQUAL "ADM-KU3")
#   set(STENCIL_PART_NAME "xcku060-ffva1156-2-e") 
#   set(STENCIL_DSA_STRING "xilinx:adm-pcie-ku3:2ddr:3.2")
#   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTENCIL_ADM_KU3")
#   set(STENCIL_DIMMS_DEFAULT 2)
#   set(STENCIL_DIMMS_MAX 2)
# elseif (STENCIL_TARGET STREQUAL "ADM-KU3-XPR")
#   set(STENCIL_PART_NAME "xcku060-ffva1156-2-e") 
#   set(STENCIL_DSA_STRING "xilinx:adm-pcie-ku3:2ddr-xpr:3.2")
#   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTENCIL_ADM_KU3_XPR")
#   set(STENCIL_DIMMS_DEFAULT 2)
#   set(STENCIL_DIMMS_MAX 2)
# elseif (STENCIL_TARGET STREQUAL "ADM-7V3")
#   set(STENCIL_PART_NAME "xc7vx690tffg1157-2") 
#   set(STENCIL_DSA_STRING "xilinx:adm-pcie-7v3:1ddr:3.0")
#   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DSTENCIL_ADM_7V3")
#   set(STENCIL_DIMMS_DEFAULT 1)
#   set(STENCIL_DIMMS_MAX 1)
# else()
#   message(FATAL_ERROR "Unknown target \"${STENCIL_TARGET}\".")
# endif()
# mark_as_advanced(STENCIL_PART_NAME)
# mark_as_advanced(STENCIL_DSA_STRING)
# mark_as_advanced(STENCIL_DIMMS_DEFAULT)
# mark_as_advanced(STENCIL_DIMMS_MAX)

# User configuration
set(STENCIL_DATA_TYPE "float" CACHE STRING "Data type.")
set(STENCIL_TIME 32 CACHE STRING "Number of timesteps.")
set(STENCIL_MEMORY_WIDTH 16 CACHE STRING "Width of memory port.")
set(STENCIL_KERNEL_WIDTH 4 CACHE STRING "Width of kernel data path.")
set(STENCIL_DEPTH 8 CACHE STRING "Depth of pipeline (determines halo size.)")
set(STENCIL_BLOCKS 4 CACHE STRING "Number of blocks.")
set(STENCIL_ROWS 8192 CACHE STRING "Number of rows.")
set(STENCIL_COLS 8192 CACHE STRING "Number of columns.")
set(STENCIL_TARGET_CLOCK 200 CACHE STRING "Target clock speed.")
set(STENCIL_TARGET_TIMING 5 CACHE STRING "Target timing of HLS.")
set(STENCIL_KEEP_INTERMEDIATE OFF CACHE STRING "Keep intermediate SDAccel files")

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
include_directories(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/include ${SDAccel_INCLUDE_DIRS})
set(STENCIL_LIBS ${SDAccel_LIBRARIES})

# Compilation flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

# Source
set(STENCIL_SRC
    src/Stencil.cpp
    src/Reference.cpp
    src/Memory.cpp)

# Dependencies
set(STENCIL_HLS_DEPENDS
    ${STENCIL_SRC}
    include/Stencil.h.in
    include/Reference.h
    include/Memory.h
    include/Compute.h
    include/hlslib/SDAccel.h
    include/hlslib/DataPack.h
    include/hlslib/Utility.h
    include/hlslib/Stream.h)

# Configure files 
set(STENCIL_KERNEL_STRING
  "jacobi_${STENCIL_TARGET}_${STENCIL_DATA_TYPE}_c${STENCIL_TARGET_CLOCK}_w${STENCIL_KERNEL_WIDTH}_d${STENCIL_DEPTH}_${STENCIL_ROWS}x${STENCIL_COLS}_b${STENCIL_BLOCKS}_t${STENCIL_TIME}.xclbin")
configure_file(include/Stencil.h.in Stencil.h)
configure_file(scripts/Synthesis.tcl.in Synthesis.tcl)

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
  -O3
  -s
  # Includes
  -I${CMAKE_BINARY_DIR}
  -I${CMAKE_SOURCE_DIR}/include
  # Source
  ${CMAKE_SOURCE_DIR}/src/Stencil.cpp
  ${CMAKE_SOURCE_DIR}/src/Memory.cpp
  # Flags
  --kernel ${STENCIL_ENTRY_FUNCTION} 
  --xdevice ${STENCIL_DSA_STRING}
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

# Kernel build
add_custom_target(kernel
  COMMAND ${SDAccel_XOCC} ${STENCIL_XOCC_FLAGS} -o ${STENCIL_KERNEL_STRING} 
  DEPENDS ${STENCIL_HLS_DEPENDS})

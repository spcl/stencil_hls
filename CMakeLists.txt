# Author:  Johannes de Fine Licht (definelicht@inf.ethz.ch)
cmake_minimum_required(VERSION 2.8.12)
project(Stencil C CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/hlslib/cmake)

# Target platform
set(STENCIL_PART_NAME "xcvu9p-flgb2104-2-i" CACHE STRING "HLS part name") 
set(STENCIL_DSA_STRING "xilinx_vcu1525_dynamic_5_1" CACHE STRING "SDx DSA/platform name")
set(STENCIL_DIMMS 2 CACHE STRING "Number of DDR DIMMs to target")

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
set(STENCIL_KEEP_INTERMEDIATE ON CACHE STRING "Keep intermediate Vitis files")
set(STENCIL_ADD_CORE OFF CACHE STRING "")                                
set(STENCIL_MULT_CORE OFF CACHE STRING "")  
set(STENCIL_ENABLE_PROFILING OFF CACHE STRING "Enable SDx profiling")

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
find_package(Vitis REQUIRED)
find_package(Threads)
include_directories(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/hlslib/include ${Vitis_INCLUDE_DIRS})
set(STENCIL_LIBS ${Vitis_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

# Compilation flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
set(STENCIL_SYNTHESIS_FLAGS "${STENCIL_SYNTHESIS_FLAGS} -std=c++11 -DHLSLIB_SYNTHESIS -DSTENCIL_SYNTHESIS")
if(STENCIL_ADD_CORE)
  set(STENCIL_SYNTHESIS_FLAGS "${STENCIL_SYNTHESIS_FLAGS} -DSTENCIL_ADD_CORE=${STENCIL_ADD_CORE}") 
endif() 
if(STENCIL_MULT_CORE) 
  set(STENCIL_SYNTHESIS_FLAGS "${STENCIL_SYNTHESIS_FLAGS} -DSTENCIL_MULT_CORE=${STENCIL_MULT_CORE}") 
endif() 
if(((${Vitis_MAJOR_VERSION} LESS 2018) AND (${Vitis_MINOR_VERSION} LESS 3)) OR ${Vitis_MAJOR_VERSION} LESS 2017)
  add_definitions(-DHLSLIB_LEGACY_SDX=1)
else()
  add_definitions(-DHLSLIB_LEGACY_SDX=0)
endif()

# Source
set(STENCIL_KERNEL_SRC
    ${CMAKE_SOURCE_DIR}/src/Stencil.cpp
    ${CMAKE_SOURCE_DIR}/src/Memory.cpp)
set(STENCIL_SRC
    ${STENCIL_KERNEL_SRC}
    ${CMAKE_SOURCE_DIR}/src/Reference.cpp)

# Configure files 
set(STENCIL_KERNEL_STRING
  "jacobi_${STENCIL_DATA_TYPE}_c${STENCIL_TARGET_CLOCK}_w${STENCIL_KERNEL_WIDTH}_d${STENCIL_DEPTH}_${STENCIL_ROWS}x${STENCIL_COLS}_b${STENCIL_BLOCKS}_t${STENCIL_TIME}")
configure_file(include/Stencil.h.in Stencil.h)
configure_file(scripts/Synthesis.tcl.in Synthesis.tcl)

# Synthesis
add_custom_target(synthesis
  COMMAND ${Vitis_HLS} -f Synthesis.tcl
  DEPENDS ${STENCIL_HLS_DEPENDS})

# Library files
add_library(stencil ${STENCIL_SRC})
target_link_libraries(stencil ${STENCIL_LIBS})
set(STENCIL_LIBS ${STENCIL_LIBS} stencil)

# Testing
enable_testing()
if (Threads_FOUND)
  add_executable(Testbench src/Testbench.cpp)
  target_link_libraries(Testbench ${STENCIL_LIBS})
  add_test(Testbench Testbench)
else()
  message(WARNING "Threads not found. Testbench will be unavailable.")
endif()

# Stats
add_executable(Stats src/Stats.cpp)
target_link_libraries(Stats ${STENCIL_LIBS})

# Vitis
add_executable(ExecuteKernel.exe src/ExecuteKernel.cpp)
target_link_libraries(ExecuteKernel.exe ${STENCIL_LIBS})
string(REPLACE " " ";" STENCIL_SYNTHESIS_FLAGS ${STENCIL_SYNTHESIS_FLAGS})
set(STENCIL_VPP_FLAGS ${STENCIL_VPP_FLAGS} 
  # Includes
  -I${CMAKE_BINARY_DIR}
  -I${CMAKE_SOURCE_DIR}/include
  -I${CMAKE_SOURCE_DIR}/hlslib/include
  # Flags
  --kernel ${STENCIL_ENTRY_FUNCTION} 
  --platform ${STENCIL_DSA_STRING}
  --xp prop:kernel.${STENCIL_ENTRY_FUNCTION}.kernel_flags="${STENCIL_SYNTHESIS_FLAGS}"
  --kernel_frequency ${STENCIL_TARGET_CLOCK})
if(STENCIL_KEEP_INTERMEDIATE)
  set(STENCIL_VPP_FLAGS ${STENCIL_VPP_FLAGS} -s)
endif()
if (STENCIL_DIMMS_INTERNAL EQUAL 2)
  if(${Vitis_MAJOR_VERSION} LESS 2019)
    set(STENCIL_VPP_FLAGS ${STENCIL_VPP_FLAGS}
      --xp misc:map_connect=add.kernel.${STENCIL_ENTRY_FUNCTION}_1.M_AXI_GMEM0.core.OCL_REGION_0.M00_AXI
      --xp misc:map_connect=add.kernel.${STENCIL_ENTRY_FUNCTION}_1.M_AXI_GMEM1.core.OCL_REGION_0.M01_AXI
      --max_memory_ports all)
  else()
    set(STENCIL_VPP_FLAGS ${STENCIL_VPP_FLAGS}
      --sp ${STENCIL_ENTRY_FUNCTION}_1.m_axi_gmem0:bank0
      --sp ${STENCIL_ENTRY_FUNCTION}_1.m_axi_gmem1:bank1)
  endif()
endif()
if(STENCIL_ENABLE_PROFILING)
  set(STENCIL_VPP_COMMAND ${STENCIL_VPP_COMMAND}
    --profile_kernel "data:all:all:all"
    --profile_kernel "stall:all:all"
    --profile_kernel "exec:all:all")
endif()

# Kernel build
if(((${Vitis_MAJOR_VERSION} LESS 2018) AND (${Vitis_MINOR_VERSION} LESS 3)) OR ${Vitis_MAJOR_VERSION} LESS 2017)
  add_custom_target(build_kernel
    COMMAND XILINX_PATH=${CMAKE_BINARY_DIR} ${Vitis_COMPILER} -t hw
    ${STENCIL_VPP_FLAGS} ${STENCIL_VPP_COMPILE_FLAGS} ${STENCIL_VPP_LINK_FLAGS}
    ${STENCIL_KERNEL_SRC} -o ${STENCIL_KERNEL_STRING}.xclbin) 
else()
  add_custom_target(compile_hardware
    COMMAND XILINX_PATH=${CMAKE_BINARY_DIR} ${Vitis_COMPILER} -c -t hw
    ${STENCIL_VPP_FLAGS} ${STENCIL_VPP_LINK_FLAGS}
    ${STENCIL_KERNEL_SRC} -o ${STENCIL_KERNEL_STRING}.xo) 
  add_custom_target(link_hardware
    COMMAND XILINX_PATH=${CMAKE_BINARY_DIR} ${Vitis_COMPILER} -l -t hw
    ${STENCIL_VPP_FLAGS} ${STENCIL_VPP_LINK_FLAGS}
    ${STENCIL_KERNEL_STRING}.xo -o ${STENCIL_KERNEL_STRING}.xclbin) 
endif()

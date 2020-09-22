# Author:  Johannes de Fine Licht (definelicht@inf.ethz.ch)
cmake_minimum_required(VERSION 2.8.12)
project(Stencil C CXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/hlslib/cmake)

# Target platform
set(STENCIL_PART_NAME "xcu250-figd2104-2L-e" CACHE STRING "HLS part name") 
set(STENCIL_DSA "xilinx_u250_xdma_201830_2" CACHE STRING "SDx DSA/platform name")

# User configuration
set(STENCIL_DATA_TYPE "float" CACHE STRING "Data type.")
set(STENCIL_MEMORY_WIDTH 16 CACHE STRING "Width of memory port.")
set(STENCIL_KERNEL_WIDTH 4 CACHE STRING "Width of kernel data path.")
set(STENCIL_DEPTH 8 CACHE STRING "Depth of pipeline (determines halo size.)")
set(STENCIL_BLOCKS 4 CACHE STRING "Number of blocks.")
set(STENCIL_ROWS 8192 CACHE STRING "Number of rows.")
set(STENCIL_COLS 8192 CACHE STRING "Number of columns.")
set(STENCIL_TARGET_CLOCK 300 CACHE STRING "Target clock speed.")
set(STENCIL_TIMING_UNCERTAINTY 1.08 CACHE STRING "Uncertainty on the timing allowed in HLS.")
set(STENCIL_KEEP_INTERMEDIATE ON CACHE BOOL "Keep intermediate Vitis files")
set(STENCIL_ADD_CORE "FAddSub_fulldsp" CACHE STRING "")                                
set(STENCIL_MULT_CORE "FMul_fulldsp" CACHE STRING "")  
set(STENCIL_ENABLE_PROFILING OFF CACHE BOOL "Enable SDx profiling")
set(STENCIL_ENABLE_DEBUGGING OFF CACHE BOOL "Enable debugging of kernel")

# Internal
set(STENCIL_KERNEL_NAME "Stencil")
set(STENCIL_ENTRY_FUNCTION "StencilKernel")
mark_as_advanced(STENCIL_ENTRY_FUNCTION)

# Dependencies
find_package(Vitis REQUIRED)
find_package(Threads)
include_directories(${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/hlslib/include ${Vitis_INCLUDE_DIRS})
set(STENCIL_LIBS ${Vitis_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

# Compilation flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
set(STENCIL_SYNTHESIS_FLAGS "${STENCIL_SYNTHESIS_FLAGS} -std=c++11 -DHLSLIB_SYNTHESIS -DSTENCIL_SYNTHESIS")
if(Vitis_USE_VITIS_HLS)
  set(STENCIL_SYNTHESIS_FLAGS "${STENCIL_SYNTHESIS_FLAGS} -D__VITIS_HLS__")
else()
  set(STENCIL_SYNTHESIS_FLAGS "${STENCIL_SYNTHESIS_FLAGS} -D__VIVADO_HLS__")
endif()
if(STENCIL_ADD_CORE)
  set(STENCIL_SYNTHESIS_FLAGS "${STENCIL_SYNTHESIS_FLAGS} -DSTENCIL_ADD_CORE=${STENCIL_ADD_CORE}") 
endif() 
if(STENCIL_MULT_CORE) 
  set(STENCIL_SYNTHESIS_FLAGS "${STENCIL_SYNTHESIS_FLAGS} -DSTENCIL_MULT_CORE=${STENCIL_MULT_CORE}") 
endif() 
add_definitions(-DHLSLIB_LEGACY_SDX=0)
if(STENCIL_ENABLE_DEBUGGING)
  add_definitions(-DSTENCIL_KERNEL_DEBUG)
endif()

# Source
set(STENCIL_KERNEL_SRC
    ${CMAKE_SOURCE_DIR}/kernel/Stencil.cpp
    ${CMAKE_SOURCE_DIR}/kernel/Memory.cpp)
set(STENCIL_SRC
    ${STENCIL_KERNEL_SRC}
    ${CMAKE_SOURCE_DIR}/src/Reference.cpp)

# Configure files 
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
add_executable(ExecuteKernel.exe host/ExecuteKernel.cpp)
target_link_libraries(ExecuteKernel.exe ${STENCIL_LIBS})
string(REPLACE " " ";" STENCIL_SYNTHESIS_FLAGS ${STENCIL_SYNTHESIS_FLAGS})
set(STENCIL_VITIS_FLAGS ${STENCIL_VITIS_FLAGS} 
  # Includes
  -I${CMAKE_BINARY_DIR}
  -I${CMAKE_SOURCE_DIR}/include
  -I${CMAKE_SOURCE_DIR}/hlslib/include
  # Flags
  --kernel ${STENCIL_ENTRY_FUNCTION} 
  --platform ${STENCIL_DSA}
  --advanced.prop kernel.${STENCIL_ENTRY_FUNCTION}.kernel_flags="${STENCIL_SYNTHESIS_FLAGS}"
  --kernel_frequency ${STENCIL_TARGET_CLOCK})
if(STENCIL_KEEP_INTERMEDIATE)
  set(STENCIL_VITIS_FLAGS ${STENCIL_VITIS_FLAGS} -s)
endif()
set(STENCIL_VITIS_FLAGS ${STENCIL_VITIS_FLAGS}
  --sp ${STENCIL_ENTRY_FUNCTION}_1.m_axi_gmem0:bank0
  --sp ${STENCIL_ENTRY_FUNCTION}_1.m_axi_gmem1:bank1)

# Kernel build
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/${STENCIL_KERNEL_NAME}_hw_emu.xo
  COMMENT "Compiling for hardware emulation."
  COMMAND ${CMAKE_COMMAND} -E env
          XILINX_PATH=${CMAKE_BINARY_DIR}
          ${Vitis_COMPILER} -c -t hw_emu
          ${STENCIL_VITIS_FLAGS}
          ${STENCIL_KERNEL_SRC} -o ${STENCIL_KERNEL_NAME}_hw_emu.xo
  DEPENDS ${STENCIL_KERNEL_SRC})
add_custom_target(compile_hardware_emulation DEPENDS
                  ${CMAKE_BINARY_DIR}/${STENCIL_KERNEL_NAME}_hw_emu.xo)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/${STENCIL_KERNEL_NAME}_hw_emu.xclbin
  COMMENT "Linking for hardware emulation."
  COMMAND ${CMAKE_COMMAND} -E env
          XILINX_PATH=${CMAKE_BINARY_DIR}
          ${Vitis_COMPILER} -l -t hw_emu
          ${STENCIL_VITIS_COMMAND} ${STENCIL_VITIS_FLAGS}
          ${STENCIL_KERNEL_NAME}_hw_emu.xo
          -o ${STENCIL_KERNEL_NAME}_hw_emu.xclbin
  DEPENDS ${CMAKE_BINARY_DIR}/${STENCIL_KERNEL_NAME}_hw_emu.xo)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/emconfig.json
  COMMENT "Generating emconfig.json file for hardware emulation."
  COMMAND ${VITIS_ROOT_DIR}/bin/emconfigutil --platform
          ${STENCIL_DSA_NAME})
add_custom_target(link_hardware_emulation DEPENDS
                  ${CMAKE_BINARY_DIR}/${STENCIL_KERNEL_NAME}_hw_emu.xclbin
                  ${CMAKE_BINARY_DIR}/emconfig.json)
# Hardware
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/${STENCIL_KERNEL_NAME}_hw.xo
  COMMENT "Compiling for hardware."
  COMMAND ${CMAKE_COMMAND} -E env
          XILINX_PATH=${CMAKE_BINARY_DIR}
          ${Vitis_COMPILER} -c -t hw
          ${STENCIL_VITIS_COMMAND} ${STENCIL_VITIS_FLAGS}
          ${STENCIL_KERNEL_SRC} -o ${STENCIL_KERNEL_NAME}_hw.xo
  DEPENDS ${STENCIL_KERNEL_SRC})
add_custom_target(compile_hardware DEPENDS
                  ${CMAKE_BINARY_DIR}/${STENCIL_KERNEL_NAME}_hw.xo)
add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/${STENCIL_KERNEL_NAME}_hw.xclbin
  COMMENT "Linking for hardware (this will run the full synthesis, placement and routing flow)."
  COMMAND ${CMAKE_COMMAND} -E env
          XILINX_PATH=${CMAKE_BINARY_DIR}
          ${Vitis_COMPILER} -l -t hw
          ${STENCIL_VITIS_COMMAND} ${STENCIL_VITIS_FLAGS}
          ${STENCIL_KERNEL_NAME}_hw.xo
          -o ${STENCIL_KERNEL_NAME}_hw.xclbin
  DEPENDS ${CMAKE_BINARY_DIR}/${STENCIL_KERNEL_NAME}_hw.xo)
add_custom_target(link_hardware DEPENDS
                  ${CMAKE_BINARY_DIR}/${STENCIL_KERNEL_NAME}_hw.xclbin)

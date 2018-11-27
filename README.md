Building the project
--------------------

This project is configured using CMake, which will generate all the build targets necessary to both simulate and run in hardware. The code depends on [hlslib](https://github.com/definelicht/hlslib), which must be cloned as a submodule with `git submodule update --init`.  The project should be configured and built out of directory, e.g.: 

```sh
mkdir build 
cd build 
cmake ../ -DSTENCIL_DSA_STRING=xilinx_vcu1525_dynamic_5_1 -DSTENCIL_DIMMS=2
```

Apart from the target DSA (platform), and number of DIMMs shown above, important configuration variables that affect the final circuit are:

- `STENCIL_DATA_TYPE`
- `STENCIL_TIME`
- `STENCIL_MEMORY_WIDTH`
- `STENCIL_KERNEL_WIDTH`
- `STENCIL_DEPTH`
- `STENCIL_BLOCKS`
- `STENCIL_ROWS`
- `STENCIL_COLS`
- `STENCIL_TARGET_CLOCK`
- `STENCIL_TARGET_TIMING`

To build the host-side code, run `make all` (or just `make`). To build the hardware kernel, use `make compile_kernel` and `make link_kernel`. To see the expected performance numbers for the current configuration, run the executable `Stats`, which is also built my `make all`.

Running the kernel
------------------

Once the kernel has been built, it is run in hardware by executing `ExecuteKernel.exe`, which is built by `make all`. This requires an appropriate SDAccel environment to be set up, which can be done with the following shell commands:

```sh
export XILINX_OPENCL=<DSA folder>/xbinst
export PATH=$XILINX_OPENCL/runtime/bin:$PATH
unset XILINX_SDX
unset XILINX_SDACCEL
unset XCL_EMULATION_MODE
```

Running the kernel will print the resulting compute and memory performance.

Source code
-----------

The main kernel is located in `src/Stencil.cpp`, with the majority of the functionality contained in `include/Compute.h` and `src/Memory.cpp`.

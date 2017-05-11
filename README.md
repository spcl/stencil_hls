Building the project
--------------------

This project is configured using CMake, which will generate all the build targets necessary to both simulate and run in hardware. The project should be configured out of directory, for example, assuming the top level directory of this project as the working directory: 

```sh
mkdir build 
cd build 
cmake ../ -DSTENCIL_PART_NAME=xcku115-flvb2104-2-e -DSTENCIL_DSA_STRING=xilinx:tul-pcie3-ku115:2ddr:3.1 -DSTENCIL_DIMMS=2
```

Apart from the target device, DSA, and number of DIMMs shown above, important configuration variables that affect the final circuit are:

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

To build the host-side code, simply `make all` (or just `make`). To build the hardware kernel, use `make kernel`. To see the expected performance numbers for the current configuration, run the executable `Stats`, which is also built my `make all`.

Running the kernel
------------------

Once the kernel has been built, it is run in hardware by executing `ExecuteKernel.exe`, which is built by `make all`. This requires an appropriate SDAccel environment to be set up, which can be done with the following shell commands:

```sh
export XILINX_OPENCL=<DSA folder>/xbinst/pkg/pcie
export XCL_PLATFORM=<DSA string>
unset XILINX_SDACCEL
unset XCL_EMULATION_MODE
```

Running the kernel will print the resulting compute and memory performance.

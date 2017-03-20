/// \author Johannes de Fine Licht (johannes.definelicht@inf.ethz.ch)
/// \date April 2016

#pragma once

#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <vector>
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/opencl.h>
#endif

namespace hlslib {

namespace ocl {

namespace {

constexpr bool verbose = true;

constexpr size_t kMaxCount = 16;
constexpr size_t kMaxString = 128;

constexpr cl_mem_flags kXilinxMemPointer = 1<<31;
constexpr unsigned kXilinxBuffer = 1<<0; 
constexpr unsigned kXilinxPipe   = 1<<1; 
constexpr unsigned kXilinxBank0  = 1<<8; 
constexpr unsigned kXilinxBank1  = 1<<9; 
constexpr unsigned kXilinxBank2  = 1<<10; 
constexpr unsigned kXilinxBank3  = 1<<11; 

struct ExtendedMemoryPointer {
  unsigned flags;
  void *obj;
  void *param;
};

template <typename IteratorType>
constexpr bool IsRandomAccess() {
  return std::is_base_of<
      std::random_access_iterator_tag,
      typename std::iterator_traits<IteratorType>::iterator_category>::value;
}

template <typename IteratorType, typename T>
constexpr bool IsIteratorOfType() {
  return std::is_same<typename std::iterator_traits<IteratorType>::value_type,
                      T>::value;
}

void Error(std::string const &message) {
  throw std::runtime_error(message);
}

/// Finds and returns all available OpenCL platforms.
std::vector<cl_platform_id> GetAvailablePlatforms() {

  std::vector<cl_platform_id> platforms(kMaxCount);
  cl_uint platformCount;
  cl_int errorCode =
      clGetPlatformIDs(kMaxCount, platforms.data(), &platformCount);
  if (errorCode != CL_SUCCESS) {
    Error("Failed to retrieve OpenCL platforms.");
    return {};
  }

  platforms.resize(platformCount);
  return platforms;
}

std::string GetPlatformVendor(cl_platform_id platformId) {
  std::array<char, kMaxString> vendorName;
  auto errorCode = clGetPlatformInfo(platformId, CL_PLATFORM_VENDOR, kMaxString,
                                     vendorName.data(), nullptr);
  if (errorCode != CL_SUCCESS) {
    Error("Failed to retrieve platform vendor name.");
    return {};
  }
  return std::string(vendorName.data());
}

cl_platform_id FindPlatformByVendor(std::string const &desiredVendor) {

  cl_platform_id platformId{};

  auto available = GetAvailablePlatforms();
  if (available.size() == 0) {
    Error("No available OpenCL platforms.");
    return platformId;
  }

  bool foundVendor = false;
  for (auto &p : available) {
    auto vendorName = GetPlatformVendor(p);
    if (std::string(vendorName.data()).find(desiredVendor) !=
        std::string::npos) {
      foundVendor = true;
      platformId = p;
      break;
    }
  }
  if (!foundVendor) {
    std::stringstream ss;
    ss << "Platform \"" << desiredVendor << "\" not found.";
    Error(ss.str());
    return platformId;
  }

  return platformId;
}

/// Finds and returns all available devices for the specified platform.
std::vector<cl_device_id>
GetAvailableDevices(cl_platform_id const &platformId) {

  std::vector<cl_device_id> devices(kMaxCount);
  cl_uint deviceCount;
  auto errorCode = clGetDeviceIDs(platformId, CL_DEVICE_TYPE_ALL,
                                  kMaxCount, devices.data(), &deviceCount);

  if (errorCode != CL_SUCCESS) {
    Error("Failed to retrieve device IDs.");
    return {};
  }

  devices.resize(deviceCount); 
  return devices;
}

std::string GetDeviceName(cl_device_id const &deviceId) {
  std::array<char, kMaxString> deviceName;
  auto errorCode = clGetDeviceInfo(deviceId, CL_DEVICE_NAME, kMaxString,
                                   deviceName.data(), nullptr);
  if (errorCode != CL_SUCCESS) {
    Error("Failed to retrieve device info.");
    return {};
  }
  return std::string(deviceName.data());
}

cl_device_id FindDeviceByName(cl_platform_id const &platformId,
                              std::string const &desiredDevice) {

  cl_device_id device;
  auto available = GetAvailableDevices(platformId);
  if (available.size() == 0) {
    Error("No compute devices found.");
    return {};
  }

  bool deviceFound = false;
  for (auto &d : available) {
    auto deviceName = GetDeviceName(d);
    if (deviceName.find(desiredDevice) != std::string::npos) {
      device = d;
      deviceFound = true;
      break;
    }
  }

  if (!deviceFound) {
    std::stringstream ss;
    ss << "Failed to find device \"" << desiredDevice << "\".";
    Error(ss.str());
    return {};
  }

  return device;
}

cl_context CreateComputeContext(std::vector<cl_device_id> const &deviceIds) {
  // TODO: support properties and callback functions
  cl_int errorCode;
  auto context = clCreateContext(nullptr, deviceIds.size(), deviceIds.data(),
                                 nullptr, nullptr, &errorCode);
  if (errorCode != CL_SUCCESS) {
    Error("Failed to create compute context.");
    return {};
  }
  return context;
}

cl_context CreateComputeContext(cl_device_id const &deviceId) {
  std::vector<cl_device_id> deviceIdVec = {deviceId};
  return CreateComputeContext(deviceIdVec);
}

cl_command_queue CreateCommandQueue(cl_context const &context,
                                    cl_device_id const &deviceId) {
  cl_int errorCode;
  auto commandQueue = clCreateCommandQueue(
      context, deviceId, CL_QUEUE_PROFILING_ENABLE, &errorCode);
  if (errorCode != CL_SUCCESS) {
    Error("Failed to create command queue.");
    return {};
  }
  return commandQueue;
}

} // End anonymous namespace

enum class Access {
  read,
  write,
  readWrite 
};

enum class MemoryBank {
  bank0,
  bank1,
  bank2,
  bank3
};

template <typename, Access>
class Buffer;

class Kernel;

class Context {

public:
  /// Performs initialization of the requested device
  Context(std::string const &vendorName, std::string const &deviceName) {

    // Find requested OpenCL platform
    platformId_ = FindPlatformByVendor(vendorName);

    // Find requested compute device
    deviceId_ = FindDeviceByName(platformId_, deviceName);

    context_ = CreateComputeContext(deviceId_);

    commandQueue_ = CreateCommandQueue(context_, deviceId_);
  }

  /// Performs initialization of first available device of requested vendor 
  Context(std::string const &vendorName) {

    // Find requested OpenCL platform
    platformId_ = FindPlatformByVendor(vendorName);

    auto devices = GetAvailableDevices(platformId_); 
    if (devices.size() == 0) {
      Error("No OpenCL devices found for platform.");
      return;
    }
    deviceId_ = devices[0];
    if (verbose) {
      std::cout << "Using device \"" << GetDeviceName(deviceId_) << "\".\n";
    }

    context_ = CreateComputeContext(deviceId_);

    commandQueue_ = CreateCommandQueue(context_, deviceId_);
  }

  /// Finds first available platform and device
  Context() {

    auto platforms = GetAvailablePlatforms();
    if (platforms.size() == 0) {
      Error("No OpenCL platform found.");
      return;
    }
    platformId_ = platforms[0];
    if (verbose) {
      std::cout << "Using platform \"" << GetPlatformVendor(platformId_)
                << "\".\n";
    }

    auto devices = GetAvailableDevices(platformId_); 
    if (devices.size() == 0) {
      Error("No OpenCL devices found for platform.");
      return;
    }
    deviceId_ = devices[0];
    if (verbose) {
      std::cout << "Using device \"" << GetDeviceName(deviceId_) << "\".\n";
    }

    context_ = CreateComputeContext(deviceId_);

    commandQueue_ = CreateCommandQueue(context_, deviceId_);
  }

  Context(Context const &) = delete;
  Context(Context &&) = default;
  Context& operator=(Context const &) = delete;
  Context& operator=(Context &&) = default;

  ~Context() {
    clReleaseContext(context_);
    clReleaseCommandQueue(commandQueue_);
  }

  cl_device_id const &deviceId() const {
    return deviceId_;
  }

  cl_context const &context() const {
    return context_;
  }

  cl_command_queue const &commandQueue() const {
    return commandQueue_;
  }

  template <typename T, Access access>
  Buffer<T, access> MakeBuffer();

  template <typename T, Access access, typename... Ts>
  Buffer<T, access> MakeBuffer(MemoryBank memoryBank, Ts... args);

  template <typename... Ts>
  Kernel MakeKernelFromBinary(std::string const &path,
                              std::string const &kernelName, Ts &... args);

  template <typename... Ts>
  Kernel MakeKernelFromSource(std::string const &path,
                              std::string const &kernelName, Ts &... args);

private:
  cl_platform_id platformId_{};
  cl_device_id deviceId_{};
  cl_context context_{};
  cl_command_queue commandQueue_{};

}; // End class Context 


template <typename T, Access access>
class Buffer {

public:
  Buffer() : context_(nullptr), nElements_(0) {}

  Buffer(Buffer<T, access> const &other) = delete; 

  Buffer(Buffer<T, access> &&other) : Buffer() {
    swap(*this, other);
  }

  /// Allocate and copy to device.
  template <typename IteratorType, typename = typename std::enable_if<
                                       IsIteratorOfType<IteratorType, T>() &&
                                       IsRandomAccess<IteratorType>()>::type>
  Buffer(Context const &context, MemoryBank memoryBank, IteratorType begin,
         IteratorType end)
      : context_(&context), nElements_(std::distance(begin, end)) {

    auto extendedPointer = CreateExtendedPointer(begin, memoryBank);

    cl_mem_flags flags = CL_MEM_COPY_HOST_PTR | kXilinxMemPointer;
    switch (access) {
    case Access::read:
      flags |= CL_MEM_READ_ONLY;
      break;
    case Access::write:
      flags |= CL_MEM_WRITE_ONLY;
      break;
    case Access::readWrite:
      flags |= CL_MEM_READ_WRITE;
      break;
    }

    cl_int errorCode;
    devicePtr_ =
        clCreateBuffer(context_->context(), flags, sizeof(T) * nElements_,
                       &extendedPointer, &errorCode);

    if (errorCode != CL_SUCCESS) {
      throw std::runtime_error(
          "Failed to initialize and copy to device memory.");
    }

  }

  /// Allocate device memory but don't perform any transfers.
  Buffer(Context const &context, MemoryBank memoryBank, size_t nElements)
      : context_(&context), nElements_(nElements) {

    T *dummy = nullptr;
    auto extendedPointer = CreateExtendedPointer(dummy, memoryBank);

    cl_mem_flags flags = kXilinxMemPointer;
    switch (access) {
    case Access::read:
      flags |= CL_MEM_READ_ONLY;
      break;
    case Access::write:
      flags |= CL_MEM_WRITE_ONLY;
      break;
    case Access::readWrite:
      flags |= CL_MEM_READ_WRITE;
      break;
    }

    cl_int errorCode;
    devicePtr_ =
        clCreateBuffer(context_->context(), flags, sizeof(T) * nElements_,
                       &extendedPointer, &errorCode);

    if (errorCode != CL_SUCCESS) {
      throw std::runtime_error("Failed to initialize device memory.");
    }
      
  }

  friend void swap(Buffer<T, access> &first, Buffer<T, access> &second) {
    std::swap(first.context_, second.context_);
    std::swap(first.devicePtr_, second.devicePtr_);
    std::swap(first.nElements_, second.nElements_);
  }

  Buffer<T, access> &operator=(Buffer<T, access> const &other) = delete;

  Buffer<T, access> &operator=(Buffer<T, access> &&other) {
    swap(*this, other);
    return *this;
  }

  ~Buffer() {
    if (nElements_ > 0) {
      clReleaseMemObject(devicePtr_);
    }
  }

  template <typename IteratorType,
            typename = typename std::enable_if<
                (access == Access::read || access == Access::readWrite) &&
                IsIteratorOfType<IteratorType, T>() &&
                IsRandomAccess<IteratorType>()>::type>
  void CopyToDevice(IteratorType source) {

    // We cannot pass a const pointer to clCreateBuffer even though the function
    // should be read only, so we allow a const_cast
    auto errorCode =
        clEnqueueWriteBuffer(context_->commandQueue(), devicePtr_, CL_TRUE, 0,
                             sizeof(T) * nElements_,
                             const_cast<T *>(&(*source)), 0, nullptr, nullptr);

    if (errorCode != CL_SUCCESS) {
      throw std::runtime_error("Failed to copy data to device.");
    }
  }

  template <typename IteratorType,
            typename = typename std::enable_if<
                (access == Access::write || access == Access::readWrite) &&
                IsIteratorOfType<IteratorType, T>() &&
                IsRandomAccess<IteratorType>()>::type>
  void CopyToHost(IteratorType target) {
    auto errorCode = clEnqueueReadBuffer(context_->commandQueue(), devicePtr_,
                                         CL_TRUE, 0, sizeof(T) * nElements_,
                                         &(*target), 0, nullptr, nullptr);
    if (errorCode != CL_SUCCESS) {
      Error("Failed to copy back memory from device.");
      return;
    }
  }

  cl_mem const &devicePtr() const { return devicePtr_; }

  cl_mem &devicePtr() { return devicePtr_; }

  size_t nElements() const { return nElements_; }

private:

  template <typename IteratorType>
  ExtendedMemoryPointer CreateExtendedPointer(IteratorType begin,
                                              MemoryBank memoryBank) {
    ExtendedMemoryPointer extendedPointer;
    switch (memoryBank) {
    case MemoryBank::bank0:
      extendedPointer.flags = kXilinxBank0;
      break;
    case MemoryBank::bank1:
      extendedPointer.flags = kXilinxBank1;
      break;
    case MemoryBank::bank2:
      extendedPointer.flags = kXilinxBank2;
      break;
    case MemoryBank::bank3:
      extendedPointer.flags = kXilinxBank3;
      break;
    }
    // The target address will not be changed, but OpenCL only accepts
    // non-const, so do a const_cast here
    extendedPointer.obj =
        const_cast<typename std::iterator_traits<IteratorType>::value_type *>(
            &(*begin));
    extendedPointer.param = nullptr;
    return extendedPointer;
  }

  Context const *context_;
  cl_mem devicePtr_{};
  size_t nElements_;

}; // End class Buffer 

class Kernel {

private:

  template <typename T, Access access>
  void SetKernelArguments(size_t index, Buffer<T, access> &arg) {
    auto errorCode =
        clSetKernelArg(kernel_, index, sizeof(cl_mem), &arg.devicePtr());
    if (errorCode != CL_SUCCESS) {
      std::stringstream ss;
      ss << "Failed to set kernel argument " << index << ".";
      Error(ss.str());
      return;
    }
  }

  template <typename T>
  void SetKernelArguments(size_t index, T arg) {
    auto errorCode = clSetKernelArg(kernel_, index, sizeof(T), &arg);
    if (errorCode != CL_SUCCESS) {
      std::stringstream ss;
      ss << "Failed to set kernel argument " << index << ".";
      Error(ss.str());
      return;
    }
  }

  template <typename T, Access access, typename... Ts>
  void SetKernelArguments(size_t index, Buffer<T, access> &arg, Ts &... args) {
    SetKernelArguments(index, arg);
    SetKernelArguments(index + 1, args...);
  }

public:

  /// Load kernel from binary file
  template <typename... Ts>
  Kernel(Context const &context, std::string const &path,
         std::string const &kernelName, bool isBinary, Ts &... kernelArgs)
      : context_(context) {

    std::ifstream input(path,
                        std::ios::in | std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
      std::stringstream ss;
      ss << "Failed to open kernel file \"" << path << "\".";
      Error(ss.str());
      return;
    }
    // Determine size of file in bytes
    size_t fileSize = input.tellg();
    input.seekg(0);

    // Load content of binary file to memory
    std::string fileContent;
    {
      std::ostringstream fileStream;
      fileStream << input.rdbuf();
      fileContent = fileStream.str();
    }

    cl_int errorCode;

    // Create OpenCL program
    if (isBinary) {
      cl_int binaryStatus;
      // Since this is just binary data the reinterpret_cast should be safe
      const unsigned char *binaryData =
          reinterpret_cast<const unsigned char *>(fileContent.data());
      program_ = clCreateProgramWithBinary(
          context.context(), 1, &context.deviceId(), &fileSize, &binaryData,
          &binaryStatus, &errorCode);
      if (binaryStatus != CL_SUCCESS || errorCode != CL_SUCCESS) {
        std::stringstream ss;
        ss << "Failed to create OpenCL program from binary file \"" << path
           << "\":";
        if (binaryStatus != CL_SUCCESS) {
          ss << " binary status: " << binaryStatus << ".";
        }
        if (errorCode != CL_SUCCESS) {
          ss << " error code: " << errorCode << ".";
        }
        Error(ss.str());
        return;
      }
    } else {
      char const *dataPtr = fileContent.data();
      program_ = clCreateProgramWithSource(context.context(), 1, &dataPtr,
                                           &fileSize, &errorCode);
      if (errorCode != CL_SUCCESS) {
        std::stringstream ss;
        ss << "Failed to create OpenCL program from source file \"" << path
           << "\".";
        Error(ss.str());
        return;
      }
    }

    // Build OpenCL program
    errorCode = clBuildProgram(program_, 1, &context.deviceId(), nullptr,
                               nullptr, nullptr);
    if (errorCode != CL_SUCCESS) {
      std::stringstream ss;
      ss << "Failed to build OpenCL program from binary file \"" << path
         << "\".";
      Error(ss.str());
      return;
    }

    // Create executable compute kernel
    kernel_ = clCreateKernel(program_, kernelName.data(), &errorCode);
    if (errorCode != CL_SUCCESS) {
      std::stringstream ss;
      ss << "Failed to create kernel with name \"" << kernelName
         << "\" from binary file \"" << path << "\".";
      Error(ss.str());
      return;
    }

    // Pass kernel arguments 
    SetKernelArguments(0, kernelArgs...);

  }

  /// Execute the kernel as an OpenCL task and returns the time elapsed.
  double ExecuteTask() {
    return ExecuteRange<1>({{1}}, {{1}});
  }

  template <unsigned dims>
  double ExecuteRange(std::array<size_t, dims> const &globalSize,
                      std::array<size_t, dims> const &localSize) {
    cl_event event;
    static std::array<size_t, dims> offsets = {};
    auto errorCode = clEnqueueNDRangeKernel(context_.commandQueue(), kernel_,
                                            dims, &offsets[0], &globalSize[0],
                                            &localSize[0], 0, nullptr, &event);
    if (errorCode != CL_SUCCESS) {
      Error("Failed to execute kernel.");
      return {};
    }
    clWaitForEvents(1, &event);
    cl_ulong timeStart, timeEnd;
    clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START,
                            sizeof(timeStart), &timeStart, nullptr);
    clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END,
                            sizeof(timeEnd), &timeEnd, nullptr);
    double elapsed = 1e-9 * (timeEnd - timeStart);
    return elapsed;
  }

private:
  Context const &context_;
  cl_program program_{};
  cl_kernel kernel_{};

}; // End class Kernel

template <typename T, Access access>
Buffer<T, access> Context::MakeBuffer() {
  return Buffer<T, access>();
}

template <typename T, Access access, typename... Ts>
Buffer<T, access> Context::MakeBuffer(MemoryBank memoryBank, Ts... args) {
  return Buffer<T, access>(*this, memoryBank, args...);
}

template <typename... Ts>
Kernel Context::MakeKernelFromBinary(std::string const &path,
                                     std::string const &kernelName,
                                     Ts &... args) {
  return Kernel(*this, path, kernelName, true, args...);
}

template <typename... Ts>
Kernel Context::MakeKernelFromSource(std::string const &path,
                                     std::string const &kernelName,
                                     Ts &... args) {
  return Kernel(*this, path, kernelName, false, args...);
}

} // End namespace ocl 

} // End namespace hlslib

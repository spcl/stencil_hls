#pragma once

#ifdef HLSLIB_SYNTHESIS

#include <hls_stream.h>
#include <iostream>

namespace hlslib {

template <typename T>
using Stream = hls::stream<T>; 

template <typename T>
T ReadBlocking(Stream<T> &stream) {
  #pragma HLS INLINE
  return stream.read();
}

template <typename T>
T ReadOptimistic(Stream<T> &stream) {
  #pragma HLS INLINE
  return stream.read();
}

template <typename T>
void WriteBlocking(Stream<T> &stream, T const &val, int) {
  #pragma HLS INLINE
  return stream.write(val);
}

template <typename T>
void WriteOptimistic(Stream<T> &stream, T const &val, int) {
  #pragma HLS INLINE
  return stream.write(val);
}

template <typename T>
bool IsEmpty(Stream<T> &stream) {
  #pragma HLS INLINE
  return stream.empty();
}

template <typename T>
bool IsEmptySimulation(Stream<T> &stream) {
  #pragma HLS INLINE
  return false;
}

template <typename T>
bool IsFull(Stream<T> &stream, int) {
  #pragma HLS INLINE
  return stream.full();
}

template <typename T>
bool IsFullSimulation(Stream<T> &stream, int) {
  #pragma HLS INLINE
  return false;
}

}

#else 

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>

#ifdef HLSLIB_DEBUG_STREAM
constexpr bool kStreamVerbose = true;
#else
constexpr bool kStreamVerbose = false;
#endif

namespace hlslib {

template <typename T>
class Stream;

template <typename T>
T ReadBlocking(Stream<T> &stream) {
  return stream.ReadBlocking();
}

template <typename T>
T ReadOptimistic(Stream<T> &stream) {
  return stream.ReadOptimistic();
}

template <typename T>
void WriteBlocking(Stream<T> &stream, T const &val, int size) {
  return stream.WriteBlocking(val, size);
}

template <typename T>
void WriteOptimistic(Stream<T> &stream, T const &val, int size) {
  return stream.WriteOptimistic(val, size);
}

template <typename T>
bool IsEmpty(Stream<T> &stream) {
  return stream.IsEmpty();
}

template <typename T>
bool IsEmptySimulation(Stream<T> &stream) {
  return stream.IsEmpty();
}

template <typename T>
bool IsFull(Stream<T> &stream, int size) {
  return stream.IsFull(size);
}

template <typename T>
bool IsFullSimulation(Stream<T> &stream, int size) {
  return stream.IsFull(size);
}


template <typename T>
class Stream {

public:

  Stream() : Stream("(unnamed)") {} 

  Stream(Stream<T> &&other) : Stream() {
    swap(*this, other);
  }

  Stream(std::string name) : name_(name) {} 

  friend void swap(Stream<T> &first, Stream<T> &second) {
    std::swap(first.queue_, second.queue_);
    std::swap(first.name_, second.name_);
  }

  // copy-and-swap
  Stream<T> &operator=(Stream<T> rhs) { // copy-and-swap
    swap(*this, rhs);
  }

  ~Stream() {
    if (queue_.size() > 0) {
      std::cerr << name_ << " contained " << queue_.size()
                << " elements at destruction.\n";
    }
  }

  T ReadBlocking() {
    std::unique_lock<std::mutex> lock(mutex_);
    bool slept = false;
    while (queue_.empty()) {
      if (kStreamVerbose && !slept) {
        std::stringstream ss;
        ss << name_ << " empty [sleeping].\n";
        std::cout << ss.str();
      }
      slept = true;
      cvRead_.wait(lock);
    }
    if (kStreamVerbose && slept) {
      std::stringstream ss;
      ss << name_ << " empty [woke up].\n";
      std::cout << ss.str();
    }
    auto front = queue_.front();
    queue_.pop();
    cvWrite_.notify_all();
    return front;
  }

  T ReadOptimistic() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty()) {
      throw std::runtime_error(name_ + ": read while empty.");
    }
    auto front = queue_.front();
    queue_.pop();
    cvWrite_.notify_all();
    return front;
  }
  
  void WriteBlocking(T const &val, int size) {
    std::unique_lock<std::mutex> lock(mutex_);
    bool slept = false;
    while (queue_.size() == size) {
      if (kStreamVerbose && !slept) {
        std::stringstream ss;
        ss << name_ << " full [" << queue_.size() << "/" << size
           << " elements, sleeping].\n";
        std::cout << ss.str();
      }
      slept = true;
      cvWrite_.wait(lock);
    }
    if (kStreamVerbose && slept) {
      std::stringstream ss;
      ss << name_ << " full [" << queue_.size() << "/" << size
         << " elements, woke up].\n";
      std::cout << ss.str();
    }
    queue_.emplace(val);
    cvRead_.notify_all();
  }
  
  void WriteOptimistic(T const &val, int size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() == size) {
      throw std::runtime_error(name_ + ": written while full.");
    }
    queue_.emplace(val);
    cvRead_.notify_all();
  }

  bool IsEmpty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() == 0;
  }

  bool IsFull(int size) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() == size;
  }

  size_t Size() const {
    return queue_.size();
  }

private:
  mutable std::mutex mutex_{};
  mutable std::condition_variable cvRead_{};
  mutable std::condition_variable cvWrite_{};
  std::string name_;
  std::queue<T> queue_;
};

} // End namespace hlslib

#endif

/// \author Johannes de Fine Licht (johannes.definelicht@inf.ethz.ch)
/// \date April 2016

#pragma once

#include "ap_int.h"

namespace hlslib {

template <typename T, int width>
class DataPack;

namespace {

template <unsigned byteWidth>
struct UnsignedIntType {};

template <>
struct UnsignedIntType<sizeof(unsigned char)> {
  using T = unsigned char;
};

template <>
struct UnsignedIntType<sizeof(unsigned short)> {
  using T = unsigned short;
};

template <>
struct UnsignedIntType<sizeof(unsigned int)> {
  using T = unsigned int;
};

template <>
struct UnsignedIntType<sizeof(unsigned long)> {
  using T = unsigned long;
};

template <typename T, int width>
class DataPackProxy {
  using Pack_t = typename UnsignedIntType<sizeof(T)>::T;
  static constexpr int kBits = 8 * width;
public:
  DataPackProxy(DataPack<T, width> &data, size_t index)
      : index_(index), data_(data) {
    #pragma HLS INLINE
  }
  ~DataPackProxy() {}
  void operator=(T const &rhs) {
    #pragma HLS INLINE
    data_.Set(index_, rhs);
  }
  void operator=(DataPackProxy<T, width> const &rhs) {
    #pragma HLS INLINE
    // Implicit call to operator T()
    data_.Set(index_, rhs);
  }
  operator T() const {
    #pragma HLS INLINE
    return data_.Get(index_);
  }
private:
  size_t index_;
  DataPack<T, width> &data_;
};

} // End anonymous namespace

template <typename T, int width>
class DataPack {

  static_assert(width > 0, "Width must be positive");

  using Pack_t = typename UnsignedIntType<sizeof(T)>::T;
  static constexpr int kBits = 8 * sizeof(T);

public:

  DataPack() : data_() {
    #pragma HLS INLINE
  }

  DataPack(T const &value) : data_() {
    #pragma HLS INLINE
    Fill(value);
  }

  DataPack(T const arr[width]) : data_() { 
    #pragma HLS INLINE
    Pack(arr);
  }

  DataPack(ap_uint<8 * width * sizeof(T)> const &data) : data_(data) {
    #pragma HLS INLINE
  }

  T Get(int i) const {
    #pragma HLS INLINE
    Pack_t temp = data_.range((i + 1) * kBits - 1, i * kBits);
    return *reinterpret_cast<T const *>(&temp);
  }

  void Set(int i, T value) {
    #pragma HLS INLINE
    Pack_t temp = *reinterpret_cast<Pack_t const *>(&value);
    data_.range((i + 1) * kBits - 1, i * kBits) = temp;
  }

  void Set(int i, ap_uint<kBits> value) {
    #pragma HLS INLINE
    data_.range((i + 1) * kBits - 1, i * kBits) = value;
  }

  void Fill(T const &value) {
    #pragma HLS INLINE
  DataPackFill:
    for (int i = 0; i < width; ++i) {
      Set(i, value);
    }
  }

  void Fill(ap_uint<kBits> const &apVal) {
    #pragma HLS INLINE
  DataPackFill:
    for (int i = 0; i < width; ++i) {
      Set(i, apVal);
    }
  }

  void Pack(T const arr[width]) {
    #pragma HLS INLINE
  DataPackPack:
    for (int i = 0; i < width; ++i) {
      #pragma HLS UNROLL
      Set(i, arr[i]);
    }
  }

  void Unpack(T arr[width]) const {
    #pragma HLS INLINE
  DataPackUnpack:
    for (int i = 0; i < width; ++i) {
      #pragma HLS UNROLL
      arr[i] = Get(i);
    }
  }

  void operator<<(T const arr[width]) {
    #pragma HLS INLINE
    Pack(arr);
  }

  void operator>>(T arr[width]) const {
    #pragma HLS INLINE
    Unpack(arr);
  }

  T operator[](const size_t i) const {
    #pragma HLS INLINE
    return Get(i);    
  }

  DataPackProxy<T, width> operator[](const size_t i) {
    #pragma HLS INLINE
    return DataPackProxy<T, width>(*this, i);
  }

  ap_uint<8 * width * sizeof(T)> data() const { return data_; }

  template <unsigned src, unsigned dst, unsigned count, int otherWidth>
  void ShiftTo(DataPack<T, otherWidth> &other) const {
    #pragma HLS INLINE
    static_assert(src + count <= width && dst + count <= otherWidth,
                  "Invalid range");
  DataPackShift:
    for (int i = 0, s = src, d = dst; i < count; ++i, ++s, ++d) {
      #pragma HLS UNROLL
      other.Set(d, Get(s));
    }
  }

private:

  ap_uint<8 * width * sizeof(T)> data_;

};

template <typename T, int width>
std::ostream& operator<<(std::ostream &os, DataPack<T, width> const &rhs) {
  os << "{" << rhs.Get(0);
  for (int i = 1; i < width; ++i) {
    os << ", " << rhs.Get(i);
  }
  os << "}";
  return os;
}

} // End namespace hlsUtil

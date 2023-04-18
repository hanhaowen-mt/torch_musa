#ifndef ATEN_SRC_ATEN_NATIVE_MUSA_MTGPUUTILS_H_
#define ATEN_SRC_ATEN_NATIVE_MUSA_MTGPUUTILS_H_

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <ATen/ATen.h>
#include <ATen/Dispatch.h>
#pragma GCC diagnostic pop

#include <mudnn.h>
#include "torch_musa/csrc/core/MUSAException.h"

namespace at {
namespace native {
namespace musa {

#define UNUSED(x) (void)(x)

#define CheckContiguous(input)                          \
  TORCH_CHECK(                                          \
      input.is_contiguous() && !input.storage_offset(), \
      "check contiguous and offset failed for pooling!");

#define CheckContiguousWithName(op_name, input)                  \
  TORCH_CHECK(                                                   \
      input.is_contiguous() && !input.storage_offset(),          \
      "check contiguous and no offset failed for unary op!(op:", \
      op_name,                                                   \
      "; contiguous:",                                           \
      input.is_contiguous(),                                     \
      "; offset:",                                               \
      input.storage_offset(),                                    \
      ")");

#define AT_DISPATCH_ALL_MTGPU_TYPES_AND_HALF(TYPE, NAME, ...)                 \
  AT_DISPATCH_SWITCH(                                                         \
      TYPE,                                                                   \
      NAME,                                                                   \
      AT_DISPATCH_CASE(at::ScalarType::Byte, __VA_ARGS__) AT_DISPATCH_CASE(   \
          at::ScalarType::Char, __VA_ARGS__)                                  \
          AT_DISPATCH_CASE(at::ScalarType::Half, __VA_ARGS__)                 \
              AT_DISPATCH_CASE(at::ScalarType::Short, __VA_ARGS__)            \
                  AT_DISPATCH_CASE(at::ScalarType::Float, __VA_ARGS__)        \
                      AT_DISPATCH_CASE(at::ScalarType::Int, __VA_ARGS__)      \
                          AT_DISPATCH_CASE(at::ScalarType::Long, __VA_ARGS__) \
                              AT_DISPATCH_CASE(                               \
                                  at::ScalarType::Double, __VA_ARGS__));

using muTensor = ::musa::dnn::Tensor;
using muHandle = ::musa::dnn::Handle;

constexpr DeviceType kMUSA = DeviceType::PrivateUse1;
constexpr ::c10::DispatchKey kMUSAKey = ::c10::DispatchKey::PrivateUse1;

#define MUSA_TENSOR_TYPE_CHECK(self)               \
  TORCH_CHECK(                                     \
      ((self.scalar_type() == ScalarType::Float) | \
       (self.scalar_type() == ScalarType::Int) |   \
       (self.scalar_type() == ScalarType::Long)),  \
      "Now muDNN only support float32, int32, and int64");

#define CHECK_MUDNN_STATUS(rst, msg)       \
  TORCH_CHECK(                             \
      rst == ::musa::dnn::Status::SUCCESS, \
      __FUNCTION__,                        \
      " MUDNN failed in: ",                \
      msg);

muTensor CreateMUTensor(const Tensor& t, bool use_stride = false);

muTensor CreateEmptyMUTensor();

// May need to contiguous the input pytorch tensor according the needed
// tensor format, so need to pass tensor as reference
void ConfigFormat(Tensor& t, muTensor& mt, bool auto_contiguous = false);

// use for memory handler
void InternalMemFree(void* ptr);
::musa::dnn::MemoryHandler InternalMemAlloc(size_t s);

bool is_musa(const Tensor& t);

} // namespace musa
} // namespace native
} // namespace at

#endif // ATEN_SRC_ATEN_NATIVE_MUSA_MTGPUUTILS_H_

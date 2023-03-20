#pragma once

#include <ATen/ATen.h>
#include <ATen/Dispatch.h>
#include <ATen/Functions.h>
#include <ATen/NativeFunctions.h>
#include <ATen/TensorUtils.h>
#include <ATen/Utils.h>
#include <ATen/native/TensorFactories.h>
#include <c10/core/Allocator.h>
#include <c10/core/TensorOptions.h>
#include <torch/library.h>

namespace at {
namespace native {
Tensor empty_mtgpu(
    IntArrayRef size,
    c10::optional<ScalarType> dtype_opt,
    c10::optional<Layout> layout_opt,
    c10::optional<Device> device_opt,
    c10::optional<bool> pin_memory_opt,
    c10::optional<c10::MemoryFormat> memory_format_opt);

// MusaContiguous create new tensor when self tensor's storage_offset > 0 and
// not contiguous
Tensor MusaContiguous(
    const Tensor& self,
    MemoryFormat memory_format = MemoryFormat::Contiguous);

} // namespace native
} // namespace at

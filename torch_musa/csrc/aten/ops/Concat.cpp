#include <ATen/Config.h>
#include <ATen/NativeFunctions.h>
#include <torch/library.h>

#include <ATen/NamedTensorUtils.h>
#include "torch_musa/csrc/aten/ops/TensorFactory.h"
#include "torch_musa/csrc/aten/utils/Utils.h"

#include <mudnn.h>

namespace at {
namespace musa {

Tensor& CatOut(const at::ITensorListRef& tensors, int64_t dim, Tensor& out) {
  if (out.numel() == 0) {
    return out;
  }
  const auto& materialized = tensors.materialize();
  const Tensor& ref = materialized[0].get();
  const OptionalDeviceGuard device_guard(device_of(ref));

  // Sicne muDNN concat doesn't support uncontiguous tensors,
  // so we store contiguous tensors for muTensors
  std::vector<Tensor> rt_tensors(materialized.size());
  int elements = 0;

  for (int idx = 0; idx < materialized.size(); ++idx) {
    if (materialized[idx].get().numel() > 0) {
      rt_tensors[idx] = materialized[idx].get().contiguous();
      elements++;
    }
  }

  // Computational muTensors
  std::vector<at::musa::muTensor> mu_tensors;
  mu_tensors.reserve(elements);
  for (const auto& tensor : rt_tensors) {
    mu_tensors.emplace_back(at::musa::CreateMUTensor(tensor));
  }

  at::musa::muTensor out_ = at::musa::CreateMUTensor(out);
  at::musa::muHandle& h = at::GetMudnnHandle();
  ::musa::dnn::Concat op;
  CHECK_MUDNN_STATUS(op.SetAxis(dim), "Set concat axis");
  CHECK_MUDNN_STATUS(
      op.Run(h, out_, elements, mu_tensors.data()), "Run Concat");

  return out;
}

Tensor Cat(const at::ITensorListRef& tensors, int64_t dim = 0) {
  const auto& materialized = tensors.materialize();
  const Tensor& ref = materialized[0].get();
  dim = dim < 0 ? dim + ref.dim() : dim;
  TORCH_CHECK(dim >= 0 && dim < ref.dim(), "Wrong Cat dim: ", dim);

  // TODO(@fan.mo): could be implemented in a more elegant way
  std::vector<int64_t> output_shape(ref.dim(), 0);
  for (const Tensor& tensor : materialized) {
    for (int d = 0; d < ref.dim(); ++d) {
      if (d == dim) {
        output_shape[d] += tensor.size(d);
      } else {
        output_shape[d] = tensor.size(d);
      }
    }
  }
  Tensor output = at::empty(
      output_shape, ref.options().memory_format(c10::MemoryFormat::Contiguous));
  CatOut(tensors, dim, output);

  return output;
}

TORCH_LIBRARY_IMPL(aten, PrivateUse1, m) {
  m.impl("cat", &Cat);
  m.impl("_cat", &Cat);
  m.impl("cat.out", &CatOut);
}

} // namespace musa
} // namespace at

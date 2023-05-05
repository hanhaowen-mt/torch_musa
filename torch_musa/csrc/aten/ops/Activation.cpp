#include <ATen/ATen.h>
#include <ATen/Config.h>
#include <ATen/NativeFunctions.h>
#include <ATen/native/Activation.h>
#include <torch/library.h>
#include <torch/torch.h>
#include <limits>

#include "torch_musa/csrc/aten/ops/TensorFactory.h"
#include "torch_musa/csrc/aten/utils/Utils.h"

namespace at {
namespace native {
namespace musa {

using UNARY_MODE = ::musa::dnn::Unary::Mode;

void UnaryCall(
    const std::string& op_name,
    Tensor& o,
    const Tensor& i,
    std::function<void(::musa::dnn::Unary&)> func) {
  muHandle& h = GetMudnnHandle();
  auto in = CreateMUTensor(i);
  auto out = CreateMUTensor(o);

  ::musa::dnn::Unary op;
  func(op);
  CHECK_MUDNN_STATUS(op.Run(h, out, in), "Run " + op_name);
}

// TODO (zaixing.wang): we should modify this function if muDNN supports other
// dytpe
void UnaryBoolOut(
    const std::string& op_name,
    Tensor& output,
    const Tensor& input,
    const Scalar& value,
    UNARY_MODE mode) {
  Tensor contiguous_input = Contiguous(input);
  UnaryCall(op_name, output, contiguous_input, [&](::musa::dnn::Unary& op) {
    if (contiguous_input.scalar_type() == ScalarType::Long ||
        contiguous_input.scalar_type() == ScalarType::Byte) {
      CHECK_MUDNN_STATUS(op.SetAlpha(value.to<int64_t>()), "SetAlpha");
    } else {
      CHECK_MUDNN_STATUS(op.SetAlpha(value.to<double>()), "SetAlpha");
    }
    CHECK_MUDNN_STATUS(op.SetMode(mode), "SetMode");
  });
}

// TODO(songtao.liu): muDNN requires bool tensor for lt/le... output,
// which cannot be autocast in muDNN now.
Tensor UnaryBool(
    const std::string& op_name,
    const Tensor& input,
    const Scalar& value,
    UNARY_MODE mode) {
  // as le/lt/ne/eq/gt/ge... ops return bool type
  Tensor output = empty_mtgpu(
      input.sizes().vec(),
      ScalarType::Bool,
      c10::nullopt,
      kMUSA,
      c10::nullopt,
      at::MemoryFormat::Contiguous);
  UnaryBoolOut(op_name, output, input, value, mode);
  return output;
}

Tensor Unary(
    const std::string& op_name,
    const Tensor& input,
    std::function<void(::musa::dnn::Unary&)> func) {
  Tensor contiguous_input = Contiguous(input);
  Tensor output = at::empty_like(contiguous_input);
  MUSA_TENSOR_TYPE_CHECK(contiguous_input);
  UnaryCall(op_name, output, contiguous_input, func);
  return output;
}

void Unary_(
    const std::string& op_name,
    Tensor& input,
    std::function<void(::musa::dnn::Unary&)> func) {
  CheckContiguousWithName(op_name, input);
  MUSA_TENSOR_TYPE_CHECK(input);
  UnaryCall(op_name, input, input, func);
}

void UnaryOut(
    const std::string& op_name,
    Tensor& output,
    const Tensor& input,
    std::function<void(::musa::dnn::Unary&)> func) {
  CheckContiguousWithName(op_name, output);
  Tensor contiguous_input = Contiguous(input);
  MUSA_TENSOR_TYPE_CHECK(contiguous_input);
  UnaryCall(op_name, output, contiguous_input, func);
}

#define DEFINE_ACTIVATE_OP(op_name, mode)                          \
  Tensor op_name(const Tensor& input) {                            \
    return Unary(__func__, input, [](::musa::dnn::Unary& op) {     \
      CHECK_MUDNN_STATUS(op.SetMode(mode), "SetMode");             \
    });                                                            \
  }                                                                \
                                                                   \
  Tensor& op_name##_(Tensor& input) {                              \
    Unary_(__func__, input, [](::musa::dnn::Unary& op) {           \
      CHECK_MUDNN_STATUS(op.SetMode(mode), "SetMode");             \
    });                                                            \
    return input;                                                  \
  }                                                                \
                                                                   \
  Tensor& op_name##Out(const Tensor& input, Tensor& output) {      \
    UnaryOut(__func__, output, input, [](::musa::dnn::Unary& op) { \
      CHECK_MUDNN_STATUS(op.SetMode(mode), "SetMode");             \
    });                                                            \
    return output;                                                 \
  }

DEFINE_ACTIVATE_OP(Relu, ::musa::dnn::Unary::Mode::RELU)
DEFINE_ACTIVATE_OP(Silu, ::musa::dnn::Unary::Mode::SILU)
DEFINE_ACTIVATE_OP(Sqrt, ::musa::dnn::Unary::Mode::SQRT)
DEFINE_ACTIVATE_OP(Tanh, ::musa::dnn::Unary::Mode::TANH)
DEFINE_ACTIVATE_OP(Sigmoid, ::musa::dnn::Unary::Mode::SIGMOID)
DEFINE_ACTIVATE_OP(Exp, ::musa::dnn::Unary::Mode::EXP)
DEFINE_ACTIVATE_OP(Sin, ::musa::dnn::Unary::Mode::SIN)
DEFINE_ACTIVATE_OP(Cos, ::musa::dnn::Unary::Mode::COS)
DEFINE_ACTIVATE_OP(Abs, ::musa::dnn::Unary::Mode::ABS)

#define SCALAR_COMPARISON(op_name, mode)                         \
  Tensor& op_name##Out(                                          \
      const Tensor& self, const Scalar& value, Tensor& output) { \
    UnaryBoolOut(__func__, output, self, value, mode);           \
    return output;                                               \
  }                                                              \
                                                                 \
  Tensor op_name(const Tensor& self, const Scalar& value) {      \
    Tensor output = empty_mtgpu(                                 \
        self.sizes().vec(),                                      \
        ScalarType::Bool,                                        \
        c10::nullopt,                                            \
        kMUSA,                                                   \
        c10::nullopt,                                            \
        at::MemoryFormat::Contiguous);                           \
    op_name##Out(self, value, output);                           \
    return output;                                               \
  }                                                              \
                                                                 \
  Tensor& op_name##_(Tensor& self, const Scalar& value) {        \
    self = UnaryBool(__func__, self, value, mode);               \
    return self;                                                 \
  }

SCALAR_COMPARISON(LeScalar, UNARY_MODE::LE)
SCALAR_COMPARISON(LtScalar, UNARY_MODE::LT)
SCALAR_COMPARISON(GeScalar, UNARY_MODE::GE)
SCALAR_COMPARISON(GtScalar, UNARY_MODE::GT)
SCALAR_COMPARISON(EqScalar, UNARY_MODE::EQ)
SCALAR_COMPARISON(NeScalar, UNARY_MODE::NE)

Tensor GELU(const Tensor& self, c10::string_view approximate) {
  auto approximate_type = get_gelutype_enum(approximate);
  TORCH_CHECK(
      approximate_type == GeluType::None,
      "Musa GELU op only support approximate is None now!");
  return Unary(__func__, self, [&](::musa::dnn::Unary& op) {
    CHECK_MUDNN_STATUS(op.SetMode(::musa::dnn::Unary::Mode::GELU), "SetMode");
  });
}

Tensor& GELUOut(
    const Tensor& self,
    c10::string_view approximate,
    Tensor& output) {
  auto approximate_type = get_gelutype_enum(approximate);
  TORCH_CHECK(
      approximate_type == GeluType::None,
      "Musa GELU op only support approximate is None now!");
  UnaryOut(__func__, output, self, [&](::musa::dnn::Unary& op) {
    CHECK_MUDNN_STATUS(op.SetMode(::musa::dnn::Unary::Mode::GELU), "SetMode");
  });
  return output;
}

void ClampCall(
    const std::string& op_name,
    Tensor& out,
    const Tensor& self,
    bool has_min,
    const c10::optional<Scalar>& min,
    bool has_max,
    const c10::optional<Scalar>& max) {
  auto t_type = self.scalar_type();
  auto self_ = Contiguous(self);

  switch (t_type) {
    case ScalarType::Float: {
      // DBL_MIN = 2.22507e-308 which is positive, so we must use lowest or
      // (-max) there !!!
      const double min_val = has_min ? min.value().to<double>()
                                     : std::numeric_limits<double>::lowest();

      const double max_val = has_max ? max.value().to<double>()
                                     : std::numeric_limits<double>::max();
      UnaryCall(op_name, out, self_, [&](::musa::dnn::Unary& op) {
        CHECK_MUDNN_STATUS(op.SetAlpha(min_val), "SetAlpha");
        CHECK_MUDNN_STATUS(op.SetBeta(max_val), "SetBeta");
        CHECK_MUDNN_STATUS(
            op.SetMode(::musa::dnn::Unary::Mode::CLIP), "SetMode");
      });
      break;
    }
    case ScalarType::Long: {
      // LONG_MIN = -9223372036854775808, LONG_MAX = 9223372036854775807
      const int64_t min_val = has_min ? min.value().to<int64_t>()
                                      : std::numeric_limits<int64_t>::min();
      const int64_t max_val = has_max ? max.value().to<int64_t>()
                                      : std::numeric_limits<int64_t>::max();
      UnaryCall(op_name, out, self_, [&](::musa::dnn::Unary& op) {
        CHECK_MUDNN_STATUS(op.SetAlpha(min_val), "SetAlpha");
        CHECK_MUDNN_STATUS(op.SetBeta(max_val), "SetBeta");
        CHECK_MUDNN_STATUS(
            op.SetMode(::musa::dnn::Unary::Mode::CLIP), "SetMode");
      });
      break;
    }

    default:
      TORCH_CHECK(false, "Unsupported tensor dtype: ", t_type);
      throw;
  }
}

Tensor Clamp(
    const Tensor& self,
    const c10::optional<Scalar>& min,
    const c10::optional<Scalar>& max) {
  const bool has_min = (min.has_value());
  const bool has_max = (max.has_value());
  TORCH_CHECK(
      has_min || has_max,
      "torch.clamp: either min, max or both scalars must be defined")
  Tensor output = at::empty_like(self);
  MUSA_TENSOR_TYPE_CHECK(self);

  ClampCall(__func__, output, self, has_min, min, has_max, max);
  return output;
}

Tensor& Clamp_(
    Tensor& self,
    const c10::optional<Scalar>& min,
    const c10::optional<Scalar>& max) {
  const bool has_min = (min.has_value());
  const bool has_max = (max.has_value());
  TORCH_CHECK(
      has_min || has_max,
      "torch.clamp: either min, max or both scalars must be defined")
  MUSA_TENSOR_TYPE_CHECK(self);

  ClampCall(__func__, self, self, has_min, min, has_max, max);
  return self;
}

Tensor& ClampOut(
    const Tensor& self,
    const c10::optional<Scalar>& min,
    const c10::optional<Scalar>& max,
    Tensor& out) {
  const bool has_min = (min.has_value());
  const bool has_max = (max.has_value());
  TORCH_CHECK(
      has_min || has_max,
      "torch.clamp: either min, max or both scalars must be defined")
  MUSA_TENSOR_TYPE_CHECK(self);

  ClampCall(__func__, out, self, has_min, min, has_max, max);
  return out;
}

Tensor& ClampTensorOut(
    const Tensor& self,
    const c10::optional<Tensor>& min,
    const c10::optional<Tensor>& max,
    Tensor& out) {
  TORCH_CHECK(
      self.device().type() == kMUSA,
      "Device of input tensor of Clamp must be MUSA, but now is ",
      self.device());
  TORCH_CHECK(
      out.device().type() == kMUSA,
      "Device of output tensor of Clamp.TensorOut must be MUSA, but now is ",
      out.device());
  auto has_min = min.has_value() && min->defined();
  auto has_max = max.has_value() && max->defined();
  Tensor cpu_min;
  Tensor cpu_max;
  if (has_min) {
    TORCH_CHECK(
        min->device().type() == kMUSA,
        "Device of min tensor of Clamp.TensorOut must be MUSA, but now is ",
        min->device());
    cpu_min = min->to("cpu");
  }
  if (has_max) {
    TORCH_CHECK(
        max->device().type() == kMUSA,
        "Device of max tensor of Clamp.TensorOut must be MUSA, but now is ",
        max->device());
    cpu_max = max->to("cpu");
  }
  out.resize_(self.sizes());
  TORCH_CHECK(
      has_min || has_max,
      "torch.clamp: At least one of 'min' or 'max' must not be None");
  auto cpu_self = self.to("cpu");
  const auto cpu_min_ref =
      has_min ? c10::optional<Tensor>(cpu_min) : c10::optional<Tensor>();
  const auto cpu_max_ref =
      has_max ? c10::optional<Tensor>(cpu_max) : c10::optional<Tensor>();
  auto cpu_result = clamp(cpu_self, cpu_min_ref, cpu_max_ref);
  out.copy_(cpu_result);
  return out;
}

Tensor Reciprocal(const Tensor& self) {
  return Unary(__func__, self, [&](::musa::dnn::Unary& op) {
    CHECK_MUDNN_STATUS(op.SetAlpha(-1.), "SetAlpha");
    CHECK_MUDNN_STATUS(op.SetMode(::musa::dnn::Unary::Mode::POW), "SetMode");
  });
}

Tensor& Reciprocal_(Tensor& self) {
  Unary_(__func__, self, [&](::musa::dnn::Unary& op) {
    CHECK_MUDNN_STATUS(op.SetAlpha(-1.), "SetAlpha");
    CHECK_MUDNN_STATUS(op.SetMode(::musa::dnn::Unary::Mode::POW), "SetMode");
  });
  return self;
}

Tensor& ReciprocalOut(const Tensor& self, Tensor& output) {
  UnaryOut(__func__, output, self, [&](::musa::dnn::Unary& op) {
    CHECK_MUDNN_STATUS(op.SetAlpha(-1.), "SetAlpha");
    CHECK_MUDNN_STATUS(op.SetMode(::musa::dnn::Unary::Mode::POW), "SetMode");
  });
  return output;
}
Tensor Neg(const Tensor& self) {
  return Unary(__func__, self, [&](::musa::dnn::Unary& op) {
    op.SetMode(::musa::dnn::Unary::Mode::MUL);
    Scalar value = -1;
    op.SetAlpha(value.to<double>());
  });
}

Tensor& Neg_(Tensor& self) {
  Unary_(__func__, self, [&](::musa::dnn::Unary& op) {
    CHECK_MUDNN_STATUS(op.SetMode(::musa::dnn::Unary::Mode::MUL), "SetMode");
    Scalar value = -1;
    CHECK_MUDNN_STATUS(op.SetAlpha(value.to<double>()), "SetAlpha");
  });
  return self;
}

Tensor& NegOut(const Tensor& self, Tensor& out) {
  UnaryOut(__func__, out, self, [&](::musa::dnn::Unary& op) {
    CHECK_MUDNN_STATUS(op.SetMode(::musa::dnn::Unary::Mode::MUL), "SetMode");
    Scalar value = -1;
    CHECK_MUDNN_STATUS(op.SetAlpha(value.to<double>()), "SetAlpha");
  });
  return out;
}

Tensor PowScalar(const Tensor& self, const Scalar& value) {
  return Unary(__func__, self, [&](::musa::dnn::Unary& op) {
    CHECK_MUDNN_STATUS(op.SetAlpha(value.to<double>()), "SetAlpha");
    CHECK_MUDNN_STATUS(op.SetMode(::musa::dnn::Unary::Mode::POW), "SetMode");
  });
}

Tensor& PowScalar_(Tensor& self, const Scalar& value) {
  Unary_("pow_.Scalar", self, [&](::musa::dnn::Unary& op) {
    CHECK_MUDNN_STATUS(op.SetAlpha(value.to<double>()), "SetAlpha");
    CHECK_MUDNN_STATUS(op.SetMode(::musa::dnn::Unary::Mode::POW), "SetMode");
  });
  return self;
}

Tensor& PowScalarOut(const Tensor& self, const Scalar& value, Tensor& output) {
  UnaryOut("pow.Tensor_Scalar_out", output, self, [&](::musa::dnn::Unary& op) {
    CHECK_MUDNN_STATUS(op.SetAlpha(value.to<double>()), "SetAlpha");
    CHECK_MUDNN_STATUS(op.SetMode(::musa::dnn::Unary::Mode::POW), "SetMode");
  });
  return output;
}

TORCH_LIBRARY_IMPL(aten, PrivateUse1, m) {
  m.impl("abs", &Abs);
  m.impl("abs_", &Abs_);
  m.impl("abs.out", &AbsOut);

  m.impl("eq.Scalar", &EqScalar);
  m.impl("eq_.Scalar", &EqScalar_);
  m.impl("eq.Scalar_out", &EqScalarOut);

  m.impl("relu", &Relu);
  m.impl("relu_", &Relu_);

  m.impl("lt.Scalar", &LtScalar);
  m.impl("lt_.Scalar", &LtScalar_);
  m.impl("lt.Scalar_out", &LtScalarOut);

  m.impl("le.Scalar", &LeScalar);
  m.impl("le_.Scalar", &LeScalar_);
  m.impl("le.Scalar_out", &LeScalarOut);

  m.impl("ne.Scalar", &NeScalar);
  m.impl("ne_.Scalar", &NeScalar_);
  m.impl("ne.Scalar_out", &NeScalarOut);

  m.impl("gt.Scalar", &GtScalar);
  m.impl("gt_.Scalar", &GtScalar_);
  m.impl("gt.Scalar_out", &GtScalarOut);

  m.impl("ge.Scalar", &GeScalar);
  m.impl("ge_.Scalar", &GeScalar_);
  m.impl("ge.Scalar_out", &GeScalarOut);

  m.impl("sqrt", &Sqrt);
  m.impl("sqrt.out", &SqrtOut);

  m.impl("tanh", &Tanh);
  m.impl("tanh_", &Tanh_);
  m.impl("tanh.out", &TanhOut);

  m.impl("gelu", &GELU);
  m.impl("gelu.out", &GELUOut);

  m.impl("clamp", &Clamp);
  m.impl("clamp_", &Clamp_);
  m.impl("clamp.out", &ClampOut);
  m.impl("clamp.Tensor_out", &ClampTensorOut);

  m.impl("reciprocal", &Reciprocal);
  m.impl("reciprocal_", &Reciprocal_);
  m.impl("reciprocal.out", &ReciprocalOut);

  m.impl("sigmoid", &Sigmoid);
  m.impl("sigmoid_", &Sigmoid_);
  m.impl("sigmoid.out", &SigmoidOut);

  m.impl("exp", &Exp);
  m.impl("exp_", &Exp_);
  m.impl("exp.out", &ExpOut);

  m.impl("silu", &Silu);
  m.impl("silu_", &Silu_);
  m.impl("silu.out", &SiluOut);

  m.impl("cos", &Cos);
  m.impl("cos_", &Cos_);
  m.impl("cos.out", &CosOut);

  m.impl("sin", &Sin);
  m.impl("sin_", &Sin_);
  m.impl("sin.out", &SinOut);

  m.impl("neg", &Neg);
  m.impl("neg_", &Neg_);
  m.impl("neg.out", &NegOut);

  m.impl("pow.Tensor_Scalar", &PowScalar);
  m.impl("pow_.Scalar", &PowScalar_);
  m.impl("pow.Tensor_Scalar_out", &PowScalarOut);
}

} // namespace musa
} // namespace native
} // namespace at

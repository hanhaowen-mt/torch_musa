#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <ATen/ATen.h>
#include <ATen/Config.h>
#include <ATen/ExpandUtils.h>
#include <ATen/NativeFunctions.h>
#include <ATen/native/BinaryOps.h>
#include <torch/library.h>

// Restore disabled warnings
#pragma GCC diagnostic pop

#include "torch_musa/csrc/aten/ops/TensorFactory.h"
#include "torch_musa/csrc/aten/utils/Utils.h"

#include <mudnn.h>

namespace at {
namespace native {
namespace musa {
using BINARY_MODE = ::musa::dnn::Binary::Mode;
using UNARY_MODE = ::musa::dnn::Unary::Mode;

void BinaryCall(
    const std::string& op_name,
    Tensor& output,
    const Tensor& self,
    const Tensor& other,
    BINARY_MODE m = BINARY_MODE::ADD,
    const Scalar alpha_scalar = 1) {
  muHandle h;
  Tensor other_tmp = alpha_scalar.equal(1) ? other : at::empty_like(other);
  auto other_mt = CreateMUTensor(other_tmp);

  if (!alpha_scalar.equal(1)) {
    ::musa::dnn::Unary uop;
    CHECK_MUDNN_STATUS(uop.SetAlpha(alpha_scalar.toDouble()), "SetAlpha");
    CHECK_MUDNN_STATUS(uop.SetMode(UNARY_MODE::MUL), "SetMode");
    auto other_in = CreateMUTensor(other);
    CHECK_MUDNN_STATUS(uop.Run(h, other_mt, other_in), "Run " + op_name);
  }

  auto output_sizes = infer_size_dimvector(self.sizes(), other.sizes());
  output.resize_(output_sizes);

  ::musa::dnn::Binary bop;
  auto contiguous_self = CreateMUTensor(self);
  auto om = CreateMUTensor(output);
  CHECK_MUDNN_STATUS(bop.SetMode(m), "SetMode");
  CHECK_MUDNN_STATUS(
      bop.Run(h, om, contiguous_self, other_mt), "Run " + op_name);
}

inline bool IsComparisonOp(const BINARY_MODE m) {
  return m == BINARY_MODE::EQ || m == BINARY_MODE::NE || m == BINARY_MODE::GE ||
      m == BINARY_MODE::GT || m == BINARY_MODE::LE || m == BINARY_MODE::LT;
}

Tensor Binary(
    const std::string op_name,
    const Tensor& self,
    const Tensor& other,
    BINARY_MODE m = BINARY_MODE::ADD,
    Scalar const& alpha_scalar = 1,
    bool inplace = false) {
  TORCH_CHECK(
      self.scalar_type() == other.scalar_type(),
      "input scalar type must the same");
  if (inplace) {
    TORCH_CHECK(
        is_expandable_to(other.sizes(), self.sizes()),
        "size {",
        self.sizes(),
        "} is not expandable to size {",
        other.sizes(),
        "}.");
  }

  if (self.numel() == 0 && other.numel() == 0) {
    if (IsComparisonOp(m)) {
      auto output_sizes = infer_size_dimvector(self.sizes(), other.sizes());
      return at::empty(output_sizes, self.options().dtype(ScalarType::Bool));
    } else {
      return at::empty({0}, self.options());
    }
  }

  Tensor contiguous_self = Contiguous(self);
  Tensor contiguous_other = Contiguous(other);

  // In some special case that 'other' is scalar and on cpu, UnaryOp could take
  // the place of BinaryOp to optimize performance. such as:
  // torch.tensor([1.2, 2.4]) * 2.0
  auto SupportOptimizeScalarToUnary = [](BINARY_MODE m,
                                         const Tensor& input,
                                         const Tensor& other) {
    const bool support_mode_type =
        (m == BINARY_MODE::ADD && input.scalar_type() == ScalarType::Float) ||
        (m == BINARY_MODE::TRUEDIV &&
         input.scalar_type() == ScalarType::Float) ||
        (m == BINARY_MODE::MUL &&
         (input.scalar_type() == ScalarType::Float ||
          input.scalar_type() == ScalarType::Long));
    const bool support_dim_device =
        other.dim() == 0 && other.device() == DeviceType::CPU;
    return support_mode_type && support_dim_device;
  };
  const bool optimize_scalar_unary =
      SupportOptimizeScalarToUnary(m, self, other);
  if (!optimize_scalar_unary) {
    // 1. deal with other or self tensor on cpu
    // 2. deal with other and self tensor shape isn't same
    if (other.dim() == 0) {
      contiguous_other = at::full_like(contiguous_self, other.item(), kMUSA);
    }
    if (self.dim() == 0) {
      contiguous_self = at::full_like(contiguous_other, self.item(), kMUSA);
    }
  }

  Tensor output;
  if (inplace) {
    TORCH_CHECK(
        is_expandable_to(other.sizes(), self.sizes()),
        "size {",
        self.sizes(),
        "} is not expandable to size {",
        other.sizes(),
        "}.");
    if (self.dim() == 0) {
      output = Contiguous(self);
    } else {
      output = contiguous_self;
    }
  } else {
    auto output_sizes = infer_size_dimvector(self.sizes(), other.sizes());
    TORCH_CHECK(
        is_expandable_to(other.sizes(), output_sizes),
        "size {",
        self.sizes(),
        "} is not expandable to size {",
        output_sizes,
        "}.");
    TORCH_CHECK(
        is_expandable_to(self.sizes(), output_sizes),
        "size {",
        self.sizes(),
        "} is not expandable to size {",
        output_sizes,
        "}.");
    // Allocate output with bool dtype about some modes
    if (m == BINARY_MODE::EQ || m == BINARY_MODE::NE || m == BINARY_MODE::GE ||
        m == BINARY_MODE::GT || m == BINARY_MODE::LE || m == BINARY_MODE::LT ||
        m == BINARY_MODE::LOGICAL_AND || m == BINARY_MODE::LOGICAL_OR ||
        m == BINARY_MODE::LOGICAL_XOR) {
      output = empty_mtgpu(
          output_sizes,
          ScalarType::Bool,
          c10::nullopt,
          kMUSA,
          c10::nullopt,
          at::MemoryFormat::Contiguous);
    } else {
      output = empty_mtgpu(
          output_sizes,
          contiguous_self.scalar_type(),
          c10::nullopt,
          kMUSA,
          c10::nullopt,
          at::MemoryFormat::Contiguous);
    }
  }

  if (optimize_scalar_unary) {
    muHandle h;
    ::musa::dnn::Unary uop;
    auto other_scalar = contiguous_other.item();
    auto ConvertBinaryModeToString = [](BINARY_MODE mode) -> std::string {
      switch (mode) {
        case BINARY_MODE::ADD:
          return "Binary ADD";
        case BINARY_MODE::MUL:
          return "Binary MUL";
        case BINARY_MODE::TRUEDIV:
          return "Binary TRUEDIV";
        default:
          return "";
      }
    };
    if (other_scalar.isFloatingPoint()) {
      CHECK_MUDNN_STATUS(uop.SetAlpha(other_scalar.toDouble()), "SetAlpha");
    } else if (other_scalar.isIntegral(false)) {
      CHECK_MUDNN_STATUS(uop.SetAlpha(other_scalar.toLong()), "SetAlpha");
    } else {
      AT_ERROR(
          other_scalar.type(),
          " is not implemented for '",
          ConvertBinaryModeToString(m),
          "'!");
    }
    if (m == BINARY_MODE::MUL) {
      CHECK_MUDNN_STATUS(uop.SetMode(UNARY_MODE::MUL), "SetMode");
    } else if (m == BINARY_MODE::ADD) {
      CHECK_MUDNN_STATUS(uop.SetMode(UNARY_MODE::ADD), "SetMode");
    } else if (m == BINARY_MODE::TRUEDIV) {
      CHECK_MUDNN_STATUS(uop.SetMode(UNARY_MODE::DIV), "SetMode");
    }
    auto mt_output = CreateMUTensor(output);
    auto mt_input = CreateMUTensor(contiguous_self);
    CHECK_MUDNN_STATUS(uop.Run(h, mt_output, mt_input), "Run " + op_name);
  } else {
    BinaryCall(
        op_name, output, contiguous_self, contiguous_other, m, alpha_scalar);
  }
  return inplace ? self.copy_(output) : output;
}

Tensor BinarycommonDtype(
    const std::string& op_name,
    const Tensor& self,
    const Tensor& other,
    Scalar const& alpha_scalar,
    BINARY_MODE m) {
  auto common_dtype = at::result_type(self, other);
  alpha_check(common_dtype, alpha_scalar);
  Tensor contiguous_self = self.to(common_dtype);
  Tensor contiguous_other = other.to(common_dtype);
  return Binary(op_name, contiguous_self, contiguous_other, m, alpha_scalar);
}

void BinarycommonDtypeInternal(
    const std::string& op_name,
    const Tensor& self,
    const Tensor& other,
    Scalar const& alpha_scalar,
    BINARY_MODE m) {
  auto common_dtype = at::result_type(self, other);
  alpha_check(common_dtype, alpha_scalar);
  Tensor contiguous_other = other.to(common_dtype);
  Binary(op_name, self, contiguous_other, m, alpha_scalar, true);
  return;
}

void BinarycommonDtypeCall(
    const std::string& op_name,
    const Tensor& self,
    const Tensor& other,
    Scalar const& alpha_scalar,
    Tensor& output,
    BINARY_MODE m) {
  auto common_dtype = at::result_type(self, other);
  alpha_check(common_dtype, alpha_scalar);
  Tensor contiguous_self = Contiguous(self.to(common_dtype));
  Tensor contiguous_other = Contiguous(other.to(common_dtype));
  BinaryCall(
      op_name, output, contiguous_self, contiguous_other, m, alpha_scalar);
}

// TODO(mt-ai): All the binary operations should be moved to the musa
// implementation and compiled using mcc.
#define DEFINE_BINARY_SCALAR_OP(op_name, mode)                                \
  Tensor op_name##Tensor(                                                     \
      const Tensor& self, const Tensor& other, Scalar const& alpha_scalar) {  \
    return BinarycommonDtype(__func__, self, other, alpha_scalar, mode);      \
  }                                                                           \
                                                                              \
  Tensor& op_name##_Tensor(                                                   \
      Tensor& self, const Tensor& other, Scalar const& alpha_scalar) {        \
    BinarycommonDtypeInternal(__func__, self, other, alpha_scalar, mode);     \
    return self;                                                              \
  }                                                                           \
                                                                              \
  Tensor& op_name##_out(                                                      \
      const Tensor& self,                                                     \
      const Tensor& other,                                                    \
      Scalar const& alpha_scalar,                                             \
      Tensor& output) {                                                       \
    BinarycommonDtypeCall(__func__, self, other, alpha_scalar, output, mode); \
    return output;                                                            \
  }

DEFINE_BINARY_SCALAR_OP(Add, BINARY_MODE::ADD)
DEFINE_BINARY_SCALAR_OP(Sub, BINARY_MODE::SUB)

#define DEFINE_BINARY_OP(op_name, mode)                             \
  Tensor op_name##Tensor(const Tensor& self, const Tensor& other) { \
    return BinarycommonDtype(__func__, self, other, 1, mode);       \
  }                                                                 \
                                                                    \
  Tensor& op_name##_Tensor(Tensor& self, const Tensor& other) {     \
    BinarycommonDtypeInternal(__func__, self, other, 1, mode);      \
    return self;                                                    \
  }                                                                 \
                                                                    \
  Tensor& op_name##_out(                                            \
      const Tensor& self, const Tensor& other, Tensor& output) {    \
    BinarycommonDtypeCall(__func__, self, other, 1, output, mode);  \
    return output;                                                  \
  }

DEFINE_BINARY_OP(Mul, BINARY_MODE::MUL)
DEFINE_BINARY_OP(Div, BINARY_MODE::TRUEDIV)
DEFINE_BINARY_OP(Equal, BINARY_MODE::EQ)
DEFINE_BINARY_OP(NotEqual, BINARY_MODE::NE)
DEFINE_BINARY_OP(Greater, BINARY_MODE::GT)
DEFINE_BINARY_OP(GreaterEqual, BINARY_MODE::GE)

Tensor& Div_out_mode(
    const Tensor& self,
    const Tensor& other,
    c10::optional<c10::string_view> rounding_mode,
    Tensor& output) {
  if (!rounding_mode.has_value()) {
    BinarycommonDtypeCall(
        __func__, self, other, 1, output, BINARY_MODE::TRUEDIV);
  } else if (*rounding_mode == "trunc") {
    BinarycommonDtypeCall(
        __func__, self, other, 1, output, BINARY_MODE::TRUNCATEDIV);
  } else if (*rounding_mode == "floor") {
    BinarycommonDtypeCall(
        __func__, self, other, 1, output, BINARY_MODE::FLOORDIV);
  }
  return output;
}

Tensor DivTensor_mode(
    const Tensor& self,
    const Tensor& other,
    c10::optional<c10::string_view> rounding_mode) {
  if (!rounding_mode.has_value()) {
    return BinarycommonDtype(__func__, self, other, 1, BINARY_MODE::TRUEDIV);
  } else if (*rounding_mode == "trunc") {
    return BinarycommonDtype(
        __func__, self, other, 1, BINARY_MODE::TRUNCATEDIV);
  } else if (*rounding_mode == "floor") {
    return BinarycommonDtype(__func__, self, other, 1, BINARY_MODE::FLOORDIV);
  } else {
    return self;
  }
}

Tensor& Div_Tensor_mode(
    Tensor& self,
    const Tensor& other,
    c10::optional<c10::string_view> rounding_mode) {
  if (!rounding_mode.has_value()) {
    BinarycommonDtypeInternal(__func__, self, other, 1, BINARY_MODE::TRUEDIV);
  } else if (*rounding_mode == "trunc") {
    BinarycommonDtypeInternal(
        __func__, self, other, 1, BINARY_MODE::TRUNCATEDIV);
  } else if (*rounding_mode == "floor") {
    BinarycommonDtypeInternal(__func__, self, other, 1, BINARY_MODE::FLOORDIV);
  }
  return self;
}

TORCH_LIBRARY_IMPL(aten, PrivateUse1, m) {
  m.impl("add.Tensor", &AddTensor);
  m.impl("add_.Tensor", &Add_Tensor);
  m.impl("add.out", &Add_out);

  m.impl("div.Tensor", &DivTensor);
  m.impl("div.Tensor_mode", &DivTensor_mode);
  m.impl("div_.Tensor_mode", &Div_Tensor_mode);
  m.impl("div_.Tensor", &Div_Tensor);
  m.impl("div.out", &Div_out);
  m.impl("div.out_mode", &Div_out_mode);

  m.impl("eq.Tensor", &EqualTensor);
  m.impl("eq_.Tensor", &Equal_Tensor);
  m.impl("eq.Tensor_out", &Equal_out);

  m.impl("ge.Tensor", &GreaterEqualTensor);
  m.impl("ge_.Tensor", &GreaterEqual_Tensor);
  m.impl("ge.Tensor_out", &GreaterEqual_out);
  // greater_equal, alias for torch.ge
  m.impl("greater_equal.Tensor", &GreaterEqualTensor);
  m.impl("greater_equal_.Tensor", &GreaterEqual_Tensor);
  m.impl("greater_equal.Tensor_out", &GreaterEqual_out);

  m.impl("gt.Tensor", &GreaterTensor);
  m.impl("gt_.Tensor", &Greater_Tensor);
  m.impl("gt.Tensor_out", &Greater_out);
  // greater, alias for torch.gt
  m.impl("greater.Tensor", &GreaterTensor);
  m.impl("greater_.Tensor", &Greater_Tensor);
  m.impl("greater.Tensor_out", &Greater_out);

  m.impl("mul.Tensor", &MulTensor);
  m.impl("mul_.Tensor", &Mul_Tensor);
  m.impl("mul.out", &Mul_out);

  m.impl("ne.Tensor", &NotEqualTensor);
  m.impl("ne_.Tensor", &NotEqual_Tensor);
  m.impl("ne.Tensor_out", &NotEqual_out);
  // not_equal, alias for torch.ne
  m.impl("not_equal.Tensor", &NotEqualTensor);
  m.impl("not_equal_.Tensor", &NotEqual_Tensor);
  m.impl("not_equal.Tensor_out", &NotEqual_out);

  m.impl("sub.Tensor", &SubTensor);
  m.impl("sub_.Tensor", &Sub_Tensor);
  m.impl("sub.out", &Sub_out);
}

} // namespace musa
} // namespace native
} // namespace at

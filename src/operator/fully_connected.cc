/*!
 * Copyright (c) 2015 by Contributors
 * \file fully_connected.cc
 * \brief fully connect operator
*/
#include "./fully_connected-inl.h"
#if MXNET_USE_MKL2017 == 1
#include <mkl_memory.h>
#include "./mkl/mkl_memory-inl.h"
#include "./mkl/mkl_fully_connected-inl.h"
#endif  // MXNET_USE_MKL2017
#if MXNET_USE_NNPACK == 1
#include "./nnpack/nnpack_fully_connected-inl.h"
#endif  // MXNET_USE_NNPACK

namespace mxnet {
namespace op {
template<>
Operator* CreateOp<cpu>(FullyConnectedParam param, int dtype,
                        std::vector<TShape> *in_shape,
                        std::vector<TShape> *out_shape,
                        Context ctx) {
  Operator *op = NULL;
#if MXNET_USE_MKL2017 == 1
  switch (dtype) {
  case mshadow::kFloat32:
    return new MKLFullyConnectedOp<cpu, float>(param, *in_shape, *out_shape);
  case mshadow::kFloat64:
    return new MKLFullyConnectedOp<cpu, double>(param, *in_shape, *out_shape);
  default:
    LOG(INFO) << MKLFullyConnectedOp<cpu, float>::getName() << " Skip MKL optimization";
    break;
  }
#endif
#if MXNET_USE_NNPACK == 1
  const size_t batch_size = (*in_shape)[0][0];
  // nnp_fully_connected_inference will do optimization for batch-size = 1
  // nnp_fully_connected_output will do optimization for batch-size > 1
  // but just found FullyConnected in NNPACK result is wrong when batch_size != 2^n
  // so here only using NNPACK when batch_size = 2^n.
  if ((batch_size == 1) || ((batch_size > 1) && (!(batch_size & (batch_size - 1))))) {
    switch (dtype) {
    case mshadow::kFloat32:
      return new NNPACKFullyConnectedOp<cpu, float>(param);
    default:
      break;
    }
  }
#endif
  switch (dtype) {
  case mshadow::kFloat32:
    op = new FullyConnectedOp<cpu, float>(param);
    break;
  case mshadow::kFloat64:
    op = new FullyConnectedOp<cpu, double>(param);
    break;
  case mshadow::kFloat16:
    LOG(FATAL) << "float16 fully connected layer is currently"
                  "only supported by CuDNN version.";
    break;
  default:
    LOG(FATAL) << "Unsupported type " << dtype;
  }

  return op;
}

// DO_BIND_DISPATCH comes from operator_common.h
Operator *FullyConnectedProp::CreateOperatorEx(Context ctx, std::vector<TShape> *in_shape,
                                     std::vector<int> *in_type) const {
  std::vector<TShape> out_shape(1, TShape()), aux_shape;
  std::vector<int> out_type(1, -1), aux_type;
  CHECK(InferType(in_type, &out_type, &aux_type));
  CHECK(InferShape(in_shape, &out_shape, &aux_shape));
  DO_BIND_DISPATCH(CreateOp, param_, (*in_type)[0], in_shape, &out_shape, ctx);
}

DMLC_REGISTER_PARAMETER(FullyConnectedParam);

MXNET_REGISTER_OP_PROPERTY(FullyConnected, FullyConnectedProp)
.describe(R"code(Apply a linear transformation: :math:`Y = XW^T + b`.

Shapes:

- **data**: `(batch_size, input_dim)`
- **weight**: `(num_hidden, input_dim)`
- **bias**: `(num_hidden,)`
- **out**: `(batch_size, num_hidden)`

The learnable parameters include both ``weight`` and ``bias``.

If ``no_bias`` is set to be true, then the ``bias`` term is ignored.

)code" ADD_FILELINE)
.add_argument("data", "ndarray-or-symbol", "Input data.")
.add_argument("weight", "ndarray-or-symbol", "Weight matrix.")
.add_argument("bias", "ndarray-or-symbol", "Bias parameter.")
.add_arguments(FullyConnectedParam::__FIELDS__());
}  // namespace op
}  // namespace mxnet

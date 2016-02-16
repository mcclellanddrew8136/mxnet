/*!
 * Copyright (c) 2015, 2016 by Contributors
 * \file dropout_allchannels.cc
 * \brief
 * \author Bing Xu, Kai Londenberg
*/

#include "./dropout_allchannels-inl.h"

namespace mxnet {
namespace op {
template<>
Operator *CreateOp<cpu>(DropoutAllchannelsParam param) {
  return new DropoutAllchannelsOp<cpu>(param);
}

// DO_BIND_DISPATCH comes from operator_common.h
Operator *DropoutAllchannelsProp::CreateOperator(Context ctx) const {
  DO_BIND_DISPATCH(CreateOp, param_);
}

DMLC_REGISTER_PARAMETER(DropoutAllchannelsParam);

MXNET_REGISTER_OP_PROPERTY(DropoutAllchannels, DropoutAllchannelsProp)
.describe("Apply dropout on a batch of image or image-like inputs, always dropping or keeping all channels of a given pixel.")
.add_argument("data", "Symbol", "Input data to apply dropout to. Has to be a 4D Tensor with shape (samples, channels, rows, cols)")
.add_arguments(DropoutAllchannelsParam::__FIELDS__());

}  // namespace op
}  // namespace mxnet



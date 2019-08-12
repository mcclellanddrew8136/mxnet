/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 *  Copyright (c) 2019 by Contributors
 * \file np_matrix_op-inl.h
 * \brief Function definition of matrix related operators
 */
#ifndef MXNET_OPERATOR_NUMPY_NP_MATRIX_OP_INL_H_
#define MXNET_OPERATOR_NUMPY_NP_MATRIX_OP_INL_H_

#include <vector>
#include <string>
#include "../nn/concat-inl.h"
#include "../tensor/matrix_op-inl.h"
#include "np_broadcast_reduce_op.h"

namespace mxnet {
namespace op {

struct NumpyTransposeParam : public dmlc::Parameter<NumpyTransposeParam> {
  mxnet::TShape axes;
  DMLC_DECLARE_PARAMETER(NumpyTransposeParam) {
    DMLC_DECLARE_FIELD(axes).set_default(mxnet::TShape(-1, 0))
    .describe("By default, reverse the dimensions, otherwise permute "
              "the axes according to the values given.");
  }
};

template<typename xpu>
void NumpyTranspose(const nnvm::NodeAttrs& attrs,
                    const OpContext& ctx,
                    const std::vector<TBlob>& inputs,
                    const std::vector<OpReqType>& req,
                    const std::vector<TBlob>& outputs) {
  const NumpyTransposeParam& param = nnvm::get<NumpyTransposeParam>(attrs.parsed);
  CHECK_EQ(req[0], kWriteTo) << "Transpose does not support inplace";
  if (ndim_is_known(param.axes)) {
    TransposeImpl<xpu>(ctx.run_ctx, inputs[0], outputs[0], param.axes);
  } else {
    mxnet::TShape axes(inputs[0].ndim(), -1);
    for (int i = 0; i < axes.ndim(); ++i) {
      axes[i] = axes.ndim() - 1 - i;
    }
    TransposeImpl<xpu>(ctx.run_ctx, inputs[0], outputs[0], axes);
  }
}

struct NumpyXReshapeParam : public dmlc::Parameter<NumpyXReshapeParam> {
  mxnet::Tuple<int> newshape;
  std::string order;
  DMLC_DECLARE_PARAMETER(NumpyXReshapeParam) {
      DMLC_DECLARE_FIELD(newshape)
          .set_default(mxnet::Tuple<int>())
          .describe("The new shape should be compatible with the original shape."
                    " If an integer, then the result will be a 1-D array of that length."
                    " One shape dimension can be -1. In this case, the value is inferred"
                    " from the length of the array and remaining dimensions."
                    " -2 to -6 are used for data manipulation"
                    " -2 copy this dimension from the input to the output shape"
                    " -3 will skip current dimension if and only if the current dim size is one"
                    " -4 copy all remain of the input dimensions to the output shape"
                    " -5 use the product of two consecutive dimensions of the input"
                    " shape as the output"
                    " -6 split one dimension of the input into two dimensions passed"
                    " subsequent to -6 in the new shape");
      DMLC_DECLARE_FIELD(order)
      .set_default("C")
      .describe("Read the elements of a using this index order, and place the elements into"
                " the reshaped array using this index order. 'C' means to read/write the elements"
                " using C-like index order, with the last axis index changing fastest, back to the"
                " first axis index changing slowest. Note that currently only C-like order is"
                " supported");
  }
};

}  // namespace op
}  // namespace mxnet

#endif  // MXNET_OPERATOR_NUMPY_NP_MATRIX_OP_INL_H_

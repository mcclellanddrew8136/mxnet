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
 * Copyright (c) 2018 by Contributors
 * \file shuffle_op.cc
 * \brief Operator to shuffle elements of an NDArray
 */
#if (__GNUC__ > 4 && !defined(__clang__major__)) || (__clang_major__ > 4 && __linux__)
  #define USE_GNU_PARALLEL_SHUFFLE
#endif

#include <mxnet/operator_util.h>
#include <algorithm>
#include <random>
#include <vector>
#ifdef USE_GNU_PARALLEL_SHUFFLE
  #include <parallel/algorithm>
#endif
#include "../elemwise_op_common.h"

namespace mxnet {
namespace op {

namespace {

  template<typename DType, typename Rand>
  void Shuffle1D(DType* out, index_t size, Rand* prnd) {
    #ifdef USE_GNU_PARALLEL_SHUFFLE
      auto rand_n = [prnd](index_t n) {
        std::uniform_int_distribution<index_t> dist(0, n - 1);
        return dist(*prnd);
      };
      __gnu_parallel::random_shuffle(out, out + size, rand_n);
    #else
      std::shuffle(out, out + size, *prnd);
    #endif
  }

  template<typename DType, typename Rand>
  void ShuffleND(DType* out, index_t size, index_t first_axis_len, Rand* prnd) {
    // Fisher-Yates shuffling
    const index_t stride = size / first_axis_len;
    auto rand_n = [prnd](index_t n) {
      std::uniform_int_distribution<index_t> dist(0, n - 1);
      return dist(*prnd);
    };
    for (index_t i = first_axis_len - 1; i > 0; --i) {
      index_t j = rand_n(i + 1);
      if (i != j) {
        std::swap_ranges(out + stride * i, out + stride * (i + 1), out + stride * j);
      }
    }
  }

}  // namespace

void ShuffleForwardCPU(const nnvm::NodeAttrs& attrs,
                       const OpContext& ctx,
                       const std::vector<TBlob>& inputs,
                       const std::vector<OpReqType>& req,
                       const std::vector<TBlob>& outputs) {
  using namespace mxnet_op;
  if (req[0] == kNullOp) {
    return;
  }
  CHECK_NE(req[0], kAddTo) << "Shuffle does not support AddTo";
  const TShape input_shape = inputs[0].shape_;
  const index_t size = inputs[0].Size();
  const index_t first_axis_len = input_shape[0];
  Stream<cpu> *s = ctx.get_stream<cpu>();
  MSHADOW_REAL_TYPE_SWITCH(inputs[0].type_flag_, DType, {
    Tensor<cpu, 1, DType> in = inputs[0].get_with_shape<cpu, 1, DType>(Shape1(size), s);
    Tensor<cpu, 1, DType> out = outputs[0].get_with_shape<cpu, 1, DType>(Shape1(size), s);
    auto& prnd = ctx.requested[0].get_random<cpu, index_t>(ctx.get_stream<cpu>())->GetRndEngine();
    if (req[0] != kWriteInplace) {
      std::copy(in.dptr_, in.dptr_ + size, out.dptr_);
    }
    if (input_shape.ndim() == 1) {
      Shuffle1D(out.dptr_, size, &prnd);
    } else {
      ShuffleND(out.dptr_, size, first_axis_len, &prnd);
    }
  });
}


// No parameter is declared.
// No backward computation is registered. Shuffling is not differentiable.

NNVM_REGISTER_OP(_shuffle)
.add_alias("shuffle")
.describe(R"code(Randomly shuffle the elements.

This shuffles the array along the first axis.
The order of the elements in each subarray does not change.
For example, if a 2D array is given, the order of the rows randomly changes,
but the order of the elements in each row does not change.
)code")
.set_num_inputs(1)
.set_num_outputs(1)
.set_attr<nnvm::FInferShape>("FInferShape", ElemwiseShape<1, 1>)
.set_attr<nnvm::FInferType>("FInferType", ElemwiseType<1, 1>)
.set_attr<FResourceRequest>("FResourceRequest",
  [](const nnvm::NodeAttrs& attrs) {
    return std::vector<ResourceRequest>{ResourceRequest::kRandom, ResourceRequest::kTempSpace};
  })
.set_attr<nnvm::FInplaceOption>("FInplaceOption",
  [](const NodeAttrs& attrs) {
    return std::vector<std::pair<int, int>>{{0, 0}};
  })
.set_attr<FCompute>("FCompute<cpu>", ShuffleForwardCPU)
.add_argument("data", "NDArray-or-Symbol", "Data to be shuffled.");

}  // namespace op
}  // namespace mxnet

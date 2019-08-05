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
 * Copyright (c) 2019 by Contributors
 * \file np_uniform_op.h
 * \brief Operator for numpy sampling from uniform distributions
 */
#include "./np_uniform_op.h"

namespace mxnet {
namespace op {

DMLC_REGISTER_PARAMETER(NumpyUniformParam);

NNVM_REGISTER_OP(_npi_uniform)
.describe("numpy behavior uniform")
.set_num_inputs(
  [](const nnvm::NodeAttrs& attrs) {
    const NumpyUniformParam& param = nnvm::get<NumpyUniformParam>(attrs.parsed);
    int type_flag = param.t;
    switch (type_flag) {
    case (0):
      return 2;
    case (1):
      return 1;
    case (2):
      return 1;
    case (3):
      return 0;
    }
    return -1;
  }
)
.set_num_outputs(1)
.set_attr<nnvm::FListInputNames>("FListInputNames",
  [](const NodeAttrs& attrs) {
    return std::vector<std::string>{"input1", "input2"};
  })
.set_attr_parser(ParamParser<NumpyUniformParam>)
.set_attr<mxnet::FInferShape>("FInferShape", NumpyUniformOpShape)
.set_attr<nnvm::FInferType>("FInferType", NumpyUniformOpType)
.set_attr<FResourceRequest>("FResourceRequest",
  [](const nnvm::NodeAttrs& attrs) {
      return std::vector<ResourceRequest>{
        ResourceRequest::kRandom, ResourceRequest::kTempSpace};
  })
.set_attr<FCompute>("FCompute<cpu>", NumpyUniformForward<cpu>)
.set_attr<nnvm::FGradient>("FGradient", MakeZeroGradNodes)
.add_argument("input1", "NDArray-or-Symbol", "Source input")
.add_argument("input2", "NDArray-or-Symbol", "Source input")
.add_arguments(NumpyUniformParam::__FIELDS__());

}  // namespace op
}  // namespace mxnet

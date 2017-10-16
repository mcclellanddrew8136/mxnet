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
 * \file elemwise_binary_scalar_op.cc
 * \brief CPU Implementation of unary function.
 */
#include "../../common/utils.h"
#include "./elemwise_binary_op.h"
#include "./elemwise_binary_scalar_op.h"

#define MXNET_OPERATOR_REGISTER_BINARY_WITH_SCALAR_SUPPORT_WITH_DENSE_RESULT(name)    \
  NNVM_REGISTER_OP(name)                                            \
  .set_num_inputs(1)                                                \
  .set_num_outputs(1)                                               \
  .set_attr_parser([](NodeAttrs* attrs) {                           \
      attrs->parsed = std::stod(attrs->dict["scalar"]);             \
    })                                                              \
  .set_attr<nnvm::FInferShape>("FInferShape", ElemwiseShape<1, 1>)  \
  .set_attr<nnvm::FInferType>("FInferType", ElemwiseType<1, 1>)     \
  .set_attr<FInferStorageType>("FInferStorageType",                 \
    BinaryScalarStorageTypeWithDenseResultStorageType)              \
  .set_attr<nnvm::FInplaceOption>("FInplaceOption",                 \
    [](const NodeAttrs& attrs){                                     \
      return std::vector<std::pair<int, int> >{{0, 0}};             \
    })                                                              \
  .add_argument("data", "NDArray-or-Symbol", "source input")        \
  .add_argument("scalar", "float", "scalar input")

namespace mxnet {
namespace op {

static bool BinaryScalarStorageTypeWithDenseResultStorageType(const NodeAttrs& attrs,
                                                              const int dev_mask,
                                                              DispatchMode* dispatch_mode,
                                                              std::vector<int>* in_attrs,
                                                              std::vector<int>* out_attrs)  {
  bool dispatched = false;
  if (common::ContainsOnlyStorage(*in_attrs, kDefaultStorage)) {
    dispatched = storage_type_assign(&out_attrs[0],
                                     kDefaultStorage,
                                     dispatch_mode,
                                     DispatchMode::kFCompute);
  } else if (dev_mask == kCPU) {
    dispatched = storage_type_assign(&out_attrs[0],
                                     kDefaultStorage,
                                     dispatch_mode,
                                     DispatchMode::kFComputeEx);
  }
  if (!dispatched) {
    dispatch_fallback(out_attrs, dispatch_mode);
    LogStorageFallback(attrs, dev_mask, in_attrs, out_attrs);
  }
  return true;
}

static bool BinaryScalarStorageType(const nnvm::NodeAttrs& attrs,
                                    const int dev_mask,
                                    DispatchMode* dispatch_mode,
                                    std::vector<int> *in_attrs,
                                    std::vector<int> *out_attrs) {
  CHECK_EQ(in_attrs->size(), 1);
  CHECK_EQ(out_attrs->size(), 1);
  const auto in_stype = in_attrs->at(0);
  auto &out_stype = out_attrs->at(0);
  bool dispatched = false;
  if (!dispatched && in_stype == kDefaultStorage) {
    // dns -> dns
    dispatched = storage_type_assign(&out_stype, kDefaultStorage,
                                     dispatch_mode, DispatchMode::kFCompute);
  }
  if (!dispatched && in_stype == kRowSparseStorage) {
    // rsp -> rsp
    dispatched = storage_type_assign(&out_stype, kRowSparseStorage,
                                     dispatch_mode, DispatchMode::kFComputeEx);
    // FComputeEx can handle dns output on cpu, too
    if (dev_mask == cpu::kDevMask && out_stype == kDefaultStorage) {
      DISPATCH_MODE_ASSIGN_CHECK(dispatch_mode, 0, DispatchMode::kFComputeEx);
      dispatched = true;
    }
  }
  if (!dispatched && in_stype == kCSRStorage) {
    // csr -> csr
    dispatched = storage_type_assign(&out_stype, kCSRStorage,
                                     dispatch_mode, DispatchMode::kFComputeEx);
    // FComputeEx can handle dns output on cpu, too
    if (dev_mask == cpu::kDevMask && out_stype == kDefaultStorage) {
      DISPATCH_MODE_ASSIGN_CHECK(dispatch_mode, 0, DispatchMode::kFComputeEx);
      dispatched = true;
    }
  }
  if (!dispatched) {
    dispatch_fallback(out_attrs, dispatch_mode);
    LogStorageFallback(attrs, dev_mask, in_attrs, out_attrs);
  }
  return true;
}

MXNET_OPERATOR_REGISTER_BINARY_WITH_SCALAR_SUPPORT_WITH_DENSE_RESULT(_plus_scalar)
.set_attr<FCompute>("FCompute<cpu>", BinaryScalarOp::Compute<cpu, mshadow::op::plus>)
.set_attr<FComputeEx>("FComputeEx<cpu>", BinaryScalarOp::ComputeEx<cpu, mshadow::op::plus>)
.set_attr<nnvm::FGradient>("FGradient", ElemwiseGradUseNone{"_copy"})
.add_alias("_PlusScalar");

MXNET_OPERATOR_REGISTER_BINARY_WITH_SCALAR_SUPPORT_WITH_DENSE_RESULT(_minus_scalar)
.set_attr<FCompute>("FCompute<cpu>", BinaryScalarOp::Compute<cpu, mshadow::op::minus>)
.set_attr<FComputeEx>("FComputeEx<cpu>", BinaryScalarOp::ComputeEx<cpu, mshadow::op::minus>)
.set_attr<nnvm::FGradient>("FGradient", ElemwiseGradUseNone{"_copy"})
.add_alias("_MinusScalar");

MXNET_OPERATOR_REGISTER_BINARY_SCALAR(_rminus_scalar)
.set_attr<FCompute>("FCompute<cpu>", BinaryScalarOp::Compute<cpu, mshadow_op::rminus>)
.set_attr<nnvm::FGradient>("FGradient", ElemwiseGradUseNone{"negative"})
.add_alias("_RMinusScalar");

MXNET_OPERATOR_REGISTER_BINARY_SCALAR(_mul_scalar)
.set_attr<FInferStorageType>("FInferStorageType", BinaryScalarStorageType)
.set_attr<FCompute>("FCompute<cpu>", BinaryScalarOp::Compute<cpu, mshadow::op::mul>)
.set_attr<FComputeEx>("FComputeEx<cpu>", BinaryScalarOp::ComputeEx<cpu, mshadow::op::mul>)
.set_attr<nnvm::FGradient>("FGradient", ElemwiseGradUseNone{"_backward_mul_scalar"})
.add_alias("_MulScalar");

MXNET_OPERATOR_REGISTER_BINARY_SCALAR(_backward_mul_scalar)
.set_attr<nnvm::TIsBackward>("TIsBackward", true)
.set_attr<FInferStorageType>("FInferStorageType", BinaryScalarStorageType)
.set_attr<FCompute>("FCompute<cpu>", BinaryScalarOp::Compute<cpu, mshadow::op::mul>)
.set_attr<FComputeEx>("FComputeEx<cpu>", BinaryScalarOp::ComputeEx<cpu, mshadow::op::mul>);

MXNET_OPERATOR_REGISTER_BINARY_SCALAR(_div_scalar)
.set_attr<FCompute>("FCompute<cpu>", BinaryScalarOp::Compute<cpu, mshadow::op::div>)
.set_attr<nnvm::FGradient>("FGradient", ElemwiseGradUseNone{"_div_scalar"})
.add_alias("_DivScalar");

MXNET_OPERATOR_REGISTER_BINARY_SCALAR(_rdiv_scalar)
.set_attr<FCompute>("FCompute<cpu>", BinaryScalarOp::Compute<cpu, mshadow_op::rdiv>)
.set_attr<nnvm::FGradient>("FGradient", ElemwiseGradUseIn{"_backward_rdiv_scalar"})
.add_alias("_RDivScalar");

MXNET_OPERATOR_REGISTER_BINARY(_backward_rdiv_scalar)
.add_argument("scalar", "float", "scalar value")
.set_attr_parser([](NodeAttrs *attrs) { attrs->parsed = std::stod(attrs->dict["scalar"]); })
.set_attr<FCompute>("FCompute<cpu>", BinaryScalarOp::Backward<
  cpu, mshadow_op::rdiv_grad>);

MXNET_OPERATOR_REGISTER_BINARY_SCALAR(_mod_scalar)
.set_attr<FCompute>("FCompute<cpu>", BinaryScalarOp::Compute<cpu, mshadow_op::mod>)
.set_attr<nnvm::FGradient>("FGradient", ElemwiseGradUseIn{"_backward_mod_scalar"})
.add_alias("_ModScalar");

MXNET_OPERATOR_REGISTER_BINARY(_backward_mod_scalar)
.add_argument("scalar", "float", "scalar value")
.set_attr_parser([](NodeAttrs *attrs) { attrs->parsed = std::stod(attrs->dict["scalar"]); })
.set_attr<FCompute>("FCompute<cpu>", BinaryScalarOp::Backward<
  cpu, mshadow_op::mod_grad>);

MXNET_OPERATOR_REGISTER_BINARY_SCALAR(_rmod_scalar)
.set_attr<FCompute>("FCompute<cpu>", BinaryScalarOp::Compute<cpu, mshadow_op::rmod>)
.set_attr<nnvm::FGradient>("FGradient", ElemwiseGradUseIn{"_backward_rmod_scalar"})
.add_alias("_RModScalar");

MXNET_OPERATOR_REGISTER_BINARY(_backward_rmod_scalar)
.add_argument("scalar", "float", "scalar value")
.set_attr_parser([](NodeAttrs *attrs) { attrs->parsed = std::stod(attrs->dict["scalar"]); })
.set_attr<FCompute>("FCompute<cpu>", BinaryScalarOp::Backward<
  cpu, mshadow_op::rmod_grad>);

}  // namespace op
}  // namespace mxnet

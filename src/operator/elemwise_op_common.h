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
* \file elemwise_op_common.h
* \brief common function used for broadcasting and reducing
* \author Xingjian Shi
*/
#ifndef MXNET_OPERATOR_ELEMWISE_OP_COMMON_H_
#define MXNET_OPERATOR_ELEMWISE_OP_COMMON_H_
#include <dmlc/logging.h>
#include <mxnet/operator.h>
#include <mxnet/operator_util.h>
#include <mxnet/op_attr_types.h>
#include <nnvm/op.h>
#include <nnvm/node.h>
#include <nnvm/op_attr_types.h>
#include <vector>
#include <string>
#include <utility>
#include "./operator_common.h"
#include "./mxnet_op.h"

namespace mxnet {
namespace op {

// TODO add doc
template<bool rsp = false, bool csr = false>
inline bool ElemwiseStorageAttr(const nnvm::NodeAttrs& attrs,
                                const Context& ctx,
                                int* dispatch_type,
                                std::vector<int> *in_attrs,
                                std::vector<int> *out_attrs) {
  // TODO(junwu): add ctx info into storage inference logic
  bool dispatched = false;
  if (common::ContainsOnlyStorage(*in_attrs, kDefaultStorage)) {
    // dns, dns ... -> dns
    dispatched = dispatch_on_storage(out_attrs, kDefaultStorage,
                                     dispatch_type, kDispatchFCompute);
  } else if (rsp && common::ContainsOnlyStorage(*in_attrs, kRowSparseStorage)) {
    // rsp, rsp, ... -> rsp
    dispatched = dispatch_on_storage(out_attrs, kRowSparseStorage,
                                     dispatch_type, kDispatchFComputeEx);
  } else if (csr && common::ContainsOnlyStorage(*in_attrs, kCSRStorage)) {
    // csr, csr, ... -> csr
    dispatched = dispatch_on_storage(out_attrs, kCSRStorage,
                                     dispatch_type, kDispatchFComputeEx);
  }
  if (!dispatched) {
    dispatch_on_storage(out_attrs, kDefaultStorage,
                        dispatch_type, kDispatchFComputeFallback);
    LogStorageFallback(attrs, ctx, in_attrs, out_attrs);
  }
  return true;
}

template<int n_in, int n_out, bool rsp = false, bool csr = false>
inline bool ElemwiseStorageType(const nnvm::NodeAttrs& attrs,
                                const Context& ctx,
                                int* dispatch_type,
                                std::vector<int> *in_attrs,
                                std::vector<int> *out_attrs) {
  CHECK_EQ(in_attrs->size(), n_in);
  CHECK_EQ(out_attrs->size(), n_out);
  return ElemwiseStorageAttr<rsp, csr>(attrs, ctx, dispatch_type, in_attrs, out_attrs);
}

template<typename AttrType, bool (*is_none)(const AttrType&),
         bool (*assign)(AttrType*, const AttrType&), bool reverse_infer,
         std::string (*attr_string)(const AttrType&),
         int n_in = -1, int n_out = -1>
inline bool ElemwiseAttr(const nnvm::NodeAttrs& attrs,
                         std::vector<AttrType> *in_attrs,
                         std::vector<AttrType> *out_attrs,
                         const AttrType& none) {
  AttrType dattr = none;
  size_t in_size = in_attrs->size();
  size_t out_size = out_attrs->size();
  if (n_in != -1)
    in_size = static_cast<size_t>(n_in);
  if (n_out != -1)
    out_size = static_cast<size_t>(n_out);

  auto deduce = [&](std::vector<AttrType> *vec, size_t size, const char *name) {
      for (size_t i = 0; i < size; ++i) {
        CHECK(assign(&dattr, (*vec)[i]))
          << "Incompatible attr in node " << attrs.name << " at " << i << "-th "
          << name << ": " << "expected " << attr_string(dattr)
          << ", got " << attr_string((*vec)[i]);
      }
    };
  deduce(in_attrs, in_size, "input");
  if (reverse_infer) deduce(out_attrs, out_size, "output");

  auto write = [&](std::vector<AttrType> *vec, size_t size, const char *name) {
      for (size_t i = 0; i < size; ++i) {
        CHECK(assign(&(*vec)[i], dattr))
          << "Incompatible attr in node " << attrs.name << " at " << i << "-th "
          << name << ": " << "expected " << attr_string(dattr)
          << ", got " << attr_string((*vec)[i]);
      }
    };
  write(in_attrs, in_size, "input");
  write(out_attrs, out_size, "output");

  if (is_none(dattr)) return false;
  return true;
}

template<int n_in, int n_out>
inline bool ElemwiseShape(const nnvm::NodeAttrs& attrs,
                          std::vector<TShape> *in_attrs,
                          std::vector<TShape> *out_attrs) {
  if (n_in != -1) {
    CHECK_EQ(in_attrs->size(), static_cast<size_t>(n_in)) << " in operator " << attrs.name;
  }
  if (n_out != -1) {
    CHECK_EQ(out_attrs->size(), static_cast<size_t>(n_out)) << " in operator " << attrs.name;
  }
  return ElemwiseAttr<TShape, shape_is_none, shape_assign, true, shape_string>(
    attrs, in_attrs, out_attrs, TShape());
}

template<int n_in, int n_out>
inline bool ElemwiseType(const nnvm::NodeAttrs& attrs,
                         std::vector<int> *in_attrs,
                         std::vector<int> *out_attrs) {
  if (n_in != -1) {
    CHECK_EQ(in_attrs->size(), static_cast<size_t>(n_in)) << " in operator " << attrs.name;
  }
  if (n_out != -1) {
    CHECK_EQ(out_attrs->size(), static_cast<size_t>(n_out)) << " in operator " << attrs.name;
  }
  return ElemwiseAttr<int, type_is_none, type_assign, true, type_string>(
    attrs, in_attrs, out_attrs, -1);
}

// The output is always dense. But the input may not be.
template<int n_out, bool rsp, bool csr>
inline bool ElemwiseStorageTypeDnsOutput(const nnvm::NodeAttrs& attrs,
                                           const Context& ctx,
                                           int* dispatch_type,
                                           std::vector<int> *in_attrs,
                                           std::vector<int> *out_attrs) {
  CHECK_EQ(out_attrs->size(), static_cast<size_t>(n_out)) << " in operator " << attrs.name;
  // TODO(junwu): add ctx info into storage inference logic
  bool dispatched = false;
  if (common::ContainsOnlyStorage(*in_attrs, kDefaultStorage)) {
    // dns, dns ... -> dns
    dispatched = dispatch_on_storage(out_attrs, kDefaultStorage,
                                     dispatch_type, kDispatchFCompute);
  } else if (rsp && common::ContainsOnlyStorage(*in_attrs, kRowSparseStorage)) {
    // rsp, rsp, ... -> dns
    dispatched = dispatch_on_storage(out_attrs, kDefaultStorage,
                                     dispatch_type, kDispatchFComputeEx);
  } else if (csr && common::ContainsOnlyStorage(*in_attrs, kCSRStorage)) {
    // csr, csr, ... -> dns
    dispatched = dispatch_on_storage(out_attrs, kDefaultStorage,
                                     dispatch_type, kDispatchFComputeEx);
  }
  if (!dispatched) {
    dispatch_on_storage(out_attrs, kDefaultStorage,
                        dispatch_type, kDispatchFComputeFallback);
    LogStorageFallback(attrs, ctx, in_attrs, out_attrs);
  }
  return true;
}

// Transfer gradient and input to FGradient function
struct ElemwiseGradUseIn {
  const char *op_name;
  std::vector<nnvm::NodeEntry> operator()(const nnvm::NodePtr& n,
                                          const std::vector<nnvm::NodeEntry>& ograds) const {
    return MakeNonlossGradNode(op_name, n, ograds, n->inputs, n->attrs.dict);
  }
};

// Transfer gradient and output to FGradient function
struct ElemwiseGradUseOut {
  const char *op_name;
  std::vector<nnvm::NodeEntry> operator()(const nnvm::NodePtr& n,
                                          const std::vector<nnvm::NodeEntry>& ograds) const {
    std::vector<nnvm::NodeEntry> heads;
    index_t n_out = n->num_outputs();
    for (index_t i = 0; i < n_out; ++i) {
      heads.emplace_back(nnvm::NodeEntry{n, i, 0});
    }
    return MakeNonlossGradNode(op_name, n, ograds, heads, n->attrs.dict);
  }
};

// Transfer gradient and input and output to FGradient function
struct ElemwiseGradUseInOut {
  const char *op_name;
  std::vector<nnvm::NodeEntry> operator()(const nnvm::NodePtr& n,
                                          const std::vector<nnvm::NodeEntry>& ograds) const {
    std::vector<nnvm::NodeEntry> heads(ograds.begin(), ograds.end());
    for (auto& h : n->inputs) {
      heads.push_back(h);
    }
    index_t n_out = n->num_outputs();
    for (index_t i = 0; i < n_out; ++i) {
      heads.emplace_back(nnvm::NodeEntry{n, i, 0});
    }
    return MakeGradNode(op_name, n, heads, n->attrs.dict);
  }
};

// Transfer only gradient to FGradient function
struct ElemwiseGradUseNone {
  const char *op_name;
  std::vector<nnvm::NodeEntry> operator()(const nnvm::NodePtr& n,
                                          const std::vector<nnvm::NodeEntry>& ograds) const {
    return MakeNonlossGradNode(op_name, n, ograds, {}, n->attrs.dict);
  }
};

struct CloneGradient {
  const char *op_name;
  std::vector<nnvm::NodeEntry> operator()(const nnvm::NodePtr& n,
                                          const std::vector<nnvm::NodeEntry>& ograds) const {
    std::vector<nnvm::NodeEntry> ret;
    const size_t input_count = n->inputs.size();
    ret.reserve(input_count);
    for (size_t i = 0; i < input_count; ++i)
      ret.emplace_back(ograds[0]);
    return ret;
  }
};

}  // namespace op
}  // namespace mxnet

#endif  // MXNET_OPERATOR_ELEMWISE_OP_COMMON_H_

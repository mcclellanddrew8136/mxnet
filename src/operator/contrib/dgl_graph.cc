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

#include <mxnet/io.h>
#include <mxnet/base.h>
#include <mxnet/ndarray.h>
#include <mxnet/operator.h>
#include <mxnet/operator_util.h>
#include <dmlc/logging.h>
#include <dmlc/optional.h>
#include "../operator_common.h"
#include "../elemwise_op_common.h"
#include "../../imperative/imperative_utils.h"
#include "../subgraph_op_common.h"
#include "../mshadow_op.h"
#include "../mxnet_op.h"
#include "../tensor/init_op.h"

namespace mxnet {
namespace op {


///////////////////////// Create induced subgraph ///////////////////////////

struct DGLSubgraphParam : public dmlc::Parameter<DGLSubgraphParam> {
  int num_args;
  bool return_mapping;
  DMLC_DECLARE_PARAMETER(DGLSubgraphParam) {
    DMLC_DECLARE_FIELD(num_args).set_lower_bound(2)
    .describe("Number of input arguments, including all symbol inputs.");
    DMLC_DECLARE_FIELD(return_mapping)
    .describe("Return mapping of vid and eid between the subgraph and the parent graph.");
  }
};  // struct DGLSubgraphParam

DMLC_REGISTER_PARAMETER(DGLSubgraphParam);

static bool DGLSubgraphStorageType(const nnvm::NodeAttrs& attrs,
                                   const int dev_mask,
                                   DispatchMode* dispatch_mode,
                                   std::vector<int> *in_attrs,
                                   std::vector<int> *out_attrs) {
  CHECK_EQ(in_attrs->at(0), kCSRStorage);
  for (size_t i = 1; i < in_attrs->size(); i++)
    CHECK_EQ(in_attrs->at(i), kDefaultStorage);

  bool success = true;
  *dispatch_mode = DispatchMode::kFComputeEx;
  for (size_t i = 0; i < out_attrs->size(); i++) {
    if (!type_assign(&(*out_attrs)[i], mxnet::kCSRStorage))
    success = false;
  }
  return success;
}

static bool DGLSubgraphShape(const nnvm::NodeAttrs& attrs,
                             std::vector<TShape> *in_attrs,
                             std::vector<TShape> *out_attrs) {
  const DGLSubgraphParam& params = nnvm::get<DGLSubgraphParam>(attrs.parsed);
  CHECK_EQ(in_attrs->at(0).ndim(), 2U);
  for (size_t i = 1; i < in_attrs->size(); i++)
    CHECK_EQ(in_attrs->at(i).ndim(), 1U);

  size_t num_g = params.num_args - 1;
  for (size_t i = 0; i < num_g; i++) {
    TShape gshape(2);
    gshape[0] = in_attrs->at(i + 1)[0];
    gshape[1] = in_attrs->at(i + 1)[0];
    out_attrs->at(i) = gshape;
  }
  for (size_t i = num_g; i < out_attrs->size(); i++) {
    TShape gshape(2);
    gshape[0] = in_attrs->at(i - num_g + 1)[0];
    gshape[1] = in_attrs->at(i - num_g + 1)[0];
    out_attrs->at(i) = gshape;
  }
  return true;
}

static bool DGLSubgraphType(const nnvm::NodeAttrs& attrs,
                            std::vector<int> *in_attrs,
                            std::vector<int> *out_attrs) {
  const DGLSubgraphParam& params = nnvm::get<DGLSubgraphParam>(attrs.parsed);
  size_t num_g = params.num_args - 1;
  for (size_t i = 0; i < num_g; i++) {
    CHECK_EQ(in_attrs->at(i + 1), mshadow::kInt64);
  }
  for (size_t i = 0; i < out_attrs->size(); i++) {
    out_attrs->at(i) = in_attrs->at(0);
  }
  return true;
}

typedef int64_t dgl_id_t;

class Bitmap {
  const size_t size = 1024 * 1024 * 4;
  const size_t mask = size - 1;
  std::vector<bool> map;

  size_t hash(dgl_id_t id) const {
    return id & mask;
  }
 public:
  Bitmap(const dgl_id_t *vid_data, int64_t len): map(size) {
    for (int64_t i = 0; i < len; ++i) {
      map[hash(vid_data[i])] = 1;
    }
  }

  bool test(dgl_id_t id) const {
    return map[hash(id)];
  }
};

/*
 * This uses a hashtable to check if a node is in the given node list.
 */
class HashTableChecker {
  std::unordered_map<dgl_id_t, dgl_id_t> oldv2newv;
  Bitmap map;
 public:
  HashTableChecker(const dgl_id_t *vid_data, int64_t len): map(vid_data, len) {
    oldv2newv.reserve(len);
    for (int64_t i = 0; i < len; ++i) {
      oldv2newv[vid_data[i]] = i;
    }
  }

  void CollectOnRow(const dgl_id_t col_idx[], const dgl_id_t eids[], size_t row_len,
                    std::vector<dgl_id_t> *new_col_idx,
                    std::vector<dgl_id_t> *orig_eids) {
    // TODO(zhengda) I need to make sure the column index in each row is sorted.
    for (size_t j = 0; j < row_len; ++j) {
      const dgl_id_t oldsucc = col_idx[j];
      const dgl_id_t eid = eids[j];
      Collect(oldsucc, eid, new_col_idx, orig_eids);
    }
  }

  void Collect(const dgl_id_t old_id, const dgl_id_t old_eid,
               std::vector<dgl_id_t> *col_idx,
               std::vector<dgl_id_t> *orig_eids) {
    if (!map.test(old_id))
      return;

    auto it = oldv2newv.find(old_id);
    if (it != oldv2newv.end()) {
      const dgl_id_t new_id = it->second;
      col_idx->push_back(new_id);
      if (orig_eids)
        orig_eids->push_back(old_eid);
    }
  }
};

static void GetSubgraph(const NDArray &csr_arr, const NDArray &varr,
                        const NDArray &sub_csr, const NDArray *old_eids) {
  TBlob data = varr.data();
  int64_t num_vertices = csr_arr.shape()[0];
  const size_t len = varr.shape()[0];
  const dgl_id_t *vid_data = data.dptr<dgl_id_t>();
  HashTableChecker def_check(vid_data, len);

  // Collect the non-zero entries in from the original graph.
  std::vector<dgl_id_t> row_idx(len + 1);
  std::vector<dgl_id_t> col_idx;
  std::vector<dgl_id_t> orig_eids;
  col_idx.reserve(len * 50);
  orig_eids.reserve(len * 50);
  const dgl_id_t *eids = csr_arr.data().dptr<dgl_id_t>();
  const dgl_id_t *indptr = csr_arr.aux_data(csr::kIndPtr).dptr<dgl_id_t>();
  const dgl_id_t *indices = csr_arr.aux_data(csr::kIdx).dptr<dgl_id_t>();
  for (size_t i = 0; i < len; ++i) {
    const dgl_id_t oldvid = vid_data[i];
    CHECK_LT(oldvid, num_vertices) << "Vertex Id " << oldvid << " isn't in a graph of "
        << num_vertices << " vertices";
    size_t row_start = indptr[oldvid];
    size_t row_len = indptr[oldvid + 1] - indptr[oldvid];
    def_check.CollectOnRow(indices + row_start, eids + row_start, row_len,
                           &col_idx, old_eids == nullptr ? nullptr : &orig_eids);

    row_idx[i + 1] = col_idx.size();
  }

  TShape nz_shape(1);
  nz_shape[0] = col_idx.size();
  TShape indptr_shape(1);
  indptr_shape[0] = row_idx.size();

  // Store the non-zeros in a subgraph with edge attributes of new edge ids.
  sub_csr.CheckAndAllocData(nz_shape);
  sub_csr.CheckAndAllocAuxData(csr::kIdx, nz_shape);
  sub_csr.CheckAndAllocAuxData(csr::kIndPtr, indptr_shape);
  dgl_id_t *indices_out = sub_csr.aux_data(csr::kIdx).dptr<dgl_id_t>();
  dgl_id_t *indptr_out = sub_csr.aux_data(csr::kIndPtr).dptr<dgl_id_t>();
  std::copy(col_idx.begin(), col_idx.end(), indices_out);
  std::copy(row_idx.begin(), row_idx.end(), indptr_out);
  dgl_id_t *sub_eids = sub_csr.data().dptr<dgl_id_t>();
  for (int64_t i = 0; i < nz_shape[0]; i++)
    sub_eids[i] = i;

  // Store the non-zeros in a subgraph with edge attributes of old edge ids.
  if (old_eids) {
    old_eids->CheckAndAllocData(nz_shape);
    old_eids->CheckAndAllocAuxData(csr::kIdx, nz_shape);
    old_eids->CheckAndAllocAuxData(csr::kIndPtr, indptr_shape);
    dgl_id_t *indices_out = old_eids->aux_data(csr::kIdx).dptr<dgl_id_t>();
    dgl_id_t *indptr_out = old_eids->aux_data(csr::kIndPtr).dptr<dgl_id_t>();
    dgl_id_t *sub_eids = old_eids->data().dptr<dgl_id_t>();
    std::copy(col_idx.begin(), col_idx.end(), indices_out);
    std::copy(row_idx.begin(), row_idx.end(), indptr_out);
    std::copy(orig_eids.begin(), orig_eids.end(), sub_eids);
  }
}

static void DGLSubgraphComputeExCPU(const nnvm::NodeAttrs& attrs,
                                    const OpContext& ctx,
                                    const std::vector<NDArray>& inputs,
                                    const std::vector<OpReqType>& req,
                                    const std::vector<NDArray>& outputs) {
  const DGLSubgraphParam& params = nnvm::get<DGLSubgraphParam>(attrs.parsed);
  size_t num_g = params.num_args - 1;
#pragma omp parallel for
  for (size_t i = 0; i < num_g; i++) {
    const NDArray *old_eids = params.return_mapping ? &outputs[i + num_g] : nullptr ;
    GetSubgraph(inputs[0], inputs[i + 1], outputs[i], old_eids);
  }
}

NNVM_REGISTER_OP(_contrib_dgl_subgraph)
.MXNET_DESCRIBE("")
.set_attr_parser(ParamParser<DGLSubgraphParam>)
.set_num_inputs([](const NodeAttrs& attrs) {
  const DGLSubgraphParam& params = nnvm::get<DGLSubgraphParam>(attrs.parsed);
  return params.num_args;
})
.set_num_outputs([](const NodeAttrs& attrs) {
  const DGLSubgraphParam& params = nnvm::get<DGLSubgraphParam>(attrs.parsed);
  int num_varray = params.num_args - 1;
  if (params.return_mapping)
    return num_varray * 2;
  else
    return num_varray;
})
.set_attr<nnvm::FListInputNames>("FListInputNames",
    [](const NodeAttrs& attrs) {
  const DGLSubgraphParam& params = nnvm::get<DGLSubgraphParam>(attrs.parsed);
  std::vector<std::string> names;
  names.reserve(params.num_args);
  names.push_back("graph");
  for (int i = 1; i < params.num_args; ++i)
    names.push_back("varray" + std::to_string(i - 1));
  return names;
})
.set_attr<FInferStorageType>("FInferStorageType", DGLSubgraphStorageType)
.set_attr<nnvm::FInferShape>("FInferShape", DGLSubgraphShape)
.set_attr<nnvm::FInferType>("FInferType", DGLSubgraphType)
.set_attr<FComputeEx>("FComputeEx<cpu>", DGLSubgraphComputeExCPU)
.set_attr<std::string>("key_var_num_args", "num_args")
.add_argument("graph", "NDArray-or-Symbol", "Input graph where we sample vertices.")
.add_argument("data", "NDArray-or-Symbol[]",
              "The input arrays that include data arrays and states.")
.add_arguments(DGLSubgraphParam::__FIELDS__());

///////////////////////// Compact subgraphs ///////////////////////////

struct SubgraphCompactParam : public dmlc::Parameter<SubgraphCompactParam> {
  int num_args;
  bool return_mapping;
  nnvm::Tuple<nnvm::dim_t> graph_sizes;
  DMLC_DECLARE_PARAMETER(SubgraphCompactParam) {
    DMLC_DECLARE_FIELD(num_args).set_lower_bound(2)
    .describe("Number of input arguments, including all symbol inputs.");
    DMLC_DECLARE_FIELD(return_mapping)
    .describe("Return mapping of vid and eid between the subgraph and the parent graph.");
    DMLC_DECLARE_FIELD(graph_sizes)
    .describe("the number of vertices in each graph.");
  }
};  // struct SubgraphCompactParam

DMLC_REGISTER_PARAMETER(SubgraphCompactParam);

static inline size_t get_num_graphs(const SubgraphCompactParam &params) {
  // Each CSR needs a 1D array to store the original vertex Id for each row.
  return params.num_args / 2;
}

static void CompactSubgraph(const NDArray &csr, const NDArray &vids,
                            const NDArray &out_csr) {
  TBlob in_idx_data = csr.aux_data(csr::kIdx);
  TBlob in_ptr_data = csr.aux_data(csr::kIndPtr);
  const dgl_id_t *indices_in = in_idx_data.dptr<dgl_id_t>();
  const dgl_id_t *indptr_in = in_ptr_data.dptr<dgl_id_t>();
  const dgl_id_t *row_ids = vids.data().dptr<dgl_id_t>();
  size_t num_elems = csr.aux_data(csr::kIdx).shape_.Size();
  size_t num_vids = vids.shape()[0];
  CHECK_EQ(num_vids, in_ptr_data.shape_[0] - 1);

  // Prepare the Id map from the original graph to the subgraph.
  std::unordered_map<dgl_id_t, dgl_id_t> id_map;
  id_map.reserve(vids.shape()[0]);
  for (size_t i = 0; i < num_vids; i++)
    id_map.insert(std::pair<dgl_id_t, dgl_id_t>(row_ids[i], i));

  TShape nz_shape(1);
  nz_shape[0] = num_elems;
  TShape indptr_shape(1);
  indptr_shape[0] = out_csr.aux_data(csr::kIndPtr).shape_.Size();
  CHECK_GE(in_ptr_data.shape_[0], indptr_shape[0]);

  out_csr.CheckAndAllocData(nz_shape);
  out_csr.CheckAndAllocAuxData(csr::kIdx, nz_shape);
  out_csr.CheckAndAllocAuxData(csr::kIndPtr, indptr_shape);

  dgl_id_t *indices_out = out_csr.aux_data(csr::kIdx).dptr<dgl_id_t>();
  dgl_id_t *indptr_out = out_csr.aux_data(csr::kIndPtr).dptr<dgl_id_t>();
  dgl_id_t *sub_eids = out_csr.data().dptr<dgl_id_t>();
  std::copy(indptr_in, indptr_in + indptr_shape[0], indptr_out);
  for (int64_t i = 0; i < nz_shape[0]; i++) {
    dgl_id_t old_id = indices_in[i];
    auto it = id_map.find(old_id);
    CHECK(it != id_map.end());
    indices_out[i] = it->second;
    sub_eids[i] = i;
  }
}

static void SubgraphCompactComputeExCPU(const nnvm::NodeAttrs& attrs,
                                        const OpContext& ctx,
                                        const std::vector<NDArray>& inputs,
                                        const std::vector<OpReqType>& req,
                                        const std::vector<NDArray>& outputs) {
  const SubgraphCompactParam& params = nnvm::get<SubgraphCompactParam>(attrs.parsed);
  size_t num_g = get_num_graphs(params);
#pragma omp parallel for
  for (size_t i = 0; i < num_g; i++) {
    CompactSubgraph(inputs[0], inputs[i + num_g], outputs[i]);
  }
}

static bool SubgraphCompactStorageType(const nnvm::NodeAttrs& attrs,
                                       const int dev_mask,
                                       DispatchMode* dispatch_mode,
                                       std::vector<int> *in_attrs,
                                       std::vector<int> *out_attrs) {
  const SubgraphCompactParam& params = nnvm::get<SubgraphCompactParam>(attrs.parsed);
  size_t num_g = get_num_graphs(params);
  CHECK_EQ(num_g * 2, in_attrs->size());
  // These are the input subgraphs.
  for (size_t i = 0; i < num_g; i++)
    CHECK_EQ(in_attrs->at(i), kCSRStorage);
  // These are the vertex Ids in the original graph.
  for (size_t i = 0; i < num_g; i++)
    CHECK_EQ(in_attrs->at(i + num_g), kDefaultStorage);

  bool success = true;
  *dispatch_mode = DispatchMode::kFComputeEx;
  for (size_t i = 0; i < out_attrs->size(); i++) {
    if (!type_assign(&(*out_attrs)[i], mxnet::kCSRStorage))
      success = false;
  }
  return success;
}

static bool SubgraphCompactShape(const nnvm::NodeAttrs& attrs,
                                 std::vector<TShape> *in_attrs,
                                 std::vector<TShape> *out_attrs) {
  const SubgraphCompactParam& params = nnvm::get<SubgraphCompactParam>(attrs.parsed);
  size_t num_g = get_num_graphs(params);
  CHECK_EQ(num_g * 2, in_attrs->size());
  // These are the input subgraphs.
  for (size_t i = 0; i < num_g; i++) {
    CHECK_EQ(in_attrs->at(i).ndim(), 2U);
    CHECK_GE(in_attrs->at(i)[0], params.graph_sizes[i]);
    CHECK_GE(in_attrs->at(i)[1], params.graph_sizes[i]);
  }
  // These are the vertex Ids in the original graph.
  for (size_t i = 0; i < num_g; i++) {
    CHECK_EQ(in_attrs->at(i + num_g).ndim(), 1U);
    CHECK_GE(in_attrs->at(i)[0], params.graph_sizes[i]);
  }

  for (size_t i = 0; i < num_g; i++) {
    TShape gshape(2);
    gshape[0] = params.graph_sizes[i];
    gshape[1] = params.graph_sizes[i];
    out_attrs->at(i) = gshape;
    if (params.return_mapping)
      out_attrs->at(i + num_g) = gshape;
  }
  return true;
}

static bool SubgraphCompactType(const nnvm::NodeAttrs& attrs,
                                std::vector<int> *in_attrs,
                                std::vector<int> *out_attrs) {
  for (size_t i = 0; i < in_attrs->size(); i++) {
    CHECK_EQ(in_attrs->at(i), mshadow::kInt64);
  }
  for (size_t i = 0; i < out_attrs->size(); i++) {
    out_attrs->at(i) = mshadow::kInt64;
  }
  return true;
}

NNVM_REGISTER_OP(_contrib_dgl_graph_compact)
.MXNET_DESCRIBE("")
.set_attr_parser(ParamParser<SubgraphCompactParam>)
.set_num_inputs([](const NodeAttrs& attrs) {
  const SubgraphCompactParam& params = nnvm::get<SubgraphCompactParam>(attrs.parsed);
  return params.num_args;
})
.set_num_outputs([](const NodeAttrs& attrs) {
  const SubgraphCompactParam& params = nnvm::get<SubgraphCompactParam>(attrs.parsed);
  int num_varray = get_num_graphs(params);
  if (params.return_mapping)
    return num_varray * 2;
  else
    return num_varray;
})
.set_attr<nnvm::FListInputNames>("FListInputNames",
    [](const NodeAttrs& attrs) {
  const SubgraphCompactParam& params = nnvm::get<SubgraphCompactParam>(attrs.parsed);
  std::vector<std::string> names;
  names.reserve(params.num_args);
  size_t num_graphs = get_num_graphs(params);
  for (size_t i = 0; i < num_graphs; i++)
    names.push_back("graph" + std::to_string(i));
  for (size_t i = 0; i < num_graphs; ++i)
    names.push_back("varray" + std::to_string(i));
  return names;
})
.set_attr<FInferStorageType>("FInferStorageType", SubgraphCompactStorageType)
.set_attr<nnvm::FInferShape>("FInferShape", SubgraphCompactShape)
.set_attr<nnvm::FInferType>("FInferType", SubgraphCompactType)
.set_attr<FComputeEx>("FComputeEx<cpu>", SubgraphCompactComputeExCPU)
.set_attr<std::string>("key_var_num_args", "num_args")
.add_argument("graph_data", "NDArray-or-Symbol[]", "Input graphs and input vertex Ids.")
.add_arguments(SubgraphCompactParam::__FIELDS__());

///////////////////////// Edge Id ///////////////////////////

inline bool EdgeIDShape(const nnvm::NodeAttrs& attrs,
                        std::vector<TShape>* in_attrs,
                        std::vector<TShape>* out_attrs) {
  CHECK_EQ(in_attrs->size(), 3U);
  CHECK_EQ(out_attrs->size(), 1U);
  CHECK_EQ(in_attrs->at(1).ndim(), 1U);
  CHECK_EQ(in_attrs->at(2).ndim(), 1U);
  CHECK_EQ(in_attrs->at(1)[0], in_attrs->at(2)[0]);

  SHAPE_ASSIGN_CHECK(*out_attrs, 0, in_attrs->at(1));
  SHAPE_ASSIGN_CHECK(*in_attrs, 1, out_attrs->at(0));
  SHAPE_ASSIGN_CHECK(*in_attrs, 2, out_attrs->at(0));
  return out_attrs->at(0).ndim() != 0U && out_attrs->at(0).Size() != 0U;
}

inline bool EdgeIDType(const nnvm::NodeAttrs& attrs,
                       std::vector<int>* in_attrs,
                       std::vector<int>* out_attrs) {
  CHECK_EQ(in_attrs->size(), 3U);
  CHECK_EQ(out_attrs->size(), 1U);

  TYPE_ASSIGN_CHECK(*out_attrs, 0, in_attrs->at(0));
  TYPE_ASSIGN_CHECK(*in_attrs, 0, out_attrs->at(0));
  return out_attrs->at(0) != -1;
}

inline bool EdgeIDStorageType(const nnvm::NodeAttrs& attrs,
                              const int dev_mask,
                              DispatchMode* dispatch_mode,
                              std::vector<int>* in_attrs,
                              std::vector<int>* out_attrs) {
  CHECK_EQ(in_attrs->size(), 3U) << "Only works for 2d arrays";
  CHECK_EQ(out_attrs->size(), 1U);
  int& in_stype = in_attrs->at(0);
  int& out_stype = out_attrs->at(0);
  bool dispatched = false;
  if (!dispatched && in_stype == kCSRStorage) {
    // csr -> dns
    dispatched = storage_type_assign(&out_stype, kDefaultStorage,
                                     dispatch_mode, DispatchMode::kFComputeEx);
  }
  if (!dispatched) {
    LOG(ERROR) << "Cannot dispatch edge_id storage type, only works for csr matrices";
  }
  return dispatched;
}

struct edge_id_csr_forward {
  template<typename DType, typename IType, typename CType>
  MSHADOW_XINLINE static void Map(int i, DType* out_data, const DType* in_data,
                                  const IType* in_indices, const IType* in_indptr,
                                  const CType* u, const CType* v) {
    const int64_t target_row_id = static_cast<int64_t>(u[i]);
    const IType target_col_id = static_cast<IType>(v[i]);
    auto ptr = std::find(in_indices + in_indptr[target_row_id], in_indices + in_indptr[target_row_id + 1], target_col_id);
    if (ptr == in_indices + in_indptr[target_row_id + 1]) {
      // does not exist in the range
      out_data[i] = DType(-1);
    } else {
      out_data[i] = *(in_data + (ptr - in_indices));
    }
  }
};

template<typename xpu>
void EdgeIDForwardCsrImpl(const OpContext& ctx,
                          const std::vector<NDArray>& inputs,
                          const OpReqType req,
                          const NDArray& output) {
  using namespace mshadow;
  using namespace mxnet_op;
  using namespace csr;
  if (req == kNullOp) return;
  CHECK_EQ(inputs.size(), 3U);
  CHECK_EQ(req, kWriteTo) << "EdgeID with CSR only supports kWriteTo";
  Stream<xpu> *s = ctx.get_stream<xpu>();
  const NDArray& u = inputs[1];
  const nnvm::dim_t out_elems = u.shape().Size();
  if (!inputs[0].storage_initialized()) {
    MSHADOW_TYPE_SWITCH(output.dtype(), DType, {
      Kernel<mxnet_op::op_with_req<mshadow_op::identity, kWriteTo>, xpu>::Launch(
        s, out_elems, output.data().dptr<DType>(), DType(-1));
    });
    return;
  }
  const NDArray& data = inputs[0];
  const TBlob& in_data = data.data();
  const TBlob& in_indices = data.aux_data(kIdx);
  const TBlob& in_indptr = data.aux_data(kIndPtr);
  const NDArray& v = inputs[2];

  CHECK_EQ(data.aux_type(kIdx), data.aux_type(kIndPtr))
    << "The dtypes of indices and indptr don't match";
  MSHADOW_TYPE_SWITCH(data.dtype(), DType, {
    MSHADOW_IDX_TYPE_SWITCH(data.aux_type(kIdx), IType, {
      MSHADOW_TYPE_SWITCH(u.dtype(), CType, {
        Kernel<edge_id_csr_forward, xpu>::Launch(
            s, out_elems, output.data().dptr<DType>(), in_data.dptr<DType>(),
            in_indices.dptr<IType>(), in_indptr.dptr<IType>(),
            u.data().dptr<CType>(), v.data().dptr<CType>());
      });
    });
  });
}

template<typename xpu>
void EdgeIDForwardEx(const nnvm::NodeAttrs& attrs,
                     const OpContext& ctx,
                     const std::vector<NDArray>& inputs,
                     const std::vector<OpReqType>& req,
                     const std::vector<NDArray>& outputs) {
  CHECK_EQ(inputs.size(), 3U);
  CHECK_EQ(outputs.size(), 1U);
  CHECK_EQ(req.size(), 1U);
  const auto in_stype = inputs[0].storage_type();
  const auto out_stype = outputs[0].storage_type();
  if (in_stype == kCSRStorage && out_stype == kDefaultStorage) {
    EdgeIDForwardCsrImpl<xpu>(ctx, inputs, req[0], outputs[0]);
  } else {
    LogUnimplementedOp(attrs, ctx, inputs, req, outputs);
  }
}

NNVM_REGISTER_OP(_contrib_edge_id)
.describe(R"code(This operator implements the edge_id function for csr arrays,
where output[i] = input[u[i], v[i]] if input[u[i], v[i]] is a non-zero element of input,
otherwise output[i] will be -1. Both u and v should be 1D vectors.
Example::
  x = [[ 1, 0, 0 ],
       [ 0, 2, 0 ],
       [ 0, 0, 3 ]]
  u = [ 0, 0, 1, 1, 2, 2 ]
  v = [ 0, 1, 1, 2, 0, 2 ]
  edge_id(x, u, v) = [ 1, -1, 2, -1, -1, 3 ]

The storage type of ``edge_id`` output depends on storage types of inputs
  - quadratic(csr, default, default) = default
  - default and rsp inputs are not supported

)code" ADD_FILELINE)
.set_num_inputs(3)
.set_num_outputs(1)
.set_attr<nnvm::FListInputNames>("FListInputNames",
  [](const NodeAttrs& attrs) {
    return std::vector<std::string>{"data", "u", "v"};
  })
.set_attr<nnvm::FInferShape>("FInferShape", EdgeIDShape)
.set_attr<nnvm::FInferType>("FInferType", EdgeIDType)
.set_attr<FInferStorageType>("FInferStorageType", EdgeIDStorageType)
.set_attr<FComputeEx>("FComputeEx<cpu>", EdgeIDForwardEx<cpu>)
.set_attr<nnvm::FInplaceOption>("FInplaceOption",
  [](const NodeAttrs& attrs) {
    return std::vector<std::pair<int, int> >{{0, 0}};
  })
.add_argument("data", "NDArray-or-Symbol", "Input ndarray")
.add_argument("u", "NDArray-or-Symbol", "u ndarray")
.add_argument("v", "NDArray-or-Symbol", "v ndarray");

}  // namespace op
}  // namespace mxnet

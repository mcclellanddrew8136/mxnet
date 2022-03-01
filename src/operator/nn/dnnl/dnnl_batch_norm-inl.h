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
 * \file dnnl_batch_norm.cc
 * \brief
 * \author Tao Lv
 */

#ifndef MXNET_OPERATOR_NN_DNNL_DNNL_BATCH_NORM_INL_H_
#define MXNET_OPERATOR_NN_DNNL_DNNL_BATCH_NORM_INL_H_

#if MXNET_USE_ONEDNN == 1
#include <dnnl.hpp>
#include <utility>
#include <vector>

#include "operator/nn/batch_norm-inl.h"
#include "dnnl_base-inl.h"
#include "dnnl_ops-inl.h"

namespace mxnet {
namespace op {

typedef dnnl::batch_normalization_forward::primitive_desc t_bn_f_pdesc;
typedef dnnl::batch_normalization_forward::desc t_bn_f_desc;
typedef dnnl::batch_normalization_backward::primitive_desc t_bn_b_pdesc;
typedef dnnl::batch_normalization_backward::desc t_bn_b_desc;

inline static dnnl::normalization_flags _GetFlags(const std::vector<NDArray>& in_data,
                                                  const std::vector<NDArray>& aux_states,
                                                  bool is_train_and_not_global_stats,
                                                  bool fuse_relu) {
  dnnl::normalization_flags flags = static_cast<dnnl::normalization_flags>(0U);
  if (in_data.size() == 3U) {
    flags |= dnnl::normalization_flags::use_scale_shift;
  }

  // aux_states[0]: inMean
  // aux_states[1]: inVariance
  if (aux_states.size() == 2U && !is_train_and_not_global_stats) {
    flags |= dnnl::normalization_flags::use_global_stats;
  }

  if (fuse_relu) {
    flags |= dnnl::normalization_flags::fuse_norm_relu;
  }
  return flags;
}

inline static t_bn_f_pdesc _GetFwd(const dnnl::memory& data_mem,
                                   bool is_train,
                                   float eps,
                                   dnnl::normalization_flags flags) {
  auto data_md = data_mem.get_desc();
  auto engine  = CpuEngine::Get()->get_engine();

  if (is_train) {
    t_bn_f_desc bnFwd_desc(dnnl::prop_kind::forward_training, data_md, eps, flags);
    return t_bn_f_pdesc(bnFwd_desc, engine);
  } else {
    t_bn_f_desc bnFwd_desc(dnnl::prop_kind::forward_inference, data_md, eps, flags);
    return t_bn_f_pdesc(bnFwd_desc, engine);
  }
}

inline static t_bn_b_pdesc _GetBwd(const dnnl::memory& data_mem,
                                   const dnnl::memory& diff_mem,
                                   float eps,
                                   dnnl::normalization_flags flags) {
  auto data_md = data_mem.get_desc();
  auto diff_md = diff_mem.get_desc();
  auto engine  = CpuEngine::Get()->get_engine();

  t_bn_b_desc bnBwd_desc(dnnl::prop_kind::backward, diff_md, data_md, eps, flags);
  return t_bn_b_pdesc(bnBwd_desc, engine, _GetFwd(data_mem, true, eps, flags));
}

typedef ParamOpSign<BatchNormParam> DNNLBNSignature;

class DNNLBNForward {
  std::shared_ptr<const dnnl::memory> weight_m;
  std::shared_ptr<dnnl::batch_normalization_forward> fwd;
  bool is_train_and_not_global_stats;
  t_bn_f_pdesc pd;

 public:
  DNNLBNForward(const t_bn_f_pdesc& _pd, bool is_train_and_not_global_stats) : pd(_pd) {
    weight_m.reset(new dnnl::memory(pd.weights_desc(), CpuEngine::Get()->get_engine()));
    fwd.reset(new dnnl::batch_normalization_forward(pd));
    this->is_train_and_not_global_stats = is_train_and_not_global_stats;
  }

  const dnnl::memory& GetWeight() const {
    return *weight_m;
  }

  const t_bn_f_pdesc& GetPd() const {
    return pd;
  }

  const dnnl::batch_normalization_forward& GetFwd() const {
    return *fwd;
  }
};

template <typename DType>
static DNNLBNForward& GetBNForward(const BatchNormParam& param,
                                   const OpContext& ctx,
                                   const dnnl::memory* data_mem,
                                   dnnl::normalization_flags flags) {
#if DMLC_CXX11_THREAD_LOCAL
  static thread_local std::unordered_map<DNNLBNSignature, DNNLBNForward, OpHash> fwds;
#else
  static MX_THREAD_LOCAL std::unordered_map<DNNLBNSignature, DNNLBNForward, OpHash> fwds;
#endif
  DNNLBNSignature key(param);
  key.AddSign(ctx.is_train);
  key.AddSign(*data_mem);
  key.AddSign(static_cast<int>(flags));

  auto it = fwds.find(key);
  if (it == fwds.end()) {
    auto fwd_pd = _GetFwd(*data_mem, ctx.is_train, param.eps, flags);
    DNNLBNForward fwd(fwd_pd, ctx.is_train && !param.use_global_stats);
    it = AddToCache(&fwds, key, fwd);
  }
  return it->second;
}

template <typename DType>
void DNNLBatchNormForward(const nnvm::NodeAttrs& attrs,
                          const OpContext& ctx,
                          const std::vector<NDArray>& inputs,
                          const std::vector<OpReqType>& req,
                          const std::vector<NDArray>& outputs,
                          bool fuse_relu) {
  const BatchNormParam& param = nnvm::get<BatchNormParam>(attrs.parsed);
  std::vector<NDArray> in_data(inputs.begin(), inputs.begin() + batchnorm::kInMovingMean);

  mxnet::TShape shape = inputs[batchnorm::kData].shape();
  const int real_axis = mxnet::op::batchnorm::GetRealAxis(shape, param.axis);
  CHECK_LT(real_axis, shape.ndim());
  NDArray out = outputs[batchnorm::kOut];
  if (param.axis != 1 || shape.ndim() != 4) {
    // reshape to (N, C, 1, D)
    mxnet::TShape new_shape{
        static_cast<index_t>(shape.ProdShape(0, real_axis)),
        shape[real_axis],
        1,
        static_cast<index_t>(shape.ProdShape(real_axis + 1, static_cast<int>(shape.ndim())))};
    in_data[batchnorm::kData] = in_data[batchnorm::kData].Reshape(new_shape);
    out                       = out.Reshape(new_shape);
  }

  const std::vector<NDArray> aux_states(inputs.begin() + batchnorm::kInMovingMean, inputs.end());
  TmpMemMgr::Get()->Init(ctx.requested[batchnorm::kTempSpace]);
  dnnl::normalization_flags flags =
      _GetFlags(in_data, aux_states, ctx.is_train && !param.use_global_stats, fuse_relu);
  NDArray& data = in_data[batchnorm::kData];
  if (data.IsDNNLData() && data.IsView())
    data = data.Reorder2Default();
  auto data_mem = static_cast<const dnnl::memory*>(data.GetDNNLData());
  auto& fwd     = GetBNForward<DType>(param, ctx, data_mem, flags);

  // for output memory
  auto fwd_dst_desc = fwd.GetPd().dst_desc();
  auto out_mem =
      static_cast<const dnnl::memory*>(const_cast<NDArray&>(out).CreateDNNLData(&fwd_dst_desc));

  // mxnet will always use scale shift.
  // But if fix_gamma is true, then all scale elements will be set to 1.0f
  if (static_cast<int>(flags) & static_cast<int>(dnnl::normalization_flags::use_scale_shift)) {
    const NDArray& gamma = in_data[batchnorm::kGamma];
    const NDArray& beta  = in_data[batchnorm::kBeta];
    CHECK_EQ(gamma.storage_type(), mxnet::kDefaultStorage);
    CHECK_EQ(beta.storage_type(), mxnet::kDefaultStorage);

    const dnnl::memory& weight_mem = fwd.GetWeight();
    float* weight_buf              = reinterpret_cast<float*>(weight_mem.get_data_handle());

    index_t channels_ = data.shape()[1];
    CHECK(weight_mem.get_desc().get_size() == channels_ * sizeof(float) * 2);
    float* weight_ptr      = gamma.data().dptr<float>();
    float* bias_ptr        = beta.data().dptr<float>();
    const size_t copy_size = sizeof(weight_buf[0]) * channels_;
    if (!param.fix_gamma) {
      memcpy(weight_buf, weight_ptr, copy_size);
      memcpy(&weight_buf[channels_], bias_ptr, copy_size);
    } else if (IsBNWriting(req[batchnorm::kGamma])) {
      for (index_t i = 0; i < channels_; i++) {
        weight_buf[i]             = 1.0f;
        weight_ptr[i]             = 1.0f;
        weight_buf[channels_ + i] = bias_ptr[i];  // bias
      }
    } else {
      for (index_t i = 0; i < channels_; i++) {
        weight_buf[i]             = 1.0f;
        weight_buf[channels_ + i] = bias_ptr[i];  // bias
      }
    }

    dnnl_args_map_t net_args;
    net_args[DNNL_ARG_SRC]         = *data_mem;
    net_args[DNNL_ARG_SCALE_SHIFT] = weight_mem;
    net_args[DNNL_ARG_DST]         = *out_mem;
    if (fuse_relu) {
      const NDArray* workspace = nullptr;
      workspace                = &outputs[3];
      auto engine              = CpuEngine::Get()->get_engine();
      if (workspace == nullptr) {
        LOG(FATAL) << "oneDNN BatchNorm: incorrect workspace input";
      }
      auto ws = std::make_shared<dnnl::memory>(
          fwd.GetPd().workspace_desc(),
          engine,
          static_cast<const dnnl::memory*>(workspace->GetDNNLData())->get_data_handle());
      net_args[DNNL_ARG_WORKSPACE] = *ws;
    }
    if (!ctx.is_train || param.use_global_stats) {
      float* omean  = outputs[batchnorm::kMean].data().dptr<float>();
      float* ovar   = outputs[batchnorm::kVar].data().dptr<float>();
      float* inmean = aux_states[batchnorm::kMovingMean].data().dptr<float>();
      float* invar  = aux_states[batchnorm::kMovingVar].data().dptr<float>();
      // to align with origin implmentation: batch_norm.cc: L164
      for (index_t i = 0; i < channels_; i++) {
        omean[i] = inmean[i];
        ovar[i]  = VARIANCE_TO_INVSTD(invar[i], param.eps);
      }
      net_args[DNNL_ARG_MEAN] =
          *(static_cast<const dnnl::memory*>(aux_states[batchnorm::kMovingMean].GetDNNLData()));
      net_args[DNNL_ARG_VARIANCE] =
          *(static_cast<const dnnl::memory*>(aux_states[batchnorm::kMovingVar].GetDNNLData()));
      DNNLStream::Get()->RegisterPrimArgs(fwd.GetFwd(), net_args);
      DNNLStream::Get()->Submit();
    } else {  // training
      const NDArray& outMean      = outputs[batchnorm::kMean];
      const NDArray& outVar       = outputs[batchnorm::kVar];
      net_args[DNNL_ARG_MEAN]     = *(static_cast<const dnnl::memory*>(outMean.GetDNNLData()));
      net_args[DNNL_ARG_VARIANCE] = *(static_cast<const dnnl::memory*>(outVar.GetDNNLData()));
      DNNLStream::Get()->RegisterPrimArgs(fwd.GetFwd(), net_args);
      DNNLStream::Get()->Submit();

      float* ovar = outVar.data().dptr<float>();
      for (index_t i = 0; i < channels_; i++) {
        ovar[i] = VARIANCE_TO_INVSTD(ovar[i], param.eps);
      }
    }
  } else {  // no input gamma and beta
    LOG(FATAL) << "oneDNN batch normalization: should not reach here ...";
  }
}

class DNNLBNBackward {
  std::shared_ptr<dnnl::batch_normalization_backward> bwd;
  const std::shared_ptr<dnnl::memory> weight_m;
  const std::shared_ptr<dnnl::memory> gradw_m;

 public:
  const t_bn_b_pdesc pd;

  explicit DNNLBNBackward(const t_bn_b_pdesc& _pd)
      : weight_m(new dnnl::memory(_pd.weights_desc(), CpuEngine::Get()->get_engine())),
        gradw_m(new dnnl::memory(_pd.diff_weights_desc(), CpuEngine::Get()->get_engine())),
        pd(_pd) {
    bwd.reset(new dnnl::batch_normalization_backward(pd));
  }

  const dnnl::memory& GetWeight() const {
    return *weight_m;
  }

  const dnnl::memory& GetGradw() const {
    return *gradw_m;
  }

  const dnnl::batch_normalization_backward& GetBwd() const {
    return *bwd;
  }
};

template <typename DType>
static DNNLBNBackward& GetBNBackward(const BatchNormParam& param,
                                     const OpContext& ctx,
                                     const NDArray& in_data,
                                     const dnnl::memory& in_mem,
                                     const NDArray& diff_data,
                                     const dnnl::memory& diff_mem,
                                     dnnl::normalization_flags flags) {
#if DMLC_CXX11_THREAD_LOCAL
  static thread_local std::unordered_map<DNNLBNSignature, DNNLBNBackward, OpHash> bwds;
#else
  static MX_THREAD_LOCAL std::unordered_map<DNNLBNSignature, DNNLBNBackward, OpHash> bwds;
#endif
  DNNLBNSignature key(param);
  key.AddSign(in_data);
  key.AddSign(diff_data);
  key.AddSign(static_cast<int>(flags));

  auto it = bwds.find(key);
  if (it == bwds.end()) {
    auto bwd_pd = _GetBwd(in_mem, diff_mem, param.eps, flags);
    DNNLBNBackward bwd(bwd_pd);
    it = AddToCache(&bwds, key, bwd);
  }
  return it->second;
}

template <typename DType>
void DNNLBatchNormBackward(const nnvm::NodeAttrs& attrs,
                           const OpContext& ctx,
                           const std::vector<NDArray>& inputs,
                           const std::vector<OpReqType>& req,
                           const std::vector<NDArray>& outputs,
                           bool fuse_relu) {
  if (fuse_relu) {
    CHECK_EQ(inputs.size(), 9U);
  } else {
    CHECK_EQ(inputs.size(), 8U);
  }
  const BatchNormParam& param = nnvm::get<BatchNormParam>(attrs.parsed);
  std::vector<NDArray> out_grad(1);
  std::vector<NDArray> out_data(3);
  std::vector<NDArray> in_data(3);
  std::vector<NDArray> aux_states(2);
  out_grad[0]                         = inputs[0];
  out_data[batchnorm::kMean]          = inputs[1];
  out_data[batchnorm::kVar]           = inputs[2];
  in_data[batchnorm::kData]           = inputs[3];
  in_data[batchnorm::kGamma]          = inputs[4];
  in_data[batchnorm::kBeta]           = inputs[5];
  aux_states[batchnorm::kMovingMean]  = inputs[6];
  aux_states[batchnorm::kMovingVar]   = inputs[7];
  const std::vector<NDArray>& in_grad = outputs;
  TmpMemMgr::Get()->Init(ctx.requested[batchnorm::kTempSpace]);
  dnnl::normalization_flags flags =
      _GetFlags(in_data, aux_states, ctx.is_train && !param.use_global_stats, fuse_relu);

  NDArray data               = in_data[batchnorm::kData];
  NDArray diff               = out_grad[batchnorm::kOut];
  NDArray gradIn             = in_grad[batchnorm::kData];
  const NDArray& moving_mean = aux_states[batchnorm::kMovingMean];
  const NDArray& moving_var  = aux_states[batchnorm::kMovingVar];
  const NDArray& out_mean    = out_data[batchnorm::kMean];
  const NDArray& out_var     = out_data[batchnorm::kVar];

  CHECK(out_mean.IsDefaultData());
  CHECK(out_var.IsDefaultData());
  CHECK(moving_mean.IsDefaultData());
  CHECK(moving_var.IsDefaultData());

  mxnet::TShape shape = data.shape();
  const int real_axis = mxnet::op::batchnorm::GetRealAxis(shape, param.axis);
  CHECK_LT(real_axis, shape.ndim());
  if (param.axis != 1 || shape.ndim() != 4) {
    // reshape to (N, C, 1, D)
    mxnet::TShape new_shape{
        static_cast<index_t>(shape.ProdShape(0, real_axis)),
        shape[real_axis],
        1,
        static_cast<index_t>(shape.ProdShape(real_axis + 1, static_cast<int>(shape.ndim())))};
    data   = data.Reshape(new_shape);
    diff   = diff.Reshape(new_shape);
    gradIn = gradIn.Reshape(new_shape);
  }

  auto data_mem = static_cast<const dnnl::memory*>(data.GetDNNLData());
  auto diff_mem = static_cast<const dnnl::memory*>(diff.GetDNNLData());
  // DNNL batchnorm should run on special layouts. If one of them isn't, we
  // should reorder them.
  if (data.IsDefaultData()) {
    auto diff_desc = diff_mem->get_desc();
    data_mem       = static_cast<const dnnl::memory*>(data.GetDNNLDataReorder(&diff_desc));
  } else if (diff.IsDefaultData()) {
    auto data_desc = data_mem->get_desc();
    diff_mem       = static_cast<const dnnl::memory*>(diff.GetDNNLDataReorder(&data_desc));
  }
  auto& bwd = GetBNBackward<DType>(param, ctx, data, *data_mem, diff, *diff_mem, flags);
  auto gradi_mem =
      CreateDNNLMem(const_cast<NDArray&>(gradIn), bwd.pd.diff_src_desc(), req[batchnorm::kData]);

  if (static_cast<int>(flags) & static_cast<int>(dnnl::normalization_flags::use_scale_shift)) {
    const NDArray& gamma   = in_data[batchnorm::kGamma];
    const NDArray& beta    = in_data[batchnorm::kBeta];
    DType* weight_buf      = reinterpret_cast<DType*>(bwd.GetWeight().get_data_handle());
    index_t channels_      = data.shape()[1];
    DType* weight_ptr      = gamma.data().dptr<DType>();
    DType* bias_ptr        = beta.data().dptr<DType>();
    const size_t copy_size = sizeof(DType) * channels_;
    if (!param.fix_gamma) {
      memcpy(weight_buf, weight_ptr, copy_size);
      memcpy(&weight_buf[channels_], bias_ptr, copy_size);
    } else {
      for (index_t i = 0; i < channels_; i++) {
        weight_buf[i] = static_cast<DType>(1.0f);
      }
      memcpy(&weight_buf[channels_], bias_ptr, copy_size);
    }
    dnnl_args_map_t net_args;
    net_args[DNNL_ARG_SRC]              = *data_mem;
    net_args[DNNL_ARG_DIFF_SRC]         = *gradi_mem.second;
    net_args[DNNL_ARG_SCALE_SHIFT]      = bwd.GetWeight();
    net_args[DNNL_ARG_DIFF_SCALE_SHIFT] = bwd.GetGradw();
    net_args[DNNL_ARG_DIFF_DST]         = *diff_mem;

    if (fuse_relu) {
      const NDArray* workspace = nullptr;
      workspace                = &inputs[8];
      if (workspace != nullptr) {
        net_args[DNNL_ARG_WORKSPACE] =
            *(static_cast<const dnnl::memory*>(workspace->GetDNNLData()));
      }
    }

    // training but no input mean and variance
    if (ctx.is_train && !param.use_global_stats) {
      DType* moving_mean_ptr = moving_mean.data().dptr<DType>();
      DType* moving_var_ptr  = moving_var.data().dptr<DType>();
      DType* out_mean_ptr    = out_mean.data().dptr<DType>();
      DType* out_var_ptr     = out_var.data().dptr<DType>();
      dnnl::memory var_mem(bwd.pd.variance_desc(), CpuEngine::Get()->get_engine());
      DType* tmp_var_ptr = reinterpret_cast<DType*>(var_mem.get_data_handle());

      DType minus_mom = (1.0f - param.momentum);
      for (index_t i = 0; i < channels_; i++) {
        moving_mean_ptr[i] = moving_mean_ptr[i] * param.momentum + out_mean_ptr[i] * minus_mom;
        float variance     = INVSTD_TO_VARIANCE(out_var_ptr[i], param.eps);
        tmp_var_ptr[i]     = variance;
        moving_var_ptr[i]  = moving_var_ptr[i] * param.momentum + variance * minus_mom;
      }
      net_args[DNNL_ARG_MEAN]     = *(static_cast<const dnnl::memory*>(out_mean.GetDNNLData()));
      net_args[DNNL_ARG_VARIANCE] = var_mem;
    } else {
      net_args[DNNL_ARG_MEAN]     = *(static_cast<const dnnl::memory*>(moving_mean.GetDNNLData()));
      net_args[DNNL_ARG_VARIANCE] = *(static_cast<const dnnl::memory*>(moving_var.GetDNNLData()));
    }
    DNNLStream::Get()->RegisterPrimArgs(bwd.GetBwd(), net_args);
    CommitOutput(gradIn, gradi_mem);
    DNNLStream::Get()->Submit();

    // copy data from gradw_mem to in_grad[1] and in_grad[2]
    DType* gw_buf   = reinterpret_cast<DType*>(bwd.GetGradw().get_data_handle());
    DType* w_grad_1 = in_grad[batchnorm::kGamma].data().dptr<DType>();
    DType* w_grad_2 = in_grad[batchnorm::kBeta].data().dptr<DType>();

    // the gradient of gamma
    if (!param.fix_gamma) {
      if (req[batchnorm::kGamma] != kNullOp) {
        if (req[batchnorm::kGamma] != kAddTo) {
          memcpy(w_grad_1, gw_buf, copy_size);
        } else {
          for (index_t i = 0; i < channels_; i++) {
            w_grad_1[i] += gw_buf[i];
          }
        }
      }
    } else {
      for (index_t i = 0; i < channels_; i++) {
        (in_grad[1].data().dptr<DType>())[i] = 0.0f;
      }
    }

    // the gradient of beta
    if (req[batchnorm::kBeta] != kNullOp) {
      if (req[batchnorm::kBeta] != kAddTo) {
        memcpy(w_grad_2, &gw_buf[channels_], copy_size);
      } else {
        DType* grad_beta = &gw_buf[channels_];
        for (index_t i = 0; i < channels_; i++) {
          w_grad_2[i] += grad_beta[i];
        }
      }
    }
  } else {
    LOG(FATAL) << "oneDNN batch normalization backward: should not reach here ...";
  }
}
}  // namespace op
}  // namespace mxnet
#endif  // MXNET_USE_ONEDNN
#endif  // MXNET_OPERATOR_NN_DNNL_DNNL_BATCH_NORM_INL_H_

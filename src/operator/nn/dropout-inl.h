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
 * Copyright (c) 2015 by Contributors
 * \file dropout-inl.h
 * \brief
 * \author Bing Xu, Da Zheng, Hang Zhang
*/

#ifndef MXNET_OPERATOR_NN_DROPOUT_INL_H_
#define MXNET_OPERATOR_NN_DROPOUT_INL_H_
#include <dmlc/logging.h>
#include <dmlc/parameter.h>
#include <mxnet/operator.h>
#include <map>
#include <vector>
#include <string>
#include <utility>
#include <algorithm>
#include "../mxnet_op.h"
#include "../mshadow_op.h"
#include "../random/sampler.h"

#if defined(USE_MKL) && defined(_OPENMP)
#include <omp.h>

#include <mkl_vml_functions.h>
#include <mkl_vsl.h>
#endif  // USE_MKL && _OPENMP

namespace dropout {
enum DropoutOpInputs {kData};
enum DropoutOpOutputs {kOut, kMask};
enum DropoutOpForwardResource {kRandom};
enum DropoutOpMode {kTraining, kAlways};
}  // namespace dropout

namespace mxnet {
namespace op {

const int MAX_DIM = 5;

struct DropoutParam : public dmlc::Parameter<DropoutParam> {
  float p;
  int mode;
  TShape axes;
  DMLC_DECLARE_PARAMETER(DropoutParam) {
    DMLC_DECLARE_FIELD(p).set_default(0.5)
    .set_range(0, 1)
    .describe("Fraction of the input that gets dropped out during training time.");
    DMLC_DECLARE_FIELD(mode)
    .add_enum("training", dropout::kTraining)
    .add_enum("always", dropout::kAlways)
    .set_default(dropout::kTraining)
    .describe("Whether to only turn on dropout during training or to also turn on for inference.");
    DMLC_DECLARE_FIELD(axes).set_default(TShape())
    .describe("Axes for variational dropout kernel.");
  }
};  // struct DropoutParam

namespace mxnet_op {
template<int ndim, typename DType, typename OP>
struct binary_broadcast_kernel {
  /*! \brief Map function for binary_broadcast_kernel */
  MSHADOW_XINLINE static void Map(int base, int length, OpReqType req,
                                  const Shape <ndim> &lstride, const Shape <ndim> &rstride,
                                  const Shape <ndim> &oshape, DType *lhs, DType *rhs,
                                  DType *out) {
    Shape <ndim> coord = unravel(base, oshape);
    auto lidx = static_cast<index_t>(dot(coord, lstride));
    auto ridx = static_cast<index_t>(dot(coord, rstride));
    KERNEL_ASSIGN(out[base], req, OP::Map(lhs[lidx], rhs[ridx]));
    // starts from 1 to avoid extra inc at end of loop
    for (int i = 1; i < length; ++i) {
      inc(&coord, oshape, &lidx, lstride, &ridx, rstride);
      // When tuning, don't actually run the op, since it's not going to be tuned against
      // the actual op we'll eventually be using
      KERNEL_ASSIGN(out[base + i], req, OP::Map(lhs[lidx], rhs[ridx]));
    }
  }
};
}  // namespace mxnet_op

#define BROADCAST_NDIM_SWITCH(ndim, NDim, ...)  \
  if (ndim <= 2) {                    \
    const int NDim = 2;               \
    {__VA_ARGS__}                     \
  } else if (ndim <= 4) {             \
    const int NDim = 4;               \
    {__VA_ARGS__}                     \
  } else if (ndim <= MAX_DIM) {  \
    const int NDim = MAX_DIM;    \
    {__VA_ARGS__}                     \
  } else {                            \
    LOG(FATAL) << "NDim too large ";  \
  }

inline int BinaryBroadcastShapeCompact(const TShape& lshape, const TShape& rshape,
                                       const TShape& oshape, TShape *new_lshape,
                                       TShape *new_rshape, TShape *new_oshape) {
  if (lshape == rshape) return 0;
  index_t odim = std::max<index_t>(oshape.ndim(), MAX_DIM);
  *new_lshape = TShape(odim);
  *new_rshape = TShape(odim);
  *new_oshape = TShape(odim);
  index_t bl = oshape.ndim() - lshape.ndim();
  index_t br = oshape.ndim() - rshape.ndim();
  index_t j = 0, lprod = 1, rprod = 1, oprod = 1;
  for (index_t i = 0; i < oshape.ndim(); ++i) {
    index_t l = 1, r = 1, o = oshape[i];
    if (i >= bl) l = lshape[i-bl];
    if (i >= br) r = rshape[i-br];
    if ((lprod != rprod || l != r) &&
        lprod*l > 1 && rprod*r > 1) {
      (*new_lshape)[j] = lprod;
      (*new_rshape)[j] = rprod;
      (*new_oshape)[j] = oprod;
      lprod = rprod = oprod = 1; ++j;
    }
    lprod *= l;
    rprod *= r;
    oprod *= o;
  }
  if (lprod > 1 || rprod > 1) {
    (*new_lshape)[j] = lprod;
    (*new_rshape)[j] = rprod;
    (*new_oshape)[j] = oprod;
    ++j;
  }
  if (j <= MAX_DIM) {
    BROADCAST_NDIM_SWITCH(j, NDim, {
      new_lshape->assign(&(*new_lshape)[0], &(*new_lshape)[NDim]);
      new_rshape->assign(&(*new_rshape)[0], &(*new_rshape)[NDim]);
      new_oshape->assign(&(*new_oshape)[0], &(*new_oshape)[NDim]);
    });
  } else {
    LOG(FATAL) << "Too many broadcast dimensions with operands " << lshape << " " << rshape;
  }
  return j;
}

template<typename xpu, typename DType>
class DropoutOp {
#if defined(USE_MKL) && defined(_OPENMP)
  static void BernoulliGenerate(common::random::RandGenerator<cpu, DType> gen,
                                int n, double p, int* r) {
    typename RandGenerator<xpu, DType>::Impl genImpl(&gen, 1);
    const int seed = 17 + genImpl.rand() % 4096;  // NOLINT(runtime/threadsafe_fn)
    const int nthr = engine::OpenMP::Get()->GetRecommendedOMPThreadCount();
#pragma omp parallel num_threads(nthr)
    {
      const int ithr = omp_get_thread_num();
      const int avg_amount = (n + nthr - 1) / nthr;
      const int my_offset = ithr * avg_amount;
      const int my_amount = std::min(my_offset + avg_amount, n) - my_offset;
      if (my_amount > 0) {
        VSLStreamStatePtr stream;
        vslNewStream(&stream, VSL_BRNG_MCG31, seed + my_offset);
        vslSkipAheadStream(stream, my_offset);
        viRngBernoulli(VSL_RNG_METHOD_BERNOULLI_ICDF, stream, my_amount, r + my_offset, p);
        vslDeleteStream(&stream);
      }
    }
  }

  // MKL forward pass
  static bool MSHADOW_CINLINE MKLForward(mshadow::Stream<cpu> *s, RandGenerator<cpu, DType> *pgen,
                                         const double pkeep,
                                         const std::vector<TBlob> &in_data,
                                         const std::vector<TBlob> &out_data) {
    // BernoulliGenerate expects an array int, so for types smaller than int, the mask buffer
    // will be too small, so we can;t use MKL in those cases
    if (sizeof(DType) >= sizeof(int)) {
      Tensor<xpu, 2, DType> mask = out_data[dropout::kMask].FlatTo2D<xpu, DType>(s);
      Tensor<xpu, 2, DType> data = in_data[dropout::kData].FlatTo2D<xpu, DType>(s);
      Tensor<xpu, 2, DType> out = out_data[dropout::kOut].FlatTo2D<xpu, DType>(s);
      DType *outptr = out.dptr_;
      DType *dataptr = data.dptr_;
      auto maskptr = reinterpret_cast<int *>(mask.dptr_);
      int count = mask.shape_[0] * mask.shape_[1];
      BernoulliGenerate(*pgen, count, pkeep, maskptr);
      const float pk_1 = 1.0f / pkeep;
#pragma omp parallel for num_threads(engine::OpenMP::Get()->GetRecommendedOMPThreadCount())
      for (int i = 0; i < count; ++i) {
        outptr[i] = dataptr[i] * maskptr[i] * pk_1;
      }
      return true;
    }
    return false;
  }

  // MKL backward pass
  static bool MSHADOW_CINLINE MKLBackward(mshadow::Stream<cpu> *s, const double pkeep,
                                          const std::vector<TBlob> &in_grad,
                                          const std::vector<TBlob> &out_data,
                                          const std::vector<TBlob> &out_grad) {
    if (sizeof(DType) >= sizeof(int)) {
      Tensor<xpu, 2, DType> grad = out_grad[dropout::kOut].FlatTo2D<xpu, DType>(s);
      Tensor<xpu, 2, DType> mask = out_data[dropout::kMask].FlatTo2D<xpu, DType>(s);
      Tensor<xpu, 2, DType> gdata = in_grad[dropout::kData].FlatTo2D<xpu, DType>(s);
      DType *ingradptr = gdata.dptr_;
      const DType *outgradptr = grad.dptr_;
      auto maskptr = reinterpret_cast<int *>(mask.dptr_);
      int count = mask.shape_[0] * mask.shape_[1];
      const float pk_1 = 1.0f / pkeep;
#pragma omp parallel for num_threads(engine::OpenMP::Get()->GetRecommendedOMPThreadCount())
      for (int i = 0; i < count; ++i) {
        ingradptr[i] = outgradptr[i] * maskptr[i] * pk_1;
      }
      return true;
    }
    return false;
  }

#ifdef __CUDACC__
  // GPU never uses MKL
  static bool MSHADOW_CINLINE MKLForward(mshadow::Stream<gpu> *s, RandGenerator<gpu, DType> *pgen,
                                         const double pkeep,
                                         const std::vector<TBlob> &in_data,
                                         const std::vector<TBlob> &out_data) {
    return false;
  }
  static bool MSHADOW_CINLINE MKLBackward(mshadow::Stream<gpu> *s, const double pkeep,
                                          const std::vector<TBlob> &in_grad,
                                          const std::vector<TBlob> &out_data,
                                          const std::vector<TBlob> &out_grad) {
    return false;
  }
#endif  // __CUDACC__

#else  // #if defined(USE_MKL) && defined(_OPENMP)
  static bool MSHADOW_CINLINE MKLForward(mshadow::Stream<xpu> *s, RandGenerator<xpu, DType> *pgen,
                                const double pkeep,
                                const std::vector<TBlob> &in_data,
                                const std::vector<TBlob> &out_data) {
    return false;
  }
  static bool MSHADOW_CINLINE MKLBackward(mshadow::Stream<xpu> *s, const double pkeep,
                                          const std::vector<TBlob> &in_grad,
                                          const std::vector<TBlob> &out_data,
                                          const std::vector<TBlob> &out_grad) {
    return false;
  }
#endif  // #if defined(USE_MKL) && defined(_OPENMP)

 public:
  /*!
   * \brief Dropout kernel, compute dropout tensor
   */
  struct BernoulliKernel {
    /*! \brief Bernoulli kernel for generating mask */
    MSHADOW_XINLINE static void Map(int id,
                                    RandGenerator<xpu, DType> gen,
                                    const int N,
                                    const int step,
                                    DType *mask_out,
                                    const real_t pkeep) {
      RNG_KERNEL_LOOP(xpu, DType, id, gen, N, step, {
        const real_t rand_num = static_cast<real_t>(genImpl.uniform());
        mask_out[i] = mshadow_op::threshold::Map<real_t>(rand_num, pkeep) * (1.0f / pkeep);
      });
    }
  };

  void Init(const DropoutParam &param) {
    this->pkeep_ = 1.0f - param.p;
    this->mode_ = static_cast<dropout::DropoutOpMode>(param.mode);
  }

  void Forward(const OpContext &ctx,
               const std::vector<TBlob> &in_data,
               const std::vector<OpReqType> &req,
               const std::vector<TBlob> &out_data) {
    if (req[dropout::kOut] != kNullOp) {
      CHECK_EQ(in_data.size(), 1U);
      if (ctx.is_train) {
        CHECK_EQ(out_data.size(), 2U);
      }
      Stream<xpu> *s = ctx.get_stream<xpu>();
      const TBlob &out = out_data[dropout::kOut];
      if (ctx.is_train || this->mode_ == dropout::kAlways) {
        RandGenerator<xpu, DType> *pgen = ctx.requested[0].get_parallel_random<xpu, DType>();
        CHECK_NOTNULL(pgen);
        if (!MKLForward(s, pgen, this->pkeep_, in_data, out_data)) {
          const TBlob &mask = out_data[dropout::kMask];
          CHECK(req[dropout::kOut] != kAddTo);
          // initialize the mask
          LaunchRNG<BernoulliKernel, xpu>(s, pgen, out.Size(),
                                          mask.dptr<DType>(),
                                          this->pkeep_);
          if (req[0] != kNullOp) {
            // broardcast mul
            TShape new_lshape, new_rshape, new_oshape;
            int ndim = BinaryBroadcastShapeCompact(in_data[dropout::kData].shape_,
                                                   mask.shape_, out.shape_,
                                                   &new_lshape, &new_rshape, &new_oshape);
            BROADCAST_NDIM_SWITCH(ndim, NDim, {
              mshadow::Shape<NDim> oshape = new_oshape.get<NDim>();
              mshadow::Shape<NDim> lstride = mxnet_op::calc_stride(new_lshape.get<NDim>());
              mshadow::Shape<NDim> rstride = mxnet_op::calc_stride(new_rshape.get<NDim>());
              mxnet_op::Kernel<mxnet_op::binary_broadcast_kernel<NDim, DType,
                               mshadow_op::mul>, xpu>::
              template LaunchEx(s, new_oshape.Size(), req[0], lstride, rstride, oshape,
              in_data[dropout::kData].dptr<DType>(),
              mask.dptr<DType>(), out.dptr<DType>());
            });
          }
        }
      } else {
        const TBlob& data = in_data[dropout::kData];
        if (req[dropout::kOut] == kWriteTo) {
          mxnet_op::copy(s, out, data);
        } else {
          MXNET_ASSIGN_REQ_SWITCH(req[dropout::kOut], Req, {
            mxnet_op::Kernel<mxnet_op::op_with_req<mshadow_op::identity, Req>, xpu>::Launch(
              s, out.Size(), out.dptr<DType>(), data.dptr<DType>());
          });
        }
      }
    }
  }

  void Backward(const OpContext &ctx,
                const std::vector<TBlob> &out_grad,
                const std::vector<TBlob> &out_data,
                const std::vector<OpReqType> &req,
                const std::vector<TBlob> &in_grad) {
    using namespace mshadow;
    using namespace mshadow::expr;
    Stream<xpu> *s = ctx.get_stream<xpu>();
    if (ctx.is_train || mode_ == dropout::kAlways) {
      if (!MKLBackward(s, this->pkeep_, in_grad, out_data, out_grad)) {
        const TBlob &gdata = in_grad[dropout::kData];
        const TBlob &grad = out_grad[dropout::kOut];
        const TBlob &mask = out_data[dropout::kMask];
        // broardcast mul
        TShape new_lshape, new_rshape, new_oshape;
        int ndim = BinaryBroadcastShapeCompact(grad.shape_,
                                               mask.shape_, gdata.shape_,
                                               &new_lshape, &new_rshape, &new_oshape);
        BROADCAST_NDIM_SWITCH(ndim, NDim, {
          mshadow::Shape<NDim> oshape = new_oshape.get<NDim>();
          mshadow::Shape<NDim> lstride = mxnet_op::calc_stride(new_lshape.get<NDim>());
          mshadow::Shape<NDim> rstride = mxnet_op::calc_stride(new_rshape.get<NDim>());
          mxnet_op::Kernel<mxnet_op::binary_broadcast_kernel<NDim, DType,
                           mshadow_op::mul>, xpu>::
          template LaunchEx(s, new_oshape.Size(), req[0], lstride, rstride, oshape,
          grad.dptr<DType>(), mask.dptr<DType>(), gdata.dptr<DType>());
        });
      }
    } else {
      const TBlob& gdata = in_grad[dropout::kData];
      const TBlob& grad = out_grad[dropout::kOut];
      if (req[dropout::kData] == kWriteTo) {
        mxnet_op::copy(s, gdata, grad);
      } else {
        MXNET_ASSIGN_REQ_SWITCH(req[dropout::kData], Req, {
          mxnet_op::Kernel<mxnet_op::op_with_req<mshadow_op::identity, Req>, xpu>::Launch(
            s, gdata.Size(), gdata.dptr<DType>(), grad.dptr<DType>());
        });
      }
    }
  }

 private:
  /*! \brief Dropout rate (keep when the generated random number is less than this value) */
  real_t pkeep_;
  /*! \brief Dropout mode */
  dropout::DropoutOpMode mode_;
};  // class DropoutOp

template<typename xpu>
void DropoutCompute(const nnvm::NodeAttrs& attrs,
                    const OpContext& ctx,
                    const std::vector<TBlob>& inputs,
                    const std::vector<OpReqType>& req,
                    const std::vector<TBlob>& outputs) {
  const DropoutParam& param = nnvm::get<DropoutParam>(attrs.parsed);
  MSHADOW_REAL_TYPE_SWITCH(inputs[0].type_flag_, DType, {
    static thread_local DropoutOp<xpu, DType> op;
    op.Init(param);
    op.Forward(ctx, inputs, req, outputs);
  });
}

template<typename xpu>
void DropoutGradCompute(const nnvm::NodeAttrs& attrs,
                        const OpContext& ctx,
                        const std::vector<TBlob>& inputs,
                        const std::vector<OpReqType>& req,
                        const std::vector<TBlob>& outputs) {
  const DropoutParam& param = nnvm::get<DropoutParam>(attrs.parsed);
  CHECK_EQ(inputs.size(), 2U);
  CHECK_EQ(outputs.size(), 1);
  CHECK_EQ(req.size(), 1);
  std::vector<TBlob> out_grads(2);
  std::vector<TBlob> out_data(2);
  out_grads[dropout::kOut] = inputs[0];
  out_data[dropout::kMask] = inputs[1];

  MSHADOW_REAL_TYPE_SWITCH(inputs[0].type_flag_, DType, {
    static thread_local DropoutOp<xpu, DType> op;
    op.Init(param);
    op.Backward(ctx, out_grads, out_data, req, outputs);
  });
}

}  // namespace op
}  // namespace mxnet
#endif  // MXNET_OPERATOR_NN_DROPOUT_INL_H_

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
#include "../tensor/elemwise_binary_broadcast_op.h"

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
  dmlc::optional<bool> cudnn_off;
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
    DMLC_DECLARE_FIELD(cudnn_off).set_default(dmlc::optional<bool>(false))
    .describe("Whether to turn off cudnn in dropout operator.");
  }
};  // struct DropoutParam

template<typename xpu, typename DType>
class DropoutOp {
#if defined(USE_MKL) && defined(_OPENMP) && !defined(__CUDACC__)
  static void BernoulliGenerate(common::random::RandGenerator<cpu, DType> gen,
                                int n, double p, int* r) {
    typename RandGenerator<xpu, DType>::Impl genImpl(&gen, 1);
    const int seed = 17 + abs(genImpl.rand() % 4096);
    CHECK_GE(seed, 0);
    const int nthr = engine::OpenMP::Get()->GetRecommendedOMPThreadCount();
#pragma omp parallel num_threads(nthr)
    {
      const int ithr = omp_get_thread_num();
      const int avg_amount = (n + nthr - 1) / nthr;
      const int my_offset = ithr * avg_amount;
      const int my_amount = std::min(my_offset + avg_amount, n) - my_offset;
      if (my_amount > 0) {
        VSLStreamStatePtr stream;
        vslNewStream(&stream, VSL_BRNG_MCG31, seed);
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

#else  // #if defined(USE_MKL) && defined(_OPENMP) && !defined(__CUDACC__)
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
#endif  // #if defined(USE_MKL) && defined(_OPENMP) && !defined(__CUDACC__)

 public:
  /*!
   * \brief Dropout kernel, compute dropout tensor
   */
  struct DropoutKernel {
    /*!
     * \brief Dropout kernel function
     * \param id Thread number (0-based representing count)
     * \param gen Random number generator
     * \param N Total number of items in the output
     * \param step Step between items, related to parallelism
     * \param dropout_out Output dropout values
     * \param mask_out  Output mask (is multiplied to create dropout output, may be 0)
     * \param input_data Input data to perform the dropout on
     * \param pkeep Dropout rate (keep when the generated random number is less than this value)
     */
    MSHADOW_XINLINE static void Map(int id,
                                    RandGenerator<xpu, DType> gen,
                                    const int N,
                                    const int step,
                                    DType *dropout_out,
                                    DType *mask_out,
                                    const DType *input_data,
                                    const real_t pkeep) {
      RNG_KERNEL_LOOP(xpu, DType, id, gen, N, step, {
        const real_t rand_num = static_cast<real_t>(genImpl.uniform());
        mask_out[i] = mshadow_op::threshold_eq::Map<real_t>(rand_num, pkeep) * (1.0f / pkeep);
        dropout_out[i] = input_data[i] * mask_out[i];
      });
    }
  };
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

  explicit DropoutOp(const DropoutParam &param, Context ctx) {
    this->pkeep_ = 1.0f - param.p;
    this->mode_ = static_cast<dropout::DropoutOpMode>(param.mode);
    this->axes_ = param.axes;
#if MXNET_USE_CUDNN == 1
    this->cudnn_off_ = param.cudnn_off && param.cudnn_off.value();
    this->ctx_ = ctx;
    if (ctx.dev_type == kGPU && this->pkeep_ > 0) {
      init_cudnn_ = false;
      dtype_ = mshadow::DataType<DType>::kCudnnFlag;
      CUDNN_CALL(cudnnCreateTensorDescriptor(&x_desc_));
      CUDNN_CALL(cudnnCreateTensorDescriptor(&y_desc_));
      CUDNN_CALL(cudnnCreateTensorDescriptor(&dx_desc_));
      CUDNN_CALL(cudnnCreateTensorDescriptor(&dy_desc_));
      CUDNN_CALL(cudnnCreateDropoutDescriptor(&dropout_desc_));
    }
#endif  // MXNET_USE_CUDNN == 1
  }

  ~DropoutOp() {
#if MXNET_USE_CUDNN == 1
    if (this->ctx_.dev_type == kGPU && this->pkeep_ > 0) {
      CUDNN_CALL(cudnnDestroyTensorDescriptor(x_desc_));
      CUDNN_CALL(cudnnDestroyTensorDescriptor(y_desc_));
      CUDNN_CALL(cudnnDestroyTensorDescriptor(dx_desc_));
      CUDNN_CALL(cudnnDestroyTensorDescriptor(dy_desc_));
      CUDNN_CALL(cudnnDestroyDropoutDescriptor(dropout_desc_));
      if (init_cudnn_) {
        Storage::Get()->Free(dropout_states_);
        Storage::Get()->Free(reserve_space_);
      }
    }
#endif  // MXNET_USE_CUDNN == 1
  }

#if MXNET_USE_CUDNN == 1 && defined(__CUDACC__)
  inline void CuDNNForward(const OpContext &ctx,
                           const TBlob &in,
                           const TBlob &out) {
      Stream<xpu> *s = ctx.get_stream<xpu>();

      // set dropout state.
      // TODO(szha): expensive call, should be cached and reused across operators.
      if (!init_cudnn_) {
        CUDNN_CALL(cudnnDropoutGetStatesSize(s->dnn_handle_, &dropout_state_byte_));
        dropout_states_ = Storage::Get()->Alloc(dropout_state_byte_, Context::GPU(s->dev_id));
        CUDNN_CALL(cudnnSetDropoutDescriptor(dropout_desc_, s->dnn_handle_,
                                             1.0f - this->pkeep_,
                                             dropout_states_.dptr, dropout_state_byte_,
                                             seed_));
      }

      // describe input/output tensor
      int dim[4], stride[4];
      dim[0] = 1;
      dim[1] = 1;
      dim[2] = 1;
      dim[3] = out.Size();
      stride[0] = out.Size();
      stride[1] = out.Size();
      stride[2] = out.Size();
      stride[3] = 1;
      CUDNN_CALL(cudnnSetTensorNdDescriptor(x_desc_,
                                            dtype_,
                                            4,
                                            dim,
                                            stride));
      CUDNN_CALL(cudnnSetTensorNdDescriptor(y_desc_,
                                            dtype_,
                                            4,
                                            dim,
                                            stride));

      // perform dropout with cudnn
      CUDNN_CALL(cudnnDropoutGetReserveSpaceSize(x_desc_, &dropout_reserve_byte_));
      if (init_cudnn_ && dropout_reserve_byte_ > reserve_space_.size) {
        Storage::Get()->Free(reserve_space_);
        init_cudnn_ = false;
      }
      if (!init_cudnn_) {
        reserve_space_ = Storage::Get()->Alloc(dropout_reserve_byte_, Context::GPU(s->dev_id));
        init_cudnn_ = true;
      }
      CUDNN_CALL(cudnnDropoutForward(s->dnn_handle_,
                                     dropout_desc_,
                                     x_desc_,
                                     in.dptr<DType>(),
                                     y_desc_,
                                     out.dptr<DType>(),
                                     reserve_space_.dptr,
                                     dropout_reserve_byte_));
  }

  inline void CuDNNBackward(const OpContext &ctx,
                            const TBlob &out_grad,
                            const TBlob &in_grad) {
      Stream<xpu> *s = ctx.get_stream<xpu>();

      // describe input/output tensor
      int dim[4], stride[4];
      dim[0] = 1;
      dim[1] = 1;
      dim[2] = 1;
      dim[3] = in_grad.Size();
      stride[0] = in_grad.Size();
      stride[1] = in_grad.Size();
      stride[2] = in_grad.Size();
      stride[3] = 1;
      CUDNN_CALL(cudnnSetTensorNdDescriptor(dy_desc_,
                                            dtype_,
                                            4,
                                            dim,
                                            stride));
      CUDNN_CALL(cudnnSetTensorNdDescriptor(dx_desc_,
                                            dtype_,
                                            4,
                                            dim,
                                            stride));

      // perform dropout with cudnn
      CUDNN_CALL(cudnnDropoutBackward(s->dnn_handle_,
                                      dropout_desc_,
                                      dy_desc_,
                                      out_grad.dptr<DType>(),
                                      dx_desc_,
                                      in_grad.dptr<DType>(),
                                      reserve_space_.dptr,
                                      dropout_reserve_byte_));
  }
#endif  // MXNET_USE_CUDNN == 1 && defined(__CUDACC__)

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
        if (this->axes_.ndim() != 0 || !MKLForward(s, pgen, this->pkeep_, in_data, out_data)) {
          const TBlob &mask = out_data[dropout::kMask];
          CHECK(req[dropout::kOut] != kAddTo);
          if (this->axes_.ndim() == 0) {
            // standard case for dropout
#if MXNET_USE_CUDNN == 1 && defined(__CUDACC__)
            if (this->pkeep_ > 0 && !this->cudnn_off_) {
              CuDNNForward(ctx, in_data[dropout::kData], out);
            } else {
              // existing dropout produces inf with pkeep=0,
              // thus revert to existing GPU kernel for consistency.
              LaunchRNG<DropoutKernel, xpu>(s, pgen, out.Size(),
                                            out.dptr<DType>(),
                                            mask.dptr<DType>(),
                                            in_data[dropout::kData].dptr<DType>(),
                                            this->pkeep_);
            }
#else
            LaunchRNG<DropoutKernel, xpu>(s, pgen, out.Size(),
                                          out.dptr<DType>(),
                                          mask.dptr<DType>(),
                                          in_data[dropout::kData].dptr<DType>(),
                                          this->pkeep_);
#endif  // MXNET_USE_CUDNN == 1 && defined(__CUDACC__)
            return;
          }

          // initialize the mask
          LaunchRNG<BernoulliKernel, xpu>(s, pgen, mask.Size(),
                                          mask.dptr<DType>(),
                                          this->pkeep_);
          // broadcast mul
          TShape new_lshape, new_rshape, new_oshape;
          int ndim = BinaryBroadcastShapeCompact(in_data[dropout::kData].shape_,
                                                 mask.shape_, out.shape_,
                                                 &new_lshape, &new_rshape, &new_oshape);
          if (!ndim) {
            MXNET_ASSIGN_REQ_SWITCH(req[dropout::kOut], Req, {
              mxnet_op::Kernel<mxnet_op::op_with_req<mshadow_op::mul, Req>, xpu>::Launch(
                s, out.Size(), out.dptr<DType>(), in_data[dropout::kData].dptr<DType>(),
                mask.dptr<DType>());
            });
          } else {
            BROADCAST_NDIM_SWITCH(ndim, NDim, {
              mshadow::Shape<NDim> oshape = new_oshape.get<NDim>();
              mshadow::Shape<NDim> lstride = mxnet_op::calc_stride(new_lshape.get<NDim>());
              mshadow::Shape<NDim> rstride = mxnet_op::calc_stride(new_rshape.get<NDim>());
              mxnet_op::Kernel<mxnet_op::binary_broadcast_kernel<NDim, DType,
                               mshadow_op::mul>, xpu>::
              template LaunchEx(s, new_oshape.Size(), req[dropout::kOut],
              lstride, rstride, oshape,
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
      if (this->axes_.ndim() != 0 || !MKLBackward(s, this->pkeep_, in_grad, out_data, out_grad)) {
        const TBlob &gdata = in_grad[dropout::kData];
        const TBlob &grad = out_grad[dropout::kOut];
        const TBlob &mask = out_data[dropout::kMask];
        if (this->axes_.ndim() == 0) {
          // standard case for dropout
          CHECK_EQ(grad.Size(), mask.Size());
#if MXNET_USE_CUDNN == 1 && defined(__CUDACC__)
          if (this->pkeep_ > 0 && !this->cudnn_off_) {
            CuDNNBackward(ctx, grad, gdata);
          } else {
            MXNET_ASSIGN_REQ_SWITCH(req[dropout::kData], Req, {
              mxnet_op::Kernel<mxnet_op::op_with_req<mshadow_op::mul, Req>, xpu>::Launch(
                s, gdata.Size(), gdata.dptr<DType>(), grad.dptr<DType>(), mask.dptr<DType>());
            });
          }
#else
          MXNET_ASSIGN_REQ_SWITCH(req[dropout::kData], Req, {
            mxnet_op::Kernel<mxnet_op::op_with_req<mshadow_op::mul, Req>, xpu>::Launch(
              s, gdata.Size(), gdata.dptr<DType>(), grad.dptr<DType>(), mask.dptr<DType>());
          });
#endif  // MXNET_USE_CUDNN == 1 & defined(__CUDACC__)
          return;
        }
        // broardcast mul
        TShape new_lshape, new_rshape, new_oshape;
        int ndim = BinaryBroadcastShapeCompact(grad.shape_,
                                               mask.shape_, gdata.shape_,
                                               &new_lshape, &new_rshape, &new_oshape);
        if (!ndim) {
          MXNET_ASSIGN_REQ_SWITCH(req[dropout::kData], Req, {
            mxnet_op::Kernel<mxnet_op::op_with_req<mshadow_op::mul, Req>, xpu>::Launch(
              s, gdata.Size(), gdata.dptr<DType>(), grad.dptr<DType>(), mask.dptr<DType>());
          });
        } else {
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
  TShape axes_;
#if MXNET_USE_CUDNN == 1
  bool cudnn_off_;
  Context ctx_;
  cudnnDataType_t dtype_;
  cudnnDropoutDescriptor_t dropout_desc_;
  bool init_cudnn_;
  uint64_t seed_ = 17 + rand() % 4096;  // NOLINT(runtime/threadsafe_fn)
  size_t dropout_state_byte_, dropout_reserve_byte_;
  Storage::Handle dropout_states_, reserve_space_;
  cudnnTensorDescriptor_t x_desc_, y_desc_, dx_desc_, dy_desc_;
#endif  // MXNET_USE_CUDNN == 1
};  // class DropoutOp

static OpStatePtr CreateDropoutState(const nnvm::NodeAttrs &attrs,
                                     const Context ctx,
                                     const std::vector<TShape> &in_shapes,
                                     const std::vector<int> &in_types) {
  const DropoutParam& param = nnvm::get<DropoutParam>(attrs.parsed);
  OpStatePtr state;
  MSHADOW_REAL_TYPE_SWITCH(in_types[dropout::kData], DType, {
    if (ctx.dev_type == kGPU) {
      state = OpStatePtr::Create<DropoutOp<gpu, DType>>(param, ctx);
    } else {
      state = OpStatePtr::Create<DropoutOp<cpu, DType>>(param, ctx);
    }
    return state;
  });
  LOG(FATAL) << "should never reach here";
  return OpStatePtr();  // should never reach here
}

template<typename xpu>
void DropoutCompute(const OpStatePtr& state,
                    const OpContext& ctx,
                    const std::vector<TBlob>& inputs,
                    const std::vector<OpReqType>& req,
                    const std::vector<TBlob>& outputs) {
  MSHADOW_REAL_TYPE_SWITCH(inputs[0].type_flag_, DType, {
    DropoutOp<xpu, DType>& op = state.get_state<DropoutOp<xpu, DType>>();
    op.Forward(ctx, inputs, req, outputs);
  });
}

template<typename xpu>
void DropoutGradCompute(const OpStatePtr& state,
                        const OpContext& ctx,
                        const std::vector<TBlob>& inputs,
                        const std::vector<OpReqType>& req,
                        const std::vector<TBlob>& outputs) {
  CHECK_EQ(inputs.size(), 2U);
  CHECK_EQ(outputs.size(), 1);
  CHECK_EQ(req.size(), 1);
  std::vector<TBlob> out_grads(2);
  std::vector<TBlob> out_data(2);
  out_grads[dropout::kOut] = inputs[0];
  out_data[dropout::kMask] = inputs[1];

  MSHADOW_REAL_TYPE_SWITCH(inputs[0].type_flag_, DType, {
    DropoutOp<xpu, DType>& op = state.get_state<DropoutOp<xpu, DType>>();
    op.Backward(ctx, out_grads, out_data, req, outputs);
  });
}

}  // namespace op
}  // namespace mxnet
#endif  // MXNET_OPERATOR_NN_DROPOUT_INL_H_

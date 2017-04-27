/*!
 * Copyright (c) 2015 by Contributors
 * \file batch_norm.cc
 * \brief
 * \author Bing Xu, Chris Olivier
*/

#include "batch_norm-inl.h"
#include <nnvm/op_attr_types.h>
#if MXNET_USE_MKL2017 == 1
#include <mkl_memory.h>
#include "./mkl/mkl_memory-inl.h"
#include "./mkl/mkl_batch_norm-inl.h"
#endif  // MXNET_USE_MKL2017

/*! \brief inverse standard deviation <-> variance */
#define VARIANCE_TO_INVSTD(__var$,    __eps$)   (1.0/sqrt((__var$) + DType(__eps$)))
#define INVSTD_TO_VARIANCE(__invstd$, __eps$)   ((1.0 / ((__invstd$) * (__invstd$))) - (__eps$))

namespace mxnet {
namespace op {
namespace batchnorm {

template<typename DType>
class DeviceTensor3 {
  DeviceTensor3(const DeviceTensor3&) = delete;

 public:
  inline DeviceTensor3(const TBlob& blob, const size_t indexOfChannel)
    : dptr_(blob.dptr<DType>())
      , indexOfChannel_(indexOfChannel)
      , shape_(3) {
    if (indexOfChannel) {
      shape_[0] = 1;
      for (size_t i = 0; i < indexOfChannel_; ++i) {
        shape_[0] *= blob.shape_[i];
      }
    } else {
      shape_[0] = 0;
    }
    shape_[1] = blob.shape_[indexOfChannel_];
    shape_[2] = 1;
    for (size_t i = indexOfChannel_ + 1, n = blob.shape_.ndim(); i < n; ++i) {
      shape_[2] *= blob.shape_[i];
    }
  }

  inline size_t Size() const {
    size_t n = 1;
    for (int i = 0; i < 3; ++i) {
      n *= shape_[i];
    }
    return n;
  }

  inline size_t ChannelCount() const {
    return shape_[1];
  }

  inline size_t BatchSize() const {
    return shape_[0];
  }

  inline size_t SpatialSize() const {
    return shape_[2];
  }

  DType *dptr_;
  size_t indexOfChannel_;
  TShape shape_;
};

/*! \brief offset, given indices such as bn, channel, depth, row, column */
static inline index_t offset(const TShape& shape,
                             const size_t *indices,
                             const size_t indicesSize) {
  const size_t dim = shape.ndim();
  size_t offset = 0;
  for (size_t i = 0; i < dim; ++i) {
    offset *= shape[i];
    if (indicesSize > i) {
      offset += indices[i];
    }
  }
  return offset;
}

/*! \brief Fast-foreach when you don't care about the position other than channel */
template<typename DType, typename OnData>
static inline void ForEachFast(const DeviceTensor3<DType> &tensor,
                               const size_t channel,
                               OnData onData) {
  const size_t num        = tensor.BatchSize();
  const size_t matrixSize = tensor.SpatialSize();

  size_t indices[2] = {0, channel};

  for (size_t batchItem = 0; batchItem < num; ++batchItem) {
    indices[0] = batchItem;
    DType *data = tensor.dptr_ + offset(tensor.shape_, &indices[0],
                                        sizeof(indices)/sizeof(indices[0]));
    for (size_t i = 0; i < matrixSize; ++i) {
      onData(data++);
    }
  }
}

/*! \brief Fast-foreach when you don't care about the position other than channel */
template<typename DType, typename OnData>
static inline void ForEachFast(const DeviceTensor3<DType> &in_data,
                               const DeviceTensor3<DType> &out_data,
                               const size_t channel,
                               OnData onData) {
  const size_t num        = in_data.BatchSize();
  const size_t matrixSize = in_data.SpatialSize();

  size_t indices[2] = {0, channel};

  for (size_t batchItem = 0; batchItem < num; ++batchItem) {
    indices[0] = batchItem;
    const size_t off = offset(in_data.shape_, &indices[0], sizeof(indices)/sizeof(indices[0]));
    const DType *data = in_data.dptr_ + off;
    DType *odata = out_data.dptr_ + off;
    for (size_t i = 0; i < matrixSize; ++i) {
      onData(data++, odata++);
    }
  }
}

/*! \brief Fast-foreach when you don't care about the position other than channel */
template<typename DType, typename OnData>
static inline void ForEachFast(const DeviceTensor3<DType>& tensor,
                               OnData onData) {
  const size_t num        = tensor.BatchSize();
  const size_t channels   = tensor.ChannelCount();
  const size_t matrixSize = tensor.SpatialSize();

  for (size_t batchItem = 0; batchItem < num; ++batchItem) {
#pragma openmp for
    for (size_t channel = 0; channel < channels; ++channel) {
      size_t indices[2] = { batchItem, channel };
      const size_t off = offset(tensor.shape_, &indices[0], sizeof(indices)/sizeof(indices[0]));
      const DType *inData = tensor.dptr_ + off;
      for (size_t i = 0; i < matrixSize; ++i) {
        onData(channel, inData++);
      }
    }
  }
}

/*! \brief Fast-foreach when you don't care about the position other than channel */
template<typename DType, typename OnData>
static inline void ForEachFast(const DeviceTensor3<DType>& in_data,
                               const DeviceTensor3<DType>& out_data,
                               OnData onData) {
  const size_t num        = in_data.BatchSize();
  const size_t channels   = in_data.ChannelCount();
  const size_t matrixSize = in_data.SpatialSize();

  for (size_t batchItem = 0; batchItem < num; ++batchItem) {
#pragma omp parallel for
    for (size_t channel = 0; channel < channels; ++channel) {
      size_t indices[2] = { batchItem, channel };
      const size_t off = offset(in_data.shape_, &indices[0], sizeof(indices)/sizeof(indices[0]));
      const DType *inData = in_data.dptr_ + off;
      DType *outData = out_data.dptr_ + off;
      for (size_t i = 0; i < matrixSize; ++i) {
        onData(channel, inData++, outData++);
      }
    }
  }
}

/*! \brief Compute the mean of each input channel */
template<typename DType>
static inline void ComputeMean(const DeviceTensor3<DType> &tensor,
                               DType *save_mean) {
  const size_t channelCount = tensor.ChannelCount();

  for (size_t i = 0; i < channelCount; ++i) {
    save_mean[i] = 0;
  }

  ForEachFast(tensor,
              [&save_mean](const size_t channel, const DType *in_data){
                save_mean[channel] += *in_data;
              });

  const size_t itemCount = tensor.Size() / channelCount;
  for (size_t i = 0, n = channelCount; i < n; ++i) {
    save_mean[i] /= itemCount;
  }
}

static inline bool IsWriting(const OpReqType ort) {
  return ort == kWriteTo || ort == kWriteInplace;
}

/*! \brief Compute the variance of each input channel, as well as update moving mean/variants */
template<typename DType>
static inline void ComputeVariance(const DeviceTensor3<DType> &tensor,
                                   const DType *mean_data,
                                   const DType eps,
                                   const TShape &oshape,
                                   DType *save_std) {
  const size_t channels   = tensor.ChannelCount();
  for (size_t i = 0; i < channels; ++i) {
    save_std[i] = 0;
  }
  ForEachFast(tensor,
              [&save_std, &mean_data](const index_t channel, const DType *current_in_data) {
                const DType mean = mean_data[channel];
                save_std[channel] += (*current_in_data - mean) * (*current_in_data - mean);
              });

  const size_t itemCount = tensor.Size() / channels;
#pragma omp parallel for
  for (size_t channel = 0; channel < channels; ++channel) {
    const DType sum = save_std[channel];

    DType invstd;
    if (sum == 0 && eps == 0.0) {
      // Nobody likes to divide by zero
      invstd = 0;
    } else {
      const DType variance = sum/itemCount;
      invstd = VARIANCE_TO_INVSTD(variance, eps);
    }
    save_std[channel] = invstd;
  }
}

}  // namespace batchnorm

/*! \brief Forward CPU */
template <typename xpu, typename DType, typename AccReal>
void BatchNormOp<xpu, DType, AccReal>::DoForward(mshadow::Stream<cpu> *stream,
                                                 const OpContext &ctx,
                                                 const std::vector<TBlob> &in_data,
                                                 const std::vector<OpReqType> &req,
                                                 const std::vector<TBlob> &out_data,
                                                 const std::vector<TBlob> &aux_states) {
  // Input
  batchnorm::DeviceTensor3<DType> inputData(in_data[batchnorm::kData], 1);
  const TBlob &weights         = in_data[batchnorm::kGamma];
  const TBlob &bias            = in_data[batchnorm::kBeta];

  // Aux (Moving)
  const TBlob &runningMean     = aux_states[batchnorm::kMovingMean];
  const TBlob &runningVariance = aux_states[batchnorm::kMovingVar];

  // Output
  batchnorm::DeviceTensor3<DType> outputData(out_data[batchnorm::kOut], 1);
  const TBlob &meanVector      = out_data[batchnorm::kMean];
  const TBlob &varianceVector  = out_data[batchnorm::kVar];

  DType *mean = meanVector.dptr<DType>();
  DType  *var = varianceVector.dptr<DType>();

  if (ctx.is_train && !param_.use_global_stats) {
    const TShape stride(2);

    // compute mean per input
    ComputeMean(inputData, meanVector.dptr<DType>());

    // compute variance per input
    ComputeVariance(inputData,
                    meanVector.dptr<DType>(),
                    static_cast<DType>(param_.eps),
                    varianceVector.shape_,
                    varianceVector.dptr<DType>());
  } else {
    const DType *rm = runningMean.dptr<DType>();
    const DType *rv = runningVariance.dptr<DType>();

    for (size_t i = 0, n = inputData.shape_[1]; i < n; ++i) {
      mean[i] = rm[i];
      var[i]  = VARIANCE_TO_INVSTD(rv[i], param_.eps);
    }
  }

  // compute output
  DType          *w = weights.dptr<DType>();
  const DType    *b = bias.dptr<DType>();

  // optionally, keep weights fixed at 1
  if (param_.fix_gamma) {
    for (size_t i =0, n = weights.Size(); i < n; ++i) {
      w[i] = DType(1);
    }
  }

  if (req[batchnorm::kData] == kWriteTo || req[batchnorm::kData] == kWriteInplace) {
    ForEachFast(inputData, outputData,
                [w, b, mean, var](const size_t channel, const DType *in_data, DType *out_data) {
                  *out_data = static_cast<DType>(
                    ((*in_data - mean[channel]) * var[channel]) * w[channel] + b[channel]);});
  }

  // Convert back to "real" variance in order to be consistent
  // with the original operator
  if (ctx.is_train && !param_.use_global_stats) {
    for (size_t i = 0, n = inputData.shape_[1]; i < n; ++i) {
      var[i] = INVSTD_TO_VARIANCE(var[i], param_.eps);
    }
  }
}

template <typename xpu, typename DType, typename AccReal>
void BatchNormOp<xpu, DType, AccReal>::DoBackward(mshadow::Stream<cpu> *stream,
                                                  const OpContext &ctx,
                                                  const std::vector<TBlob> &out_grad,
                                                  const std::vector<TBlob> &in_data,
                                                  const std::vector<TBlob> &out_data,
                                                  const std::vector<OpReqType> &req,
                                                  const std::vector<TBlob> &in_grad,
                                                  const std::vector<TBlob> &aux_states) {
  // Input Data
  batchnorm::DeviceTensor3<DType> inputData(in_data[batchnorm::kData], 1);
  const TBlob &weights   = in_data[batchnorm::kGamma];

  // Input Grad
  batchnorm::DeviceTensor3<DType> gradIn(in_grad[batchnorm::kData], 1);
  const TBlob &gradWeight = in_grad[batchnorm::kGamma];
  const TBlob &gradBias   = in_grad[batchnorm::kBeta];

  // Aux (Moving)
  const TBlob &runningMean = aux_states[batchnorm::kMovingMean];
  const TBlob &runningVariance = aux_states[batchnorm::kMovingVar];

  // Output
  batchnorm::DeviceTensor3<DType> gradOut(out_grad[batchnorm::kOut], 1);
  const TBlob &saveMean = out_data[batchnorm::kMean];
  const TBlob &saveStd  = out_data[batchnorm::kVar];

  const size_t channelCount = inputData.shape_[1];
  const size_t itemCount    = inputData.Size() / channelCount;

  // Avoid multiple dptr() call within the channel loop
  DType *runningMeanDataPtr = runningMean.dptr<DType>();
  DType *runningVarDataPtr  = runningVariance.dptr<DType>();
  DType *saveMeanDataPtr = saveMean.dptr<DType>();
  DType *saveVarianceDataPtr = saveStd.dptr<DType>();
  DType *gradWeightData = gradWeight.dptr<DType>();
  DType *gradBiasData = gradBias.dptr<DType>();

#pragma omp parallel for
  for (int channel = 0; channel < static_cast<int>(channelCount); ++channel) {
    DType *weight = weights.dptr<DType>();
    const DType w = weight ? weight[channel] : DType(1);
    DType mean, invstd;
    if (ctx.is_train && !param_.use_global_stats) {
      mean = saveMeanDataPtr[channel];
      const DType variance = saveVarianceDataPtr[channel];
      invstd = VARIANCE_TO_INVSTD(variance, param_.eps);

      // update running averages
      runningMeanDataPtr[channel] = runningMeanDataPtr[channel] * param_.momentum
                                    + mean * (DType(1) - param_.momentum);

      runningVarDataPtr[channel] = runningVarDataPtr[channel] * param_.momentum
                                   + variance * (DType(1) - param_.momentum);

    } else {
      mean = runningMeanDataPtr[channel];
      invstd = VARIANCE_TO_INVSTD(runningVarDataPtr[channel], param_.eps);
    }

    // sumGradOut over all gradOutput in feature plane
    DType sumGradOut = 0;
    ForEachFast(gradOut, channel,
                [&sumGradOut](const DType *gradOut_data) {
                  sumGradOut += *gradOut_data;
                });

    // dot product of the Q(X) and gradOuput
    DType dotp = 0;
    ForEachFast(inputData, gradOut, channel,
                [&dotp, mean](const DType *thisInputData, const DType *gradOut_data) {
                  dotp += (*thisInputData - mean) * (*gradOut_data);
                });

    if (gradIn.shape_.ndim()) {  // if there's a grad input
      if (ctx.is_train && !param_.use_global_stats) {
        // when in training mode
        // Q(X) = X - E[x] ; i.e. input centered to zero mean
        // Y = Q(X) / σ    ; i.e. BN output before weight and bias
        // dL/dX = (Q(dL/dY) - dot(Y, dL/dY) * Y) / σ * w

        // projection of gradOutput on to output scaled by std
        const DType k = dotp * invstd * invstd / itemCount;
        ForEachFast(inputData, gradIn, channel,
                    [&mean, &k](const DType *in_data, DType *gradIn_data) {
                      *gradIn_data = (*in_data - mean) * k;
                    });

        const DType iw = invstd * w;
        const DType gradMean = sumGradOut / itemCount;
        ForEachFast(gradOut, gradIn, channel,
                    [iw, gradMean](const DType *gradOut_data, DType *gradIn_data) {
                      *gradIn_data = (*gradOut_data - gradMean - *gradIn_data) * iw;
                    });
      } else {
        // when in evaluation mode
        // Q(X) = X - running_mean  ; i.e. input centered to zero mean
        // Y = Q(X) / running_std    ; i.e. BN output before weight and bias
        // dL/dX = w / running_std
        const DType iw = invstd * w;
        ForEachFast(gradOut, gradIn, channel,
                    [iw](const DType *gradOut_data, DType *gradIn_data) {
                      *gradIn_data = *gradOut_data * iw;
                    });
      }
    }

    // May want to make this a param eventually
    const DType scale = 1.0f;

    if (batchnorm::IsWriting(req[batchnorm::kGamma])) {
      if (!param_.fix_gamma) {
        gradWeightData[channel] = scale * dotp * invstd;
      } else {
        gradWeightData[channel] = DType(0);
      }
    }

    if (batchnorm::IsWriting(req[batchnorm::kBeta])) {
      gradBiasData[channel] = scale * sumGradOut;
    }
  }
}


template<>
Operator *CreateOp<cpu>(BatchNormParam param, int dtype) {
#if MXNET_USE_MKL2017 == 1
  return new MKLBatchNormOp<cpu, float>(param);
#endif
  Operator *op = nullptr;
  MSHADOW_REAL_TYPE_SWITCH_EX(dtype,
                           DType,
                           AccReal,
                           { op = new BatchNormOp<cpu, DType, AccReal>(param); });
  return op;
}

// DO_BIND_DISPATCH comes from operator_common.h
Operator *BatchNormProp::CreateOperatorEx(Context ctx, std::vector<TShape> *in_shape,
                                          std::vector<int> *in_type) const {
  std::vector<TShape> out_shape, aux_shape;
  std::vector<int> out_type, aux_type;
  CHECK(InferType(in_type, &out_type, &aux_type));
  CHECK(InferShape(in_shape, &out_shape, &aux_shape));
  DO_BIND_DISPATCH(CreateOp, param_, (*in_type)[0]);
}

DMLC_REGISTER_PARAMETER(BatchNormParam);

MXNET_REGISTER_OP_PROPERTY(BatchNorm, BatchNormProp)
  .describe(R"code(Batch normalization.

Normalizes a data batch by mean and variance, and applies a scale ``gamma`` as
well as offset ``beta``.

Assume the input has more than one dimension and we normalize along axis 1.
We first compute the mean and variance along this axis:

.. math::

  data\_mean[i] = mean(data[:,i,:,...]) \\
  data\_var[i] = var(data[:,i,:,...])

Then compute the normalized output, which has the same shape as input, as following:

.. math::

  out[:,i,:,...] = \frac{data[:,i,:,...] - data\_mean[i]}{\sqrt{data\_var[i]+\epsilon}} * gamma[i] + beta[i]

Both *mean* and *var* returns a scalar by treating the input as a vector.

Assume the input has size *k* on axis 1, then both ``gamma`` and ``beta``
have shape *(k,)*. If ``output_mean_var`` is set to be true, then outputs both ``data_mean`` and
``data_var`` as well, which are needed for the backward pass.

Besides the inputs and the outputs, this operator accepts two auxiliary
states, ``moving_mean`` and ``moving_var``, which are *k*-length
vectors. They are global statistics for the whole dataset, which are updated
by::

  moving_mean = moving_mean * momentum + data_mean * (1 - momentum)
  moving_var = moving_var * momentum + data_var * (1 - momentum)

If ``use_global_stats`` is set to be true, then ``moving_mean`` and
``moving_var`` are used instead of ``data_mean`` and ``data_var`` to compute
the output. It is often used during inference.

Both ``gamma`` and ``beta`` are learnable parameters. But if ``fix_gamma`` is true,
then set ``gamma`` to 1 and its gradient to 0.

)code" ADD_FILELINE)
  .add_argument("data", "NDArray-or-Symbol", "Input data to batch normalization")
  .add_argument("gamma", "NDArray-or-Symbol", "gamma array")
  .add_argument("beta", "NDArray-or-Symbol", "beta array")
  .add_arguments(BatchNormParam::__FIELDS__());

NNVM_REGISTER_OP(BatchNorm)
.set_attr<nnvm::FSetInputVarAttrOnCompose>(
  "FSetInputVarAttrOnCompose",
  [](const nnvm::NodeAttrs& attrs, nnvm::NodePtr var, const int index) {
    if (var->attrs.dict.find("__init__") != var->attrs.dict.end()) return;
    if (index == 3) {
      var->attrs.dict["__init__"] = "[\"zero\", {}]";
    } else if (index == 4) {
      var->attrs.dict["__init__"] = "[\"one\", {}]";
    }
  });

}  // namespace op
}  // namespace mxnet


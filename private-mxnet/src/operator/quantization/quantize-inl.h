/*!
 *  Copyright (c) 2017 by Contributors
 * \file quantize-inl.h
 * \brief implementation of quantize operation
 */
#ifndef MXNET_OPERATOR_CONTRIB_QUANTIZE_INL_H_
#define MXNET_OPERATOR_CONTRIB_QUANTIZE_INL_H_

#include <mxnet/operator_util.h>
#include <vector>
#include <limits>
#include "../elemwise_op_common.h"
#include "../mshadow_op.h"
#include "../mxnet_op.h"
#include "./quantization_utils.h"

namespace mxnet {
namespace op {

struct QuantizeParam : public dmlc::Parameter<QuantizeParam> {
  int   out_type;
  int   shift_exponent;
  DMLC_DECLARE_PARAMETER(QuantizeParam) {
    DMLC_DECLARE_FIELD(out_type)
    .add_enum("int8", mshadow::kInt8)
    .add_enum("uint8", mshadow::kUint8)
    .add_enum("int32", mshadow::kInt32)
    .set_default(mshadow::kInt8)
    .describe("Output data type.");
    DMLC_DECLARE_FIELD(shift_exponent)
    .set_default(0)
    .describe("Number of shift for Dynamic Fixed Point (MKL).");
  }
};

struct quantize {
  template<typename DstDType, typename SrcDType>
  MSHADOW_XINLINE static void Map(int i, DstDType *out, float *omin_range,
                                  float *omax_range, const SrcDType *in,
                                  const float *imin_range, const float *imax_range) {
    using mshadow::red::limits::MinValue;
    using mshadow::red::limits::MaxValue;
    float scale = (MaxValue<DstDType>() - MinValue<DstDType>()) /
                  (*imax_range - *imin_range);
    out[i] = static_cast<DstDType>((in[i] - *imin_range) * scale + 0.5) +
        MinValue<DstDType>();
    *omin_range = *imin_range;
    *omax_range = *imax_range;
  }
};


// keep zero-center
struct quantize_v2 {
  template<typename DstDType, typename SrcDType>
  MSHADOW_XINLINE static void Map(int i, DstDType *out, float *omin_range,
                                  float *omax_range, const SrcDType *in,
                                  const float *imin_range, const float *imax_range) {
    float real_range = MaxAbs(*imin_range, *imax_range);
    float quantized_range = MinAbs(MaxValue<DstDType>(), MinValue<DstDType>());
    float scale = quantized_range / real_range;
    SrcDType x = in[i];
    out[i] = static_cast<DstDType>(
        Sign(x) * Min(Abs(x) * scale + 0.5f, quantized_range));
    *omin_range = -real_range;
    *omax_range =  real_range;
  }
};

template<typename xpu>
void QuantizeCompute(const nnvm::NodeAttrs& attrs,
                     const OpContext& ctx,
                     const std::vector<TBlob>& inputs,
                     const std::vector<OpReqType>& req,
                     const std::vector<TBlob>& outputs) {
  using namespace mshadow;
  using namespace mxnet_op;
  Stream<xpu> *s = ctx.get_stream<xpu>();

  const QuantizeParam& param = nnvm::get<QuantizeParam>(attrs.parsed);
  typedef float  SrcDType;
  typedef int8_t DstDType;
  Kernel<quantize_v2, xpu>::Launch(s, outputs[0].Size(),
    outputs[0].dptr<DstDType>(), outputs[1].dptr<float>(), outputs[2].dptr<float>(),
    inputs[0].dptr<SrcDType>(), inputs[1].dptr<float>(), inputs[2].dptr<float>());
}

inline bool QuantizeShape(const nnvm::NodeAttrs& attrs,
                          std::vector<TShape> *in_attrs,
                          std::vector<TShape> *out_attrs) {
  const QuantizeParam& param = nnvm::get<QuantizeParam>(attrs.parsed);
  CHECK_EQ(in_attrs->size(), 3U);
  CHECK_EQ(out_attrs->size(), 3U);

  CHECK(!shape_is_none(in_attrs->at(0)));
  for (size_t i = 1; i < 3; ++i) {
    CHECK(shape_is_scalar(in_attrs->at(i)));
  }

  SHAPE_ASSIGN_CHECK(*out_attrs, 0, in_attrs->at(0));
  SHAPE_ASSIGN_CHECK(*out_attrs, 1, TShape{1});
  SHAPE_ASSIGN_CHECK(*out_attrs, 2, TShape{1});
  return true;
}

inline bool QuantizeType(const nnvm::NodeAttrs& attrs,
                         std::vector<int> *in_attrs,
                         std::vector<int> *out_attrs) {
  const QuantizeParam& param = nnvm::get<QuantizeParam>(attrs.parsed);

  // std::cout << "test param: " << param.out_type << std::endl;

  auto out_type = param.out_type;
  if (attrs.scalars.size() > 0)
  {
    // std::cout << "test param: " << attrs.scalars[attrs.scalars.size() - 1] << std::endl;
    out_type = attrs.scalars[attrs.scalars.size() - 1];
  }

  CHECK_EQ(in_attrs->size(), 3U);
  CHECK_EQ(out_attrs->size(), 3U);
  CHECK_EQ((*in_attrs)[0], mshadow::kFloat32)
    << "`quantize` only supports float32 input for now";
  CHECK_EQ((*in_attrs)[1], mshadow::kFloat32)
    << "the second input of `quantize` should be a tensor with type of float";
  CHECK_EQ((*in_attrs)[2], mshadow::kFloat32)
    << "the third input of `quantize` should be a tensor with type of float";
  TYPE_ASSIGN_CHECK(*out_attrs, 0, out_type);
  TYPE_ASSIGN_CHECK(*out_attrs, 1, mshadow::kFloat32);
  TYPE_ASSIGN_CHECK(*out_attrs, 2, mshadow::kFloat32);
  return (*in_attrs)[0] != -1;
}

}  // namespace op
}  // namespace mxnet
#endif  // MXNET_OPERATOR_CONTRIB_QUANTIZE_INL_H_

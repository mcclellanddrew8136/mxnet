/*!
 * Copyright (c) 2016 by Contributors
 * \file caffe_loss-inl.h
 * \brief Caffe Operator
 * \author Haoran Wang 
*/
#ifndef PLUGIN_CAFFE_CAFFE_LOSS_INL_H_
#define PLUGIN_CAFFE_CAFFE_LOSS_INL_H_

#include <caffe/layer.hpp>
#include <caffe/proto/caffe.pb.h>
#include <caffe/blob.hpp>

#include <dmlc/logging.h>
#include <dmlc/parameter.h>
#include <mxnet/operator.h>

#include <map>
#include <vector>
#include <string>
#include <utility>
#include <iostream>
#include <exception>

#include "../../src/operator/operator_common.h"

#include "caffe_common.h"
#include "caffe_operator_util.h"
#include "caffe_stream.h"
#include "caffe_fieldentry.h"
#include "caffe_blob.h"

namespace mxnet {
namespace op {

struct CaffeLossParam : public dmlc::Parameter<CaffeLossParam> {
  caffe::LayerParameter prototxt;
  int in_num, out_num;
  float grad_scale;
  caffe::Layer<float> *caffe_op;

  DMLC_DECLARE_PARAMETER(CaffeLossParam) { DMLC_DECLARE_FIELD(prototxt).set_default("layer{}")
    .describe("Caffe's layer parameter");
    DMLC_DECLARE_FIELD(in_num).set_range(0, 100).set_default(2)
    .describe("Operator input number");
    DMLC_DECLARE_FIELD(out_num).set_range(0, 100).set_default(1)
    .describe("Operator output number");
    DMLC_DECLARE_FIELD(grad_scale)
    .set_default(1.0f)
    .describe("Scale the gradient by a float factor (a.k.a weight of this loss).");
  }
};


/**
 * \brief this is the implementation of caffe operator in caffe.
 * \tparam xpu the device that the op will be executed on.
 */
template<typename xpu>
class CaffeLoss : public Operator {
 public:
  explicit CaffeLoss(CaffeLossParam p):param_(p),
                                       caffeOp_(p.caffe_op) {
    InitCaffeBlobs(bot_, param_.in_num);
    InitCaffeBlobs(top_, param_.out_num);
    flags_.resize(param_.in_num);
  }

  ~CaffeLoss() {
    DelCaffeBlobs(bot_, param_.in_num);
    DelCaffeBlobs(top_, param_.out_num);
  }

  virtual void Forward(const OpContext &ctx,
                       const std::vector<TBlob> &in_data,
                       const std::vector<OpReqType> &req,
                       const std::vector<TBlob> &out_data,
                       const std::vector<TBlob> &aux_args) {
    // Set mode before forward
    ::mxnet::CaffeMode::SetMode<xpu>();
    using ::caffe::Blob;
    using std::vector;
    using namespace mshadow;
    using namespace mshadow::expr;
    for (size_t i = 0; i < req.size(); ++i)
      CHECK_EQ(req[i], kWriteTo);

    CHECK_EQ(in_data.size(), param_.in_num);
    CHECK_EQ(out_data.size(), param_.out_num);

    Stream<xpu> *s = ctx.get_stream<xpu>();
#if defined(__CUDACC__)
    // TODO(Haoran): when need cublas handle in stream?
    CHECK_EQ(s->blas_handle_ownership_, Stream<xpu>::OwnHandle)
          << "Must init CuBLAS handle in stream";
#endif  // __CUDACC__

    TBlob2CaffeBlob<xpu>(caffememtype::Data, bot_.begin(), in_data.begin(), param_.in_num);
    TBlob2CaffeBlob<xpu>(caffememtype::Data, top_.begin(), out_data.begin(), param_.out_num);

    caffeOp_->SetUp(bot_, top_);
    caffeOp_->Forward(bot_, top_);
  }

  virtual void Backward(const OpContext &ctx,
                        const std::vector<TBlob> &out_grad,
                        const std::vector<TBlob> &in_data,
                        const std::vector<TBlob> &out_data,
                        const std::vector<OpReqType> &req,
                        const std::vector<TBlob> &in_grad,
                        const std::vector<TBlob> &aux_args) {
    // Set mode before backward
    ::mxnet::CaffeMode::SetMode<xpu>();
    using namespace mshadow;
    using namespace mshadow::expr;
    CHECK_EQ(out_grad.size(), param_.out_num);
    for (size_t i = 0; i < param_.in_num; ++i)
      CHECK(req[i] != kAddTo) << "caffe doesn't accm diff on bottom data";
    CHECK(in_data.size() == param_.in_num);

    Stream<xpu> *s = ctx.get_stream<xpu>();
#if defined(__CUDACC__)
    // TODO(Haoran): when need cublas handle in stream?
    CHECK_EQ(s->blas_handle_ownership_, Stream<xpu>::OwnHandle)
          << "Must init CuBLAS handle in stream";
#endif  // __CUDACC__

    TBlob2CaffeBlob<xpu>(caffememtype::Grad, bot_.begin(), in_grad.begin(), param_.in_num);
    //TBlob2CaffeBlob<xpu>(caffememtype::Grad, top_.begin(), out_grad.begin(), param_.out_num);
    top_[0]->set_cpu_diff(&param_.grad_scale);

    // Set BP flag 
    for (size_t i = 0; i < param_.in_num; ++i)
      flags_[i] = req[i] != kNullOp;

    caffeOp_->Backward(top_, flags_, bot_);
  }

 private:
  CaffeLossParam param_;
  caffe::Layer<float> *caffeOp_;
  std::vector<caffe::Blob<float> *> bot_, top_;
  std::vector<bool> flags_;
};  // class CaffeLoss 

// Decalre Factory function, used for dispatch specialization
template<typename xpu>
Operator* CreateOp(CaffeLossParam param);

#if DMLC_USE_CXX11
class CaffeLossProp : public OperatorProperty {
 public:
  std::vector<std::string> ListArguments() const override {
    return {"data", "label"};
  }

  void Init(const std::vector<std::pair<std::string, std::string> >& kwargs) override {
    param_.Init(kwargs);
    CHECK_EQ(param_.out_num, 1);
    CHECK_EQ(param_.in_num, 2);

    // Fetch grad_scale from prototxt
    if ((param_.prototxt.loss_weight_size() > 0))
      param_.grad_scale = param_.prototxt.loss_weight(0);

    entry_ = CaffeOpInitRegistry::Get()->Find(param_.prototxt.type());
    param_.caffe_op = entry_->gen_f_(this->param_.prototxt);
  }

  std::map<std::string, std::string> GetParams() const override {
    return param_.__DICT__();
  }

  /*brief Set up caffe_op to infer output shape*/
  bool InferShape(std::vector<TShape> *in_shape,
                  std::vector<TShape> *out_shape,
                  std::vector<TShape> *aux_shape) const override {
    using namespace mshadow;
    using caffe::Blob;
    using std::vector;
    CHECK_GE(in_shape->size(), param_.in_num);
    // Initialize bottom & top blobs for caffe_op setup
    vector<Blob<float> *> bot_blobs, top_blobs;
    // Set OperatorParam input dims & caffe op input blobs
    for (size_t i = 0; i < param_.in_num; ++i) {
      TShape tshape = (*in_shape)[i];
      if (tshape.ndim() == 0) return false;
      auto blob_ptr = new Blob<float>();
      blob_ptr->Reshape(TShape2Vector(tshape));
      bot_blobs.push_back(blob_ptr);
    }

    // Set caffe op output blobs
    for (size_t i = 0; i < param_.out_num; ++i)
      top_blobs.push_back(new Blob<float>());

    param_.caffe_op->SetUp(bot_blobs, top_blobs);
    CHECK_EQ(in_shape->size(), param_.caffe_op->blobs().size() + param_.in_num);
    // Initialize out dims & out shapes
    out_shape->clear();
    for (auto blob : top_blobs) {
      TShape tshape = Vector2TShape(blob->shape());
      out_shape->push_back(tshape);
    }

    // Free caffe in & out blobs
    for (auto blob_ptr : bot_blobs)
      delete blob_ptr;
    for (auto blob_ptr : top_blobs)
      delete blob_ptr;

    return true;
  }

  OperatorProperty* Copy() const override {
    auto copy_prop = new CaffeLossProp();
    copy_prop->param_ = this->param_;
    return copy_prop;
  }

  std::string TypeString() const override {
    return "CaffeLoss";
  }

  std::vector<int> DeclareBackwardDependency(
    const std::vector<int> &out_grad,
    const std::vector<int> &in_data,
    const std::vector<int> &out_data) const override {
    std::vector<int> dep;
    dep.insert(dep.end(), in_data.begin(), in_data.end());
    dep.insert(dep.end(), out_data.begin(), out_data.end());
    return dep;
  }

  Operator* CreateOperator(Context ctx) const override;

 private:
  mutable CaffeLossParam param_;
  mutable CaffeOpInitEntry* entry_;
};  // class CaffeLossSymbol
#endif

}  // namespace op
}  // namespace mxnet
#endif  // PLUGIN_CAFFE_CAFFE_OPERATOR_INL_H_

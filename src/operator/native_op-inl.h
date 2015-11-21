/*!
 * Copyright (c) 2015 by Contributors
 * \file native_op-inl.h
 * \brief
 * \author Junyuan Xie
*/

#ifndef MXNET_OPERATOR_NATIVE_OP_INL_H_
#define MXNET_OPERATOR_NATIVE_OP_INL_H_
#include <dmlc/logging.h>
#include <dmlc/parameter.h>
#include <mxnet/operator.h>
#include <mxnet/c_api.h>
#include <mxnet/ndarray.h>
#include <map>
#include <vector>
#include <string>
#include <utility>
#include <sstream>
#include "./operator_common.h"

namespace mxnet {
namespace op {

struct NativeOpParam : public dmlc::Parameter<NativeOpParam> {
  void *info;
  bool need_top_grad;

  NativeOpInfo *pinfo;
  int num_inputs_, num_outputs_;
  DMLC_DECLARE_PARAMETER(NativeOpParam) {
    DMLC_DECLARE_FIELD(info);
    DMLC_DECLARE_FIELD(need_top_grad).set_default(true)
    .describe("Whether this layer needs out grad for backward. "
      "Should be false for loss layers.");
  }
};

template<typename xpu>
class NativeOp : public Operator {
 public:
  explicit NativeOp(NativeOpParam p) {
    this->param_ = p;
  }

  virtual void Forward(const OpContext &ctx,
                       const std::vector<TBlob> &in_data,
                       const std::vector<OpReqType> &req,
                       const std::vector<TBlob> &out_data,
                       const std::vector<TBlob> &aux_args) {
    using namespace mshadow;
    Stream<xpu> *s = ctx.get_stream<xpu>();

    std::vector<NDArray> in_ndarrs;
    std::vector<void*> in_handels;
    for (index_t i = 0; i < in_data.size(); ++i) {
      NDArray arr = NDArray(in_data[i], ctx.ctx.dev_id);
      in_ndarrs.push_back(arr);
      in_handels.push_back(&arr);
    }

    std::vector<NDArray> out_ndarrs;
    std::vector<void*> out_handels;
    for (index_t i = 0; i < out_data.size(); ++i) {
      NDArray arr = NDArray(out_data[i], ctx.ctx.dev_id);
      out_ndarrs.push_back(arr);
      out_handels.push_back(&arr);
    }

    s->Wait();
    param_.pinfo->forward(in_handels.size(), in_handels.data(),
                          out_handels.size(), out_handels.data(),
                          param_.pinfo->p_forward, &ctx);

  }

  virtual void Backward(const OpContext &ctx,
                        const std::vector<TBlob> &out_grad,
                        const std::vector<TBlob> &in_data,
                        const std::vector<TBlob> &out_data,
                        const std::vector<OpReqType> &req,
                        const std::vector<TBlob> &in_grad,
                        const std::vector<TBlob> &aux_args) {
    using namespace mshadow;
    Stream<xpu> *s = ctx.get_stream<xpu>();
    ptrs.clear();
    ndims.clear();
    shapes.clear();
    tags.clear();
    SyncVec(in_data, "in_data", s, 0);
    SyncVec(out_data, "out_data", s, 1);
    SyncVec(in_grad, "in_grad", s, 2);
    if (param_.need_top_grad) {
      SyncVec(out_grad, "out_grad", s, 3);
    }
    s->Wait();
    param_.pinfo->backward(ptrs.size(), ptrs.data(), ndims.data(), shapes.data(),
        tags.data(), param_.pinfo->p_backward);
    for (index_t i = 0; i < in_grad.size(); ++i) {
      CHECK_NE(req[i], kAddTo) << "NativeOp doesn't support AddTo for output";
      if (req[i] != kNullOp) {
        std::stringstream ss;
        ss << std::string("in_grad") << i;
        Copy(in_grad[i].FlatTo2D<xpu, real_t>(s),
             buffer_map[ss.str()].second, s);
      }
    }
    s->Wait();
    ctx.async_on_complete();
  }

 private:
  NativeOpParam param_;
  std::vector<real_t*> ptrs;
  std::vector<int> ndims;
  std::vector<unsigned*> shapes;
  std::vector<int> tags;
  std::map<std::string, std::pair<TShape, mshadow::Tensor<cpu, 2> > > buffer_map;

  virtual void SyncBuffer(const TBlob &tblob,
                          const std::string &name,
                          mshadow::Stream<xpu> *stream) {
    using namespace mshadow;
    std::map<std::string, std::pair<TShape, mshadow::Tensor<cpu, 2> > >::iterator buffer =
      buffer_map.find(name);
    if (buffer == buffer_map.end() || buffer->second.first != tblob.shape_) {
      if (buffer != buffer_map.end()) {
        FreeSpace<2, real_t>(&(buffer->second.second));
        buffer_map.erase(buffer);
      }
      buffer_map[name] =
        std::pair<TShape, Tensor<cpu, 2> >(tblob.shape_,
                                         NewTensor<cpu>(tblob.shape_.FlatTo2D(),
                                                        0.0f,
                                                        false));
      buffer = buffer_map.find(name);
    }
    Copy(buffer->second.second, tblob.FlatTo2D<xpu, real_t>(stream), stream);
  }

  virtual void SyncVec(const std::vector<TBlob> &vec,
                       const std::string &prefix,
                       mshadow::Stream<xpu> *stream,
                       int tag) {
    for (size_t i = 0; i < vec.size(); ++i) {
      std::stringstream name;
      name << prefix << i;
      SyncBuffer(vec[i], name.str(), stream);
      ptrs.push_back(buffer_map[name.str()].second.dptr_);
      ndims.push_back(vec[i].ndim());
      shapes.push_back(const_cast<index_t*>(vec[i].shape_.data()));
      tags.push_back(tag);
    }
  }

  virtual ExecType exec_type() const {
    // Use asynchronize complete notification
    return kAsync;
 }
};  // NativeOp

template<typename xpu>
Operator* CreateOp(NativeOpParam param);

#if DMLC_USE_CXX11
class NativeOpProp : public OperatorProperty {
 public:
  std::vector<std::string> ListArguments() const override {
    char ** args = NULL;
    param_.pinfo->list_arguments(&args, param_.pinfo->p_list_arguments);
    std::vector<std::string> ret;
    for (int i = 0; args[i] != NULL; ++i) {
      ret.push_back(args[i]);
    }
    return ret;
  }

  std::vector<std::string> ListOutputs() const override {
    char ** args = NULL;
    param_.pinfo->list_outputs(&args, param_.pinfo->p_list_outputs);
    std::vector<std::string> ret;
    for (int i = 0; args[i] != NULL; ++i) {
      ret.push_back(args[i]);
    }
    return ret;
  }

  int NumOutputs() const override {
    return param_.num_outputs_;
  }

  void Init(const std::vector<std::pair<std::string, std::string> >& kwargs) override {
    param_.Init(kwargs);
    for (auto iter = kwargs.begin(); iter != kwargs.end(); ++iter) {
      if (iter->first == "info") {
        sscanf(iter->second.c_str(), "%p", &param_.pinfo);
      }
    }
    param_.num_inputs_ = ListArguments().size();
    param_.num_outputs_ = ListOutputs().size();
  }

  std::map<std::string, std::string> GetParams() const override {
    return param_.__DICT__();
  }


  bool InferShape(std::vector<TShape> *in_shape,
                  std::vector<TShape> *out_shape,
                  std::vector<TShape> *aux_shape) const override {
    std::vector<unsigned*> shapes;
    std::vector<int> ndims;
    for (auto iter = in_shape->begin(); iter != in_shape->end(); ++iter) {
      shapes.push_back(iter->data());
      ndims.push_back(iter->ndim());
    }
    shapes.resize(param_.num_inputs_+param_.num_outputs_);
    ndims.resize(param_.num_inputs_+param_.num_outputs_);
    param_.pinfo->infer_shape(shapes.size(), ndims.data(), shapes.data(),
          param_.pinfo->p_infer_shape);
    for (unsigned i = 0; i < in_shape->size(); ++i) {
      SHAPE_ASSIGN_CHECK(*in_shape, i, TShape(shapes[i], shapes[i]+ndims[i]));
    }
    out_shape->clear();
    for (unsigned i = param_.num_inputs_; i < shapes.size(); ++i) {
      out_shape->push_back(TShape(shapes[i], shapes[i]+ndims[i]));
    }
    return true;
  }

  OperatorProperty* Copy() const override {
    NativeOpProp *prop_sym = new NativeOpProp();
    prop_sym->param_ = this->param_;
    return prop_sym;
  }

  std::string TypeString() const override {
    return "_Native";
  }

  std::vector<int> DeclareBackwardDependency(
    const std::vector<int> &out_grad,
    const std::vector<int> &in_data,
    const std::vector<int> &out_data) const override {
    std::vector<int> deps;
    if (param_.need_top_grad) {
      deps.insert(deps.end(), out_grad.begin(), out_grad.end());
    }
    deps.insert(deps.end(), in_data.begin(), in_data.end());
    deps.insert(deps.end(), out_data.begin(), out_data.end());
    return deps;
  }

  std::vector<std::pair<int, void*> > BackwardInplaceOption(
    const std::vector<int> &out_grad,
    const std::vector<int> &in_data,
    const std::vector<int> &out_data,
    const std::vector<void*> &in_grad) const override {
    return {};
  }

  Operator* CreateOperator(Context ctx) const override;

 private:
  NativeOpParam param_;
};  // class PythonProp
#endif  // DMLC_USE_CXX11
}  // namespace op
}  // namespace mxnet
#endif  // MXNET_OPERATOR_NATIVE_OP_INL_H_

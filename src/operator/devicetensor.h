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
 * Copyright (c) 2018 by Contributors
 * \file device-tensor.h
 * \brief  bilinear upsample operator
 * \author Hang Zhang
 */
#ifndef MXNET_OPERATOR_DEVICETENSOR_H_
#define MXNET_OPERATOR_DEVICETENSOR_H_

#include <dmlc/logging.h>
#include <dmlc/parameter.h>
#include <mxnet/operator.h>
#include <mxnet/ndarray.h>
#include <vector>
#include <string>
#include <utility>
#include "../ndarray/ndarray_function.h"
#include "./operator_common.h"
#include "./mxnet_op.h"
#include "./mshadow_op.h"

namespace mxnet {
namespace op {

template<typename DType, int Dim>
struct DeviceTensor {
 public:
  MSHADOW_XINLINE DeviceTensor(DType *p, const int *size)
    : dptr_(p) {
    for (int i = 0; i < Dim; ++i) {
      size_[i] = size ? size[i] : 0;
    }
  }

  MSHADOW_XINLINE unsigned getSize(const int i) const {
    assert(i < Dim);
    return size_[i];
  }

  MSHADOW_XINLINE int numElements() const {
    int n = 1;
    for (int i = 0; i < Dim; ++i) {
      n *= size_[i];
    }
    return n;
  }

  MSHADOW_XINLINE DeviceTensor<DType, Dim-1> select(const size_t x) const {
    assert(Dim > 1);
    int offset = x;
    for (int i = 1; i < Dim; ++i) {
      offset *= size_[i];
    }
    DeviceTensor<DType, Dim-1> tensor(dptr_ + offset, nullptr);
    for (int i = 0; i < Dim - 1; ++i) {
      tensor.size_[i] = this->size_[i+1];
    }
    return tensor;
  }

  MSHADOW_XINLINE DeviceTensor<DType, Dim-1> operator[](const size_t x) const {
    assert(Dim > 1);
    int offset = x;
    for (int i = 1; i < Dim; ++i) {
      offset *= size_[i];
    }
    DeviceTensor<DType, Dim-1> tensor(dptr_ + offset, nullptr);
    for (int i = 0; i < Dim - 1; ++i) {
      tensor.size_[i] = this->size_[i+1];
    }
    return tensor;
  }

  MSHADOW_XINLINE size_t InnerSize() const {
    assert(Dim >= 3);
    size_t sz = 1;
    for (size_t i = 2; i < Dim; ++i) {
      sz *= size_[i];
    }
    return sz;
  }

  MSHADOW_XINLINE size_t ChannelCount() const {
    assert(Dim >= 3);
    return size_[1];
  }

  MSHADOW_XINLINE DType* data_ptr() const {
    return dptr_;
  }

  DType *dptr_;
  int size_[Dim];
};

template<typename DType>
struct DeviceTensor<DType, 1> {
  MSHADOW_XINLINE DeviceTensor(DType *p, const int *size)
    : dptr_(p) {
    size_[0] = size ? size[0] : 0;
  }

  MSHADOW_XINLINE unsigned getSize(const int i) const {
    assert(i == 0);
    return size_[0];
  }

  MSHADOW_XINLINE int numElements() const {
    return size_[0];
  }

  MSHADOW_XINLINE DType &operator[](const size_t x) const {
      return *(dptr_ + x);
  }

  MSHADOW_XINLINE DType* data_ptr() const {
    return dptr_;
  }

  DType *dptr_;
  int size_[1];
};

template<typename DType, int Dim>
static DeviceTensor<DType, Dim> devicetensor(const TBlob &blob) {
  DType *data = blob.dptr<DType>();
  const int inDim = blob.shape_.ndim();
  assert(inDim == Dim);
  DeviceTensor<DType, Dim> tensor(data, nullptr);
  for (int i = 0; i < Dim; ++i) {
    tensor.size_[i] = blob.size(i);
  }
  return tensor;
}

}  // namespace op
}  // namespace mxnet
#endif  // MXNET_OPERATOR_DEVICETENSOR_H_

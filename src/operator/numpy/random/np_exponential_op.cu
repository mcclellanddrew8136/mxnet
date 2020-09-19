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
 * \file np_exponential_op.cu
 * \brief Operator for numpy sampling from exponential distributions
 */

#include "./np_exponential_op.h"

namespace mxnet {
namespace op {

NNVM_REGISTER_OP(_npi_exponential)
<<<<<<< HEAD
    .set_attr<FCompute>("FCompute<gpu>", NumpyExponentialForward<gpu>);
=======
.set_attr<FIsCUDAGraphsCompatible>("FIsCUDAGraphsCompatible",
    [](const NodeAttrs&, const bool) {
      return false;
    })
.set_attr<FCompute>("FCompute<gpu>", NumpyExponentialForward<gpu>);
>>>>>>> f4bcd48dd... [1.x][FEATURE] CUDA graphs support (#19142)

NNVM_REGISTER_OP(_backward_broadcast_exponential)
    .set_attr<FCompute>("FCompute<gpu>", ExponentialReparamBackward<gpu>);

}  // namespace op
}  // namespace mxnet

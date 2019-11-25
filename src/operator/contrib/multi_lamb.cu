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
 *  Copyright (c) 2019 by Contributors
 * \file multi_lamb.cu
 * \brief vectorized lamb coefficient computed from sums of squared weights and grads
 * \author Moises Hernandez
 */

#include "./multi_lamb-inl.h"

namespace mxnet {
namespace op {

#define BLOCK_SIZE_LAMB 512
#define ILP_LAMB 4
    
template<bool has_mixed_precision, typename MPDType, typename DType>
__global__ void kernel_step1(const MultiLAMBKernelParam<DType, MPDType> kernel_params,
                             const float learning_rate, 
                             const float beta1, const float beta2,
                             const MPDType beta3, const MPDType beta4,
                             const MPDType biascorrection1, 
                             const MPDType biascorrection2,
                             const float epsilon,
                             const float wd,
                             const int step,
                             const float clip_gradient,
                             const bool bias_correction, 
                             const float rescale_grad,
                             int* block_to_tensor,
                             int* block_to_chunk) {
  const int tensorID = block_to_tensor[blockIdx.x];
  const int chunckID = block_to_chunk[blockIdx.x];
  const int startPos = chunckID * kernel_params.chunk_size + threadIdx.x;
  const int stopPos = chunckID * kernel_params.chunk_size + kernel_params.chunk_size;
    
  MPDType r_weight[ILP_LAMB];
  MPDType r_grad[ILP_LAMB];
  MPDType r_mean[ILP_LAMB];
  MPDType r_var[ILP_LAMB];
  MPDType r_g[ILP_LAMB];
  
  for(size_t i=startPos; i<stopPos && i<kernel_params.sizes[tensorID]; i+= blockDim.x*ILP_LAMB){
#pragma unroll
      for(int ii = 0; ii < ILP_LAMB; ii++){
          int load_pos = i + ii*blockDim.x;
          if(load_pos < stopPos && load_pos < kernel_params.sizes[tensorID]){
              r_weight[ii] = has_mixed_precision ? kernel_params.weights32[tensorID][load_pos]:
                                                   static_cast<MPDType>(kernel_params.weights[tensorID][load_pos]);
              r_grad[ii] = static_cast<MPDType>(kernel_params.grads[tensorID][load_pos]);
              r_mean[ii] = kernel_params.mean[tensorID][load_pos];
              r_var[ii] = kernel_params.var[tensorID][load_pos];
          }else{
              r_weight[ii] = static_cast<MPDType>(0);
              r_grad[ii] = static_cast<MPDType>(0);
              r_mean[ii] = static_cast<MPDType>(0);
              r_var[ii] = static_cast<MPDType>(0);
          }
      }
#pragma unroll
      for(int ii = 0; ii < ILP_LAMB; ii++){
          r_grad[ii] = r_grad[ii] * rescale_grad;
          if (clip_gradient >= 0.0f)
              r_grad[ii] = max(min(r_grad[ii], clip_gradient), -clip_gradient);
          r_mean[ii] = static_cast<MPDType>(beta1) * r_mean[ii] + beta3 * r_grad[ii];
          r_var[ii] = static_cast<MPDType>(beta2) * r_var[ii] + beta4 * r_grad[ii] * r_grad[ii];
          r_g[ii] = (r_mean[ii] / biascorrection1) / (sqrtf(r_var[ii] / biascorrection2) + epsilon) 
                    + wd * r_weight[ii];
       }
#pragma unroll
      for(int ii = 0; ii < ILP_LAMB; ii++){
          int store_pos = i + ii*blockDim.x;
          if(store_pos < stopPos && store_pos < kernel_params.sizes[tensorID]){
              kernel_params.mean[tensorID][store_pos] = r_mean[ii];
              kernel_params.var[tensorID][store_pos] = r_var[ii];
              kernel_params.temp_g[tensorID][store_pos] = r_g[ii];
          }
      }
  }
}
    
template<bool has_mixed_precision, typename MPDType, typename DType>
__global__ void kernel_step2(const MultiLAMBKernelParam<DType, MPDType> kernel_params,
                             const float* sumSqWeigths,
                             const float* sumSqtemp_g,
                             const float learning_rate,
                             const float lower_bound, 
                             const float upper_bound,
                             int* block_to_tensor,
                             int* block_to_chunk,
                             const OpReqType req) {
  const int tensorID = block_to_tensor[blockIdx.x];
  const int chunckID = block_to_chunk[blockIdx.x];
  const int startPos = chunckID * kernel_params.chunk_size + threadIdx.x;
  const int stopPos = chunckID * kernel_params.chunk_size + kernel_params.chunk_size;
  
  MPDType r1 = sqrtf(sumSqWeigths[tensorID]);
  MPDType r2 = sqrtf(sumSqtemp_g[tensorID]);
  r1 = min(max(r1, lower_bound), upper_bound);
  MPDType lr_adjusted;
  if (r1 == 0.0f || r2 == 0.0f)
      lr_adjusted = learning_rate;
  else
      lr_adjusted = learning_rate * r1/r2;
  
  MPDType r_weight[ILP_LAMB];
  MPDType r_g[ILP_LAMB];
  
  for(size_t i=startPos; i<stopPos && i<kernel_params.sizes[tensorID]; i+= blockDim.x*ILP_LAMB){
#pragma unroll
      for(int ii = 0; ii < ILP_LAMB; ii++){
          int load_pos = i + ii*blockDim.x;
          if(load_pos < stopPos&& load_pos < kernel_params.sizes[tensorID]){
              r_weight[ii] = has_mixed_precision ? kernel_params.weights32[tensorID][load_pos]:
                                                   static_cast<MPDType>(kernel_params.weights[tensorID][load_pos]);
              r_g[ii] = kernel_params.temp_g[tensorID][load_pos];
          }
      }
#pragma unroll
      for(int ii = 0; ii < ILP_LAMB; ii++){
          r_weight[ii] -= lr_adjusted * r_g[ii];
          
      }
#pragma unroll
      for(int ii = 0; ii < ILP_LAMB; ii++){
          int store_pos = i + ii*blockDim.x;
          if(store_pos < stopPos && store_pos < kernel_params.sizes[tensorID]){
              if (has_mixed_precision)
                  kernel_params.weights32[tensorID][store_pos] = r_weight[ii];
              KERNEL_ASSIGN(kernel_params.out_data[tensorID][store_pos], req, r_weight[ii]);
          }
      }
  }
}


template<typename MPDType, bool has_mixed_precision>
struct MultiLAMB_step1_kernelg {
  template<typename DType>
  MSHADOW_XINLINE static void Map(int i,
                                  const MultiLAMBKernelParam<DType, MPDType>& kernel_params,
                                  const float learning_rate, 
                                  const float beta1, const float beta2, 
                                  const float epsilon,
                                  const float wd,
                                  const int step,
                                  const float clip_gradient,
                                  const bool bias_correction, 
                                  const float rescale_grad) {
    using namespace mshadow_op;
    for (size_t index = 0; index < kernel_params.ntensors; ++index) {
      if ((size_t)i < kernel_params.sizes[index]) {
        MPDType w = has_mixed_precision ? kernel_params.weights32[index][i]:
                                          MPDType(kernel_params.weights[index][i]);
        MPDType scaled_grad = static_cast<MPDType>(kernel_params.grads[index][i])*rescale_grad;
        if (clip_gradient >= 0.0f)
            scaled_grad = mshadow_op::clip::Map(scaled_grad, static_cast<MPDType>(clip_gradient));
  
        MPDType mean = static_cast<MPDType>(beta1) * kernel_params.mean[index][i] + 
          (static_cast<MPDType>(1.0f) - static_cast<MPDType>(beta1)) * scaled_grad;
        MPDType var = static_cast<MPDType>(beta2) * kernel_params.var[index][i] + 
          (static_cast<MPDType>(1.0f) - static_cast<MPDType>(beta2)) * scaled_grad * scaled_grad;
        kernel_params.mean[index][i]=mean;
        kernel_params.var[index][i]=var;
  
        MPDType g;
        if(bias_correction){
          MPDType mean_hat = mean / (static_cast<MPDType>(1.0f) - power::Map(static_cast<MPDType>(beta1), static_cast<MPDType>(step)));
          MPDType var_hat = var / (static_cast<MPDType>(1.0f) - power::Map(static_cast<MPDType>(beta2), static_cast<MPDType>(step)));
          g = mean_hat / (sqrt(var_hat) + epsilon) + wd * w;
        }else{
          g = mean / (sqrt(var) + epsilon) + wd * w;
        }
        kernel_params.temp_g[index][i]=g;
      }
    }
  }
};

template<typename MPDType, bool has_mixed_precision>
struct MultiLAMB_step2_kernelg {
  template<typename DType>
  MSHADOW_XINLINE static void Map(int i, 
                                  const MultiLAMBKernelParam<DType, MPDType>& kernel_params,
                                  const float* sumSqWeigths,
                                  const float* sumSqtemp_g,
                                  const float learning_rate,
                                  const float lower_bound, 
                                  const float upper_bound,
                                  const OpReqType req) {
    for (size_t index = 0; index < kernel_params.ntensors; ++index) {
      if ((size_t)i < kernel_params.sizes[index]) {
        MPDType w = has_mixed_precision ? kernel_params.weights32[index][i]:
                                            MPDType(kernel_params.weights[index][i]);
        float r1 = sqrt(sumSqWeigths[index]);
        float r2 = sqrt(sumSqtemp_g[index]);
      
        r1 = min(max(r1, lower_bound), upper_bound);
      
      
        // calculate lamb_trust_ratio
        MPDType r;
        if (r1 == 0.0f || r2 == 0.0f)
          r = 1.0f;
        else
          r = r1/r2;
          
        MPDType lr_adjusted = learning_rate * r;
        w -= lr_adjusted * kernel_params.temp_g[index][i];

        // update weights
        if (has_mixed_precision)
          kernel_params.weights32[index][i] = w;
        KERNEL_ASSIGN(kernel_params.out_data[index][i], req, w);
      }
    }
  }
};
    

    
template<typename MPDType>
struct map_tensors_chunks {
  template<typename DType>
  MSHADOW_XINLINE static void Map(int i, 
                                  const MultiLAMBKernelParam<DType, MPDType>& kernel_params,
                                  int* block_to_tensor,
                                  int* block_to_chunk) {
    int chunkID=0;
    for (int index = 0; index < kernel_params.ntensors; ++index) {
      int current_chunk=0;
      for (int j = 0; j < kernel_params.sizes[index]; j+=kernel_params.chunk_size) {
          block_to_tensor[chunkID] = index;
          block_to_chunk[chunkID]= current_chunk;
          current_chunk++;
          chunkID++;
      }
    }  
  }
};
    
    
  
template<typename MPDType, typename DType>
void call_kernel1(Stream<gpu>* s,
                  const MultiLAMBKernelParam<DType, MPDType>& kernel_params,
                  const MultiLAMBParam &param,
                  int* block_to_tensor,
                  int* block_to_chunk){
  
  int nblocks = kernel_params.nchunks;
  Kernel<map_tensors_chunks<MPDType>, gpu>::
                        Launch(s, 1,
                               kernel_params,
                               block_to_tensor,
                               block_to_chunk);
  
  bool has_mixed_precision = !std::is_same<DType, MPDType>::value;
  MPDType beta3 = 1.0 - param.beta1;
  MPDType beta4 = 1.0 - param.beta2;
    
  MPDType bias_correction1 = 1.0 - std::pow(param.beta1,param.step);
  MPDType bias_correction2 = 1.0f - std::pow(param.beta2,param.step);
  
  if(has_mixed_precision)
    kernel_step1<true><<<nblocks, BLOCK_SIZE_LAMB, 0, Stream<gpu>::GetStream(s)>>>(
                      kernel_params,
                      param.learning_rate,
                      param.beta1, param.beta2,
                      beta3, beta4,
                      bias_correction1,
                      bias_correction2,
                      param.epsilon, param.wd,
                      param.step, param.clip_gradient,
                      param.bias_correction,
                      param.rescale_grad,
                      block_to_tensor,
                      block_to_chunk);
  else
    kernel_step1<false><<<nblocks, BLOCK_SIZE_LAMB, 0, Stream<gpu>::GetStream(s)>>>(
                      kernel_params,
                      param.learning_rate,
                      param.beta1, param.beta2,
                      beta3, beta4,
                      bias_correction1,
                      bias_correction2,
                      param.epsilon, param.wd,
                      param.step, param.clip_gradient,
                      param.bias_correction,
                      param.rescale_grad,
                      block_to_tensor,
                      block_to_chunk);
  /*Kernel<MultiLAMB_step1_kernelg<MPDType, !std::is_same<DType, MPDType>::value>, gpu>::  
                                  Launch(s, kernel_params.max_size,
                                  kernel_params,
                                  param.learning_rate,
                                  param.beta1, param.beta2,
                                  param.epsilon,
                                  param.wd,
                                  param.step,
                                  param.clip_gradient,
                                  param.bias_correction,
                                  param.rescale_grad);*/
  }

template<typename MPDType, typename DType>
void call_kernel2(Stream<gpu>* s,
                  const MultiLAMBKernelParam<DType, MPDType>& kernel_params,
                  const MultiLAMBParam &param,
                  float* r1, float* r2,
                  int* block_to_tensor,
                  int* block_to_chunk,
                  const OpReqType req){

  size_t nblocks = kernel_params.nchunks;
  bool has_mixed_precision = !std::is_same<DType, MPDType>::value;
  if(has_mixed_precision)
    kernel_step2<true><<<nblocks, BLOCK_SIZE_LAMB, 0, Stream<gpu>::GetStream(s)>>>(
                      kernel_params,
                      r1, r2,
                      param.learning_rate,
                      param.lower_bound, param.upper_bound,
                      block_to_tensor,
                      block_to_chunk,
                      req);
  else
    kernel_step2<false><<<nblocks, BLOCK_SIZE_LAMB, 0, Stream<gpu>::GetStream(s)>>>(
                      kernel_params,
                      r1, r2,
                      param.learning_rate,
                      param.lower_bound, param.upper_bound,
                      block_to_tensor,
                      block_to_chunk,
                      req);

  /*Kernel<MultiLAMB_step2_kernelg<MPDType, !std::is_same<DType, MPDType>::value>, gpu>::  
                                Launch(s, kernel_params.max_size,
                                kernel_params,
                                r1, r2,
                                param.learning_rate,
                                param.lower_bound, param.upper_bound,
                                req);*/
}


NNVM_REGISTER_OP(_multi_lamb_update)
.set_attr<FCompute>("FCompute<gpu>",  multiLAMBUpdate<gpu, false>);

NNVM_REGISTER_OP(_multi_mp_lamb_update)
.set_attr<FCompute>("FCompute<gpu>",  multiLAMBUpdate<gpu, true>);

}  // namespace op
}  // namespace mxnet

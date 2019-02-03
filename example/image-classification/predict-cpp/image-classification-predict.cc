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
 * Copyright (c) 2015 by Xiao Liu, pertusa, caprice-j
 * \file image_classification-predict.cpp
 * \brief C++ predict example of mxnet
 *
 * This is a simple predictor which shows how to use c api for image classification. It uses
 * opencv for image reading.
 *
 * Created by liuxiao on 12/9/15.
 * Thanks to : pertusa, caprice-j, sofiawu, tqchen, piiswrong
 * Home Page: www.liuxiao.org
 * E-mail: liuxiao@foxmail.com
*/

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <thread>
#include <iomanip>
#include <mxnet/c_predict_api.h>

// Read file to buffer
class BufferFile {
 public :
  std::string file_path_;
  std::size_t length_ = 0;
  std::unique_ptr<char[]> buffer_;

  explicit BufferFile(const std::string& file_path)
    : file_path_(file_path) {

    std::ifstream ifs(file_path.c_str(), std::ios::in | std::ios::binary);
    if (!ifs) {
      std::cerr << "Can't open the file. Please check " << file_path << ". \n";
      return;
    }

    ifs.seekg(0, std::ios::end);
    length_ = static_cast<std::size_t>(ifs.tellg());
    ifs.seekg(0, std::ios::beg);
    std::cout << file_path.c_str() << " ... " << length_ << " bytes\n";

    buffer_.reset(new char[length_]);
    ifs.read(buffer_.get(), length_);
    ifs.close();
  }

  std::size_t GetLength() {
    return length_;
  }

  char* GetBuffer() {
    return buffer_.get();
  }
};

void predict(PredictorHandle pred_hnd, const std::vector<mx_float> &image_data,
             NDListHandle nd_hnd, const std::string &synset_file, int i) {
  auto image_size = image_data.size();
  // Set Input Image
  MXPredSetInput(pred_hnd, "data", image_data.data(), static_cast<mx_uint>(image_size));

  // Do Predict Forward
  MXPredForward(pred_hnd);

  mx_uint output_index = 0;

  mx_uint* shape = nullptr;
  mx_uint shape_len;

  // Get Output Result
  MXPredGetOutputShape(pred_hnd, output_index, &shape, &shape_len);

  std::size_t size = 1;
  for (mx_uint i = 0; i < shape_len; ++i) { size *= shape[i]; }

  std::vector<float> data(size);

  MXPredGetOutput(pred_hnd, output_index, &(data[0]), static_cast<mx_uint>(size));

  // Release NDList
  if (nd_hnd) {
    MXNDListFree(nd_hnd);
  }

  // Release Predictor
  MXPredFree(pred_hnd);

}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    std::cout << "No test image here." << std::endl
              << "Usage: ./image-classification-predict apple.jpg [num_threads]" << std::endl;
    return EXIT_FAILURE;
  }

  std::string test_file(argv[1]);
  int num_threads = 1;
  if (argc == 3)
    num_threads = std::atoi(argv[2]);

  // Models path for your model, you have to modify it
  std::string json_file = "model/Inception/Inception-BN-symbol.json";
  std::string param_file = "model/Inception/Inception-BN-0126.params";
  std::string synset_file = "model/Inception/synset.txt";
  std::string nd_file = "model/Inception/mean_224.nd";

  BufferFile json_data(json_file);
  BufferFile param_data(param_file);

  // Parameters
  int dev_type = 1;  // 1: cpu, 2: gpu
  int dev_id = 0;  // arbitrary.
  mx_uint num_input_nodes = 1;  // 1 for feedforward
  const char* input_key[1] = { "data" };
  const char** input_keys = input_key;

  // Image size and channels
  int width = 224;
  int height = 224;
  int channels = 3;

  const mx_uint input_shape_indptr[2] = { 0, 4 };
  const mx_uint input_shape_data[4] = { 1,
                                        static_cast<mx_uint>(channels),
                                        static_cast<mx_uint>(height),
                                        static_cast<mx_uint>(width) };

  if (json_data.GetLength() == 0 || param_data.GetLength() == 0) {
    return EXIT_FAILURE;
  }

  auto image_size = static_cast<std::size_t>(width * height * channels);

  // Read Mean Data
  const mx_float* nd_data = nullptr;
  NDListHandle nd_hnd = nullptr;
  BufferFile nd_buf(nd_file);

  if (nd_buf.GetLength() > 0) {
    mx_uint nd_index = 0;
    mx_uint nd_len;
    const mx_uint* nd_shape = nullptr;
    const char* nd_key = nullptr;
    mx_uint nd_ndim = 0;

    MXNDListCreate(static_cast<const char*>(nd_buf.GetBuffer()),
                   static_cast<int>(nd_buf.GetLength()),
                   &nd_hnd, &nd_len);

    MXNDListGet(nd_hnd, nd_index, &nd_key, &nd_data, &nd_shape, &nd_ndim);
  }


  std::vector<mx_float> image_data(image_size);
  for (int i = 0; i < image_size; i++) {
    image_data[i] = 0;
  }

//  GetImageFile(test_file, image_data.data(), channels, cv::Size(width, height), nd_data);

  if (num_threads == 1) {
    // Create Predictor
    PredictorHandle pred_hnd;
    MXPredCreate(static_cast<const char*>(json_data.GetBuffer()),
                 static_cast<const char*>(param_data.GetBuffer()),
                 static_cast<int>(param_data.GetLength()),
                 dev_type,
                 dev_id,
                 num_input_nodes,
                 input_keys,
                 input_shape_indptr,
                 input_shape_data,
                 &pred_hnd);

    predict(pred_hnd, image_data, nd_hnd, synset_file, 0);
  } else {
    // Create Predictor
    std::vector<PredictorHandle> pred_hnds(num_threads, nullptr);
    MXPredCreateMultiThread(static_cast<const char*>(json_data.GetBuffer()),
                            static_cast<const char*>(param_data.GetBuffer()),
                            static_cast<int>(param_data.GetLength()),
                            dev_type,
                            dev_id,
                            num_input_nodes,
                            input_keys,
                            input_shape_indptr,
                            input_shape_data,
                            pred_hnds.size(),
                            pred_hnds.data());

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++)
      threads.emplace_back(predict, pred_hnds[i], image_data, nd_hnd, synset_file, i);
    for (int i = 0; i < num_threads; i++)
      threads[i].join();
  }
  printf("run successfully\n");

  return EXIT_SUCCESS;
}

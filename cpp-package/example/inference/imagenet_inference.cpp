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

/*
 * This example demonstrates image classification workflow with pre-trained models using MXNet C++ API.
 * The example performs following tasks.
 * 1. Load the pre-trained model.
 * 2. Load the parameters of pre-trained model.
 * 3. Load the inference dataset and create a new ImageRecordIter.
 * 4. Run the forward pass and obtain throughput & accuracy.
 */

#include <sys/stat.h>
#include <sys/time.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <type_traits>
#include "mxnet/c_api.h"
#include "mxnet/tuple.h"
#include "mxnet-cpp/MxNetCpp.h"
#include "mxnet-cpp/initializer.h"

using namespace mxnet::cpp;

double get_msec() {
    struct timeval time;
    gettimeofday(&time, NULL);
    return 1e+3 * time.tv_sec + 1e-3 * time.tv_usec;
}

// define the data type for NDArray, aliged with the definition in mshadow/base.h
enum TypeFlag {
  kFloat32 = 0,
  kFloat64 = 1,
  kFloat16 = 2,
  kUint8 = 3,
  kInt32 = 4,
  kInt8  = 5,
  kInt64 = 6,
};

/*
 * class Predictor
 *
 * This class encapsulates the functionality to load the model, prepare dataset and run the forward pass.
 */

class Predictor {
 public:
    Predictor() {}
    Predictor(const std::string& model_json_file, const std::string& model_params_file, const Shape& input_shape,
              bool use_gpu, const std::string& dataset, const int data_nthreads, const std::string& data_layer_type,
              std::vector<float>& rgb_mean, std::vector<float>& rgb_std, int shuffle_chunk_seed, int seed, bool benchmark);
    void BenchmarkScore(int num_inference_batches);
    void Score(int num_skipped_batches, int num_inference_batches);
    ~Predictor();

 private:
    bool CreateImageRecordIter();
    bool AdvanceDataIter(int skipped_batches);
    void LoadModel(const std::string& model_json_file);
    void LoadParameters(const std::string& model_parameters_file);
    void InitParameters();

    inline bool FileExists(const std::string& name) {
      struct stat buffer;
      return (stat(name.c_str(), &buffer) == 0);
    }
    int GetDataLayerType();

    NDArray mean_img;
    std::map<std::string, NDArray> args_map;
    std::map<std::string, NDArray> aux_map;
    Symbol net;
    Executor *executor;
    Shape input_shape;
    Context global_ctx = Context::cpu();

    MXDataIter *val_iter;
    bool use_gpu_;
    std::string dataset_;
    int data_nthreads_;
    std::string data_layer_type_;
    std::vector<float> rgb_mean_;
    std::vector<float> rgb_std_;
    int shuffle_chunk_seed_;
    int seed_;
    bool benchmark_;
};


/*
 * The constructor takes following parameters as input:
 * 1. model_json_file:  The model in json formatted file.
 * 2. model_params_file: File containing model parameters
 * 3. input_shape: Shape of input data to the model. Since this class will be running one inference at a time,
 *                 the input shape is required to be in format Shape(1, number_of_channels, height, width)
 *                 The input image will be resized to (height x width) size before running the inference.
 * 4. use_gpu: if run inference on GPU
 * 5. dataset: data file to be used for inference
 * 6. data_nthreads: number of threads for data loading
 * 7. data_layer_type: data type for data layer
 * 8. rgb_mean: mean value to be subtracted on R/G/B channel
 * 9. rgb_std: standard deviation on R/G/B channel
 * 10. shuffle_chunk_seed: shuffling chunk seed
 * 11. seed: shuffling seed
 * 12. benchmark: use dummy data for inference
 *
 * The constructor will:
 *  1. Load the model and parameter files.
 *  2. Create ImageRecordIter by using the given dataset file.
 *  3. Infer and construct NDArrays according to the input argument and create an executor.
 */
Predictor::Predictor(const std::string& model_json_file, const std::string& model_params_file, const Shape& input_shape,
                     bool use_gpu, const std::string& dataset, const int data_nthreads, const std::string& data_layer_type,
                     std::vector<float>& rgb_mean, std::vector<float>& rgb_std, int shuffle_chunk_seed, int seed, bool benchmark)
    : input_shape(input_shape), use_gpu_(use_gpu), dataset_(dataset), data_nthreads_(data_nthreads), data_layer_type_(data_layer_type),
      rgb_mean_(rgb_mean), rgb_std_(rgb_std), shuffle_chunk_seed_(shuffle_chunk_seed), seed_(seed), benchmark_(benchmark) {
  if (use_gpu) {
    global_ctx = Context::gpu();
  }

  //initilize data iterator
  if (!benchmark_ && !CreateImageRecordIter()) {
    LG << "Error: failed to create ImageRecordIter";
    throw std::runtime_error("ImageRecordIter cannot be created");
  }

  // Load the model
  LoadModel(model_json_file);
  // Initilize the parameters
  // benchmark=false, load from params file
  // benchmark=true, randomly initializes parameters
  if (!benchmark_) LoadParameters(model_params_file);
  else InitParameters();

  int dtype = GetDataLayerType();
  if (dtype == -1) {
    throw std::runtime_error("Unsupported data layer type...");
  }
  args_map["data"] = NDArray(input_shape, global_ctx, false, dtype);
  Shape label_shape(input_shape[0]);
  args_map["softmax_label"] = NDArray(label_shape, global_ctx, false);
  std::vector<NDArray> arg_arrays;
  std::vector<NDArray> grad_arrays;
  std::vector<OpReqType> grad_reqs;
  std::vector<NDArray> aux_arrays;

  // infer and create ndarrays according to the given input ndarrays.
  net.InferExecutorArrays(global_ctx, &arg_arrays, &grad_arrays, &grad_reqs, &aux_arrays, args_map,
                      std::map<std::string, NDArray>(), std::map<std::string, OpReqType>(),
                      aux_map);
  for (auto& i : grad_reqs) i = OpReqType::kNullOp;

  // Create an executor after binding the model to input parameters.
  executor = new Executor(net, global_ctx, arg_arrays, grad_arrays, grad_reqs, aux_arrays);
}

/*
 * The following function is used to get the data type for input data
 */
int Predictor::GetDataLayerType() {
  int ret_type = -1;
  if (data_layer_type_ == "float32") {
    ret_type = kFloat32;
  } else if (data_layer_type_ == "int8") {
    ret_type = kInt8;
  } else if (data_layer_type_ == "uint8") {
    ret_type = kUint8;
  } else {
    LG << "Unsupported data layer type " << data_layer_type_ << "..."
       << "Please use one of {float32, int8, uint8}";
  }
  return ret_type;
}

/*
 * create a new ImageRecordIter according to the given parameters
 */
bool Predictor::CreateImageRecordIter() {
  val_iter = new MXDataIter("ImageRecordIter");
  if (!FileExists(dataset_)) {
    LG << "Error: " << dataset_ << " must be provided";
    return false;
  }

  std::vector<index_t> shape_vec {input_shape[1], input_shape[2], input_shape[3]};
  mxnet::TShape data_shape(shape_vec.begin(), shape_vec.end());

  // set image record parser parameters
  val_iter->SetParam("path_imgrec", dataset_);
  val_iter->SetParam("label_width", 1);
  val_iter->SetParam("data_shape", data_shape);
  val_iter->SetParam("preprocess_threads", data_nthreads_);
  val_iter->SetParam("shuffle_chunk_seed", shuffle_chunk_seed_);

  // set Batch parameters
  val_iter->SetParam("batch_size", input_shape[0]);

  // image record parameters
  val_iter->SetParam("shuffle", true);
  val_iter->SetParam("seed", seed_);

  // set normalize parameters
  val_iter->SetParam("mean_r", rgb_mean_[0]);
  val_iter->SetParam("mean_g", rgb_mean_[1]);
  val_iter->SetParam("mean_b", rgb_mean_[2]);
  val_iter->SetParam("std_r", rgb_std_[0]);
  val_iter->SetParam("std_g", rgb_std_[1]);
  val_iter->SetParam("std_b", rgb_std_[2]);

  //set prefetcher parameters
  if (use_gpu_) {
    val_iter->SetParam("ctx", "gpu");
  } else {
    val_iter->SetParam("ctx", "cpu");
  }
  val_iter->SetParam("dtype", data_layer_type_);

  val_iter->CreateDataIter();
  return true;
}

/*
 * The following function loads the model from json file.
 */
void Predictor::LoadModel(const std::string& model_json_file) {
  if (!FileExists(model_json_file)) {
    LG << "Model file " << model_json_file << " does not exist";
    throw std::runtime_error("Model file does not exist");
  }
  LG << "Loading the model from " << model_json_file << std::endl;
  net = Symbol::Load(model_json_file);
}


/*
 * The following function loads the model parameters.
 */
void Predictor::LoadParameters(const std::string& model_parameters_file) {
  if (!FileExists(model_parameters_file)) {
    LG << "Parameter file " << model_parameters_file << " does not exist";
    throw std::runtime_error("Model parameters does not exist");
  }
  LG << "Loading the model parameters from " << model_parameters_file << std::endl;
  std::map<std::string, NDArray> parameters;
  NDArray::Load(model_parameters_file, 0, &parameters);
  for (const auto &k : parameters) {
    if (k.first.substr(0, 4) == "aux:") {
      auto name = k.first.substr(4, k.first.size() - 4);
      aux_map[name] = k.second.Copy(global_ctx);
    }
    if (k.first.substr(0, 4) == "arg:") {
      auto name = k.first.substr(4, k.first.size() - 4);
      args_map[name] = k.second.Copy(global_ctx);
    }
  }
  /*WaitAll is need when we copy data between GPU and the main memory*/
  NDArray::WaitAll();
}

/*
 * The following function randomly initializes the parameters when benchmark_ is true.
 */
void Predictor::InitParameters () {
  std::vector<mx_uint> data_shape;
  for (index_t i=0; i < input_shape.ndim(); i++) {
    data_shape.push_back(input_shape[i]);
  }

  std::map<std::string, std::vector<mx_uint> > arg_shapes;
  std::vector<std::vector<mx_uint> > aux_shapes, in_shapes, out_shapes;
  arg_shapes["data"] = data_shape;
  net.InferShape(arg_shapes, &in_shapes, &aux_shapes, &out_shapes);

  // initializer to call
  Xavier xavier(Xavier::uniform, Xavier::avg, 2.0f);
  index_t i = 0;
  for (auto& name : net.ListArguments()) {
    int paramType = kFloat32;
    if (Initializer::StringEndWith(name, "weight_quantize") ||
        Initializer::StringEndWith(name, "bias_quantize")) {
      paramType = kInt8;
    }
    NDArray tmp_arr(in_shapes[i++], global_ctx, false, paramType);
    xavier(name, &tmp_arr);
    args_map[name] = tmp_arr.Copy(global_ctx);
  }

  i = 0;
  for (auto& name : net.ListAuxiliaryStates()) {
    NDArray tmp_arr(aux_shapes[i++], global_ctx, false);
    xavier(name, &tmp_arr);
    aux_map[name] = tmp_arr.Copy(global_ctx);
  }
  /*WaitAll is need when we copy data between GPU and the main memory*/
  NDArray::WaitAll();
}

/*
 * The following function runs the forward pass on the model.
 * for dummy data
 */
void Predictor::BenchmarkScore(int num_inference_batches) {
  // Create dummy data
  std::vector<float> dummy_data(input_shape.Size());

  for (int i = 0; i < static_cast<int>(input_shape.Size()); ++i) {
    dummy_data[i] = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
  }
    executor->arg_dict()["data"].SyncCopyFromCPU(
        dummy_data.data(),
        input_shape.Size());
  NDArray::WaitAll();

  LG << "Running the forward pass on model to evaluate the performance..";

  // warm up.
  for (int i = 0; i < 5; i++) {
    executor->Forward(false);
    NDArray::WaitAll();
  }

  // Run the forward pass.
  double ms = get_msec();
  for (int i = 0; i < num_inference_batches; i++) {
    executor->Forward(false);
    NDArray::WaitAll();
  }
  ms = get_msec() - ms;
  LG << "benchmark completed!";
  LG << "batch size: " << input_shape[0] << " num batch: " << num_inference_batches
     << " throughput: " << 1000 * input_shape[0] * num_inference_batches / ms
     << " imgs/s latency:" << ms / input_shape[0] / num_inference_batches << " ms";
}

/*
 * \param skipped_batches skip the first number of batches
 *
 */
bool Predictor::AdvanceDataIter(int skipped_batches) {
  assert(skipped_batches >= 0);
  if (skipped_batches == 0) return true;
  int skipped_count = 0;
  while (val_iter->Next()) {
    if (++skipped_count >= skipped_batches) break;
  }
  if (skipped_count != skipped_batches) return false;
  return true;
}

/*
 * The following function runs the forward pass on the model.
 * for real data
 */
void Predictor::Score(int num_skipped_batches, int num_inference_batches) {
  // Create metrics
  Accuracy val_acc;

  val_iter->Reset();
  val_acc.Reset();
  int nBatch = 0;

  if (!AdvanceDataIter(num_skipped_batches)) {
    LG << "skipped batches should less than total batches!";
    return;
  }
  double ms = get_msec();
  while (val_iter->Next()) {
    auto data_batch = val_iter->GetDataBatch();
    data_batch.data.CopyTo(&args_map["data"]);
    data_batch.label.CopyTo(&args_map["softmax_label"]);
    NDArray::WaitAll();

    // running on forward pass
    executor->Forward(false);
    NDArray::WaitAll();
    val_acc.Update(data_batch.label, executor->outputs[0]);

    if (++nBatch >= num_inference_batches) {
      break;
    }
  }
  ms = get_msec() - ms;
  num_inference_batches = (nBatch == num_inference_batches) ? num_inference_batches : nBatch;

  auto args_name = net.ListArguments();
  std::cout << "INFO:" << "Dataset for inference: " << dataset_ << std::endl
            << "INFO:" << "label_name = " << args_name[args_name.size()-1] << std::endl
            << "INFO:" << "rgb_mean: " << "(" << rgb_mean_[0] << ", " << rgb_mean_[1] << ", " << rgb_mean_[2] << ")" << std::endl
            << "INFO:" << "rgb_std: " << "(" << rgb_std_[0] << ", " << rgb_std_[1] << ", " << rgb_std_[2] << ")" << std::endl
            << "INFO:" << "Image shape: " << "(" << input_shape[1] << ", " << input_shape[2] << ", " << input_shape[3] << ")" << std::endl
            << "INFO:" << "Finished inference with: " << num_inference_batches * input_shape[0] << " images " << std::endl
            << "INFO:" << "Batch size = " << input_shape[0] << " for inference" << std::endl
            << "INFO:" << "Accuracy: " << val_acc.Get() << std::endl
            << "INFO:" << "Throughput: " << (1000 * num_inference_batches * input_shape[0] / ms) << " images per second" << std::endl;
}

Predictor::~Predictor() {
  if (executor) {
    delete executor;
  }
  if (!benchmark_) {
    delete val_iter;
  }
  MXNotifyShutdown();
}

/*
 * Convert the input string of number into the vector.
 */
template<typename T>
std::vector<T> createVectorFromString(const std::string& input_string) {
    std::vector<T> dst_vec;
    char *p_next;
    T elem;
    bool bFloat = std::is_same<T, float>::value;
    if (!bFloat) {
      elem = strtol(input_string.c_str(), &p_next, 10);
    } else {
      elem = strtof(input_string.c_str(), &p_next);
    }

    dst_vec.push_back(elem);
    while (*p_next) {
        if (!bFloat) {
          elem = strtol(p_next, &p_next, 10);
        } else {
          elem = strtof(p_next, &p_next);
        }
        dst_vec.push_back(elem);
    }
    return dst_vec;
}

void printUsage() {
    std::cout << "Usage:" << std::endl;
    std::cout << "imagenet_inference --symbol_file <model symbol file in json format>  " << std::endl
              << "--params_file <model params file> " << std::endl
              << "--dataset <dataset used to run inference> " << std::endl
              << "--data_nthreads <default: 60> " << std::endl
              << "--input_shape <shape of input image e.g \"3 224 224\">] " << std::endl
              << "--rgb_mean <mean value to be subtracted on R/G/B channel e.g \"0 0 0\"> " << std::endl
              << "--rgb_std <standard deviation on R/G/B channel. e.g \"1 1 1\"> " << std::endl
              << "--batch_size <number of images per batch> " << std::endl
              << "--num_skipped_batches <skip the number of batches for inference> " << std::endl
              << "--num_inference_batches <number of batches used for inference> " << std::endl
              << "--data_layer_type <default: \"float32\" choices: [\"float32\", \"int8\", \"uint8\"]> " << std::endl
              << "--gpu  <whether to run inference on GPU, default: false>" << std::endl
              << "--benchmark <whether to use dummy data to run inference, default: false>"
              << std::endl;
}

int main(int argc, char** argv) {
  std::string model_file_json;
  std::string model_file_params;
  std::string dataset("");
  std::string input_rgb_mean("0 0 0");
  std::string input_rgb_std("1 1 1");
  bool use_gpu = false;
  bool benchmark = false;
  int batch_size = 64;
  int num_skipped_batches = 0;
  int num_inference_batches = 100;
  std::string data_layer_type("float32");
  std::string input_shape("3 224 224");
  int seed = 48564309;
  int shuffle_chunk_seed = 3982304;
  int data_nthreads = 60;

  int index = 1;
  while (index < argc) {
    if (strcmp("--symbol_file", argv[index]) == 0) {
            index++;
            model_file_json = (index < argc ? argv[index]:"");
        } else if (strcmp("--params_file", argv[index]) == 0) {
            index++;
            model_file_params = (index < argc ? argv[index]:"");
        } else if (strcmp("--dataset", argv[index]) == 0) {
            index++;
            dataset = (index < argc ? argv[index]:dataset);
        } else if (strcmp("--data_nthreads", argv[index]) == 0) {
            index++;
            data_nthreads = strtol(argv[index], nullptr, 10);
        } else if (strcmp("--input_shape", argv[index]) == 0) {
            index++;
            input_shape = (index < argc ? argv[index]:input_shape);
        } else if (strcmp("--rgb_mean", argv[index]) == 0) {
            index++;
            input_rgb_mean = (index < argc ? argv[index]:input_rgb_mean);
        } else if (strcmp("--rgb_std", argv[index]) == 0) {
            index++;
            input_rgb_std = (index < argc ? argv[index]:input_rgb_std);
        } else if (strcmp("--batch_size", argv[index]) == 0) {
            index++;
            batch_size = strtol(argv[index], nullptr, 10);
        }  else if (strcmp("--num_skipped_batches", argv[index]) == 0) {
            index++;
            num_skipped_batches = strtol(argv[index], nullptr, 10);
        }  else if (strcmp("--num_inference_batches", argv[index]) == 0) {
            index++;
            num_inference_batches = strtol(argv[index], nullptr, 10);
        } else if (strcmp("--data_layer_type", argv[index]) == 0) {
            index++;
            data_layer_type = (index < argc ? argv[index]:data_layer_type);
        } else if (strcmp("--gpu", argv[index]) == 0) {
            use_gpu = true;
        } else if (strcmp("--benchmark", argv[index]) == 0) {
            benchmark = true;
        } else if (strcmp("--help", argv[index]) == 0) {
            printUsage();
            return 0;
        }
        index++;
  }

  if (model_file_json.empty() || (!benchmark && model_file_params.empty())) {
    LG << "ERROR: Model details such as symbol, param files are not specified";
    printUsage();
    return 1;
  }
  std::vector<index_t> input_dimensions = createVectorFromString<index_t>(input_shape);
  input_dimensions.insert(input_dimensions.begin(), batch_size);
  Shape input_data_shape(input_dimensions);

  std::vector<float> rgb_mean = createVectorFromString<float>(input_rgb_mean);
  std::vector<float> rgb_std = createVectorFromString<float>(input_rgb_std);

  // Initialize the predictor object
  Predictor predict(model_file_json, model_file_params, input_data_shape, use_gpu, dataset,
                    data_nthreads, data_layer_type, rgb_mean, rgb_std, shuffle_chunk_seed, seed, benchmark);

  if (benchmark) {
    predict.BenchmarkScore(num_inference_batches);
  } else {
    predict.Score(num_skipped_batches, num_inference_batches);
  }
  return 0;
}

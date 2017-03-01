/**
 * Copyright (c) 2017 by Contributors
 * @file   kvstore_nccl.h
 * @brief  NCCL implementation
 */
#ifndef MXNET_KVSTORE_KVSTORE_NCCL_H_
#define MXNET_KVSTORE_KVSTORE_NCCL_H_

#if MXNET_USE_NCCL

#include <mxnet/kvstore.h>
#include <nccl.h>
#include <unordered_map>
#include <bitset>
#include <vector>
#include <utility>
#include <algorithm>
#include <map>
#include "../storage/pooled_storage_manager.h"
#include "../common/cuda_utils.h"

namespace mxnet {
namespace kvstore {
/**
 * \brief store data in local machine
 */
class KVStoreNCCL : public KVStore {
 public:
  KVStoreNCCL() : KVStore() {
  }

  virtual ~KVStoreNCCL() {
    for (size_t i = 0; i < streams_.size(); ++i) {
      cudaStreamDestroy(streams_[i]);
    }
  }

  void Init(const std::vector<int>& keys,
            const std::vector<NDArray>& values) override {
    if (streams_.empty()) {
      int ndev = 0;
      cudaGetDeviceCount(&ndev);
      streams_.resize(ndev);
      for (size_t i = 0; i < streams_.size(); ++i) {
        cudaSetDevice(i);
        cudaStreamCreate(&(streams_[i]));
      }
    }
    for (size_t i = 0; i < keys.size(); ++i) {
      CHECK(local_.find(keys[i]) == local_.end())
          << "duplicate init of key " << keys[i];
      Entry e;
      e.initial_value = values[i].Copy(Context::CPUPinned(0));
      e.sent_to_device = false;
      local_[keys[i]] = e;
    }
  }

  void Push(const std::vector<int>& keys,
            const std::vector<NDArray>& values,
            int priority) override {
    std::vector<int> uniq_keys;
    std::vector<std::vector<NDArray> > grouped_vals;
    GroupKVPairs(keys, values, &uniq_keys, &grouped_vals);

    for (size_t i = 0; i < uniq_keys.size(); ++i) {
      int key = uniq_keys[i];
      CHECK(local_.find(key) != local_.end())
        << "key " << key << " has not been inited";
      std::vector<int32_t> devices;
      for (size_t j = 0; j < grouped_vals[i].size(); ++j) {
        CHECK(grouped_vals[i][j].ctx().dev_mask() != cpu::kDevMask)
          << "NCCL KVStore does not support data on the CPU";
        devices.push_back(grouped_vals[i][j].ctx().dev_id);
      }
      std::sort(devices.begin(), devices.end());
      Entry & e = local_[key];
      if (!e.sent_to_device) {
        InitializeEntry(&e, grouped_vals[i], devices);
      } else {
        for (size_t j = 0; j < e.sharded_value.size(); ++j) {
          if (e.sharded_value[j].ctx().dev_id != devices[j]) {
            LOG(FATAL) << "Entry in NCCL KVstore can be accesed only by single set of devices.";
          }
        }
      }
      const std::vector<NDArray> & merged = ReduceScatter(&e, grouped_vals[i], priority);
      if (updater_ != nullptr) {
        for (size_t j = 0; j < merged.size(); ++j) {
          if (merged[j].shape().Size() > 0) {
            updater_(key, merged[j], &(e.sharded_value[j]));
          } else {
            e.sharded_value[j] = merged[j];
          }
        }
      } else {
        e.sharded_value = merged;
      }
    }
  }

  void Pull(const std::vector<int>& keys,
            const std::vector<NDArray*>& values,
            int priority) override {
    std::vector<int> uniq_keys;
    std::vector<std::vector<NDArray*> > grouped_vals;
    GroupKVPairs(keys, values, &uniq_keys, &grouped_vals);

    for (size_t i = 0; i < uniq_keys.size(); ++i) {
      int key = uniq_keys[i];
      CHECK(local_.find(key) != local_.end())
        << "key " << key << " has not been inited";
      std::vector<int32_t> devices;
      for (size_t j = 0; j < grouped_vals[i].size(); ++j) {
        CHECK(grouped_vals[i][j]->ctx().dev_mask() != cpu::kDevMask)
          << "NCCL KVStore does not support data on the CPU";
        devices.push_back(grouped_vals[i][j]->ctx().dev_id);
      }
      std::sort(devices.begin(), devices.end());
      Entry & e = local_[key];
      if (!e.sent_to_device) {
        InitializeEntry(&e, grouped_vals[i], devices);
      } else {
        for (size_t j = 0; j < e.sharded_value.size(); ++j) {
          if (e.sharded_value[j].ctx().dev_id != devices[j]) {
            LOG(FATAL) << "Entry in NCCL KVstore can be accesed only by single set of devices.";
          }
        }
      }
      AllGather(e, grouped_vals[i], priority);
    }
  }

 protected:
  /**
   * \brief group values on keys
   */
  template <typename V>
  void GroupKVPairs(const std::vector<int>& keys,
                    const std::vector<V>& values,
                    std::vector<int>* uniq_keys,
                    std::vector<std::vector<V> >* grouped_vals) {
    CHECK_EQ(keys.size(), values.size());
    // TODO(mli) check if already sorted as an optimization
    using Idx = std::pair<int, int>;
    std::vector<Idx> idx(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
      idx[i].first = keys[i]; idx[i].second = i;
    }
    std::sort(idx.begin(), idx.end(), [](const Idx& a, const Idx& b) {
        return a.first < b.first;
      });

    int pre_key = idx[0].first - 1;
    for (auto i : idx) {
      if (i.first != pre_key) {
        uniq_keys->push_back(i.first);
        grouped_vals->push_back({values[i.second]});
        pre_key = i.first;;
      } else {
        grouped_vals->back().push_back(values[i.second]);
      }
    }
  }

  struct Entry {
    NDArray initial_value;
    bool sent_to_device;
    std::vector<NDArray> sharded_value;
    std::vector<NDArray> scratch_space;
    TShape shape;
    std::vector<ncclComm_t> communicators;
  };

  // Utility function
  template<typename T>
  inline const T * ptr(const T & obj) { return &obj; }
  template<typename T>
  inline const T * ptr(const T * obj) { return obj; }

  template<typename T>
  inline void InitializeEntry(Entry * e,
                              const std::vector<T> & grouped_vals,
                              const std::vector<int32_t> & devices) {
    {
      std::lock_guard<std::mutex> l(mxnet::storage::GPUPooledStorageManager::mutex_);
      if (comms_.find(devices) == comms_.end()) {
        e->communicators = std::vector<ncclComm_t>(devices.size());
        ncclCommInitAll(&(e->communicators[0]), devices.size(), &(devices[0]));
        comms_[devices] = e->communicators;
      } else {
        e->communicators = comms_[devices];
      }
    }
    e->sharded_value.resize(devices.size());
    e->scratch_space.resize(devices.size());
    e->shape = e->initial_value.shape();
    TShape dst_shape = TShape(1);
    dst_shape[0] = e->shape.Size();
    NDArray temp = e->initial_value.Reshape(dst_shape);
    for (size_t j = 0; j < grouped_vals.size(); ++j) {
      index_t stride = (dst_shape.Size() + grouped_vals.size() - 1)/grouped_vals.size();
      index_t begin = std::min(j * stride, dst_shape.Size());
      index_t end = std::min((j + 1) * stride, dst_shape.Size());
      e->sharded_value[j] = temp.Slice(begin, end).Copy((ptr<NDArray>(grouped_vals[j]))->ctx());
      e->scratch_space[j] = NDArray(e->sharded_value[j].shape(),
                                    e->sharded_value[j].ctx(),
                                    false,
                                    e->sharded_value[j].dtype());
    }
    e->sent_to_device = true;
    // free the temporary array
    e->initial_value = NDArray();
  }

  std::vector<NDArray> ReduceScatter(Entry * e,
                                     const std::vector<NDArray> & src,
                                     int priority) {
    std::vector<Engine::VarHandle> const_vars(src.size());
    std::vector<Engine::VarHandle> mutable_vars(src.size());
    index_t stride = (e->shape.Size() + src.size() - 1)/src.size();
    for (size_t i = 0; i < src.size(); ++i) {
      const_vars[i] = src[i].var();
      mutable_vars[i] = e->scratch_space[i].var();
    }
    Engine::Get()->PushSync([e, src, stride, this](RunContext rctx) {
          {
            std::lock_guard<std::mutex> l(storage::GPUPooledStorageManager::mutex_);
            for (size_t i = 0; i < src.size(); ++i) {
              CHECK(src[i].ctx().dev_id == e->scratch_space[i].ctx().dev_id)
                << "Different order of devices in push and pull";
              cudaSetDevice(src[i].ctx().dev_id);
              MSHADOW_TYPE_SWITCH(src[i].dtype(), DType,
              ncclReduceScatter(src[i].data().dptr<DType>(),
                                e->scratch_space[i].data().dptr<DType>(),
                                stride,
                                GetNCCLType(src[i].dtype()),
                                ncclSum,
                                e->communicators[i],
                                streams_[src[i].ctx().dev_id]););
            }
          }
          for (size_t i = 0; i < src.size(); ++i) {
            CUDA_CALL(cudaSetDevice(src[i].ctx().dev_id));
            CUDA_CALL(cudaStreamSynchronize(streams_[src[i].ctx().dev_id]));
          }
        },
        Context::CPU(),
        const_vars,
        mutable_vars,
        FnProperty::kCPUPrioritized,
        priority,
        PROFILER_MESSAGE("KVStoreReduce"));
    return e->scratch_space;
  }

  void AllGather(const Entry & src,
                 const std::vector<NDArray*> & dst,
                 int priority) {
    std::vector<Engine::VarHandle> const_vars(src.sharded_value.size());
    std::vector<Engine::VarHandle> mutable_vars(dst.size());
    index_t stride = (src.shape.Size() + dst.size() - 1)/dst.size();
    for (size_t i = 0; i < src.sharded_value.size(); ++i) {
      const_vars[i] = src.sharded_value[i].var();
    }
    for (size_t i = 0; i < dst.size(); ++i) {
      mutable_vars[i] = dst[i]->var();
    }
    Engine::Get()->PushSync([src, dst, stride, this](RunContext rctx) {
          {
            std::lock_guard<std::mutex> l(storage::GPUPooledStorageManager::mutex_);
            for (size_t i = 0; i < dst.size(); ++i) {
              cudaSetDevice(dst[i]->ctx().dev_id);
              MSHADOW_TYPE_SWITCH(dst[i]->dtype(), DType,
              ncclAllGather(src.sharded_value[i].data().dptr<DType>(),
                            stride,
                            GetNCCLType(src.sharded_value[i].dtype()),
                            dst[i]->data().dptr<DType>(),
                            src.communicators[i],
                            streams_[src.sharded_value[i].ctx().dev_id]););
            }
          }
          for (size_t i = 0; i < dst.size(); ++i) {
            CUDA_CALL(cudaSetDevice(dst[i]->ctx().dev_id));
            CUDA_CALL(cudaStreamSynchronize(streams_[dst[i]->ctx().dev_id]));
          }
        },
        Context::CPU(),
        const_vars,
        mutable_vars,
        FnProperty::kCPUPrioritized,
        priority,
        PROFILER_MESSAGE("KVStoreBroadcast"));
  }

  ncclDataType_t GetNCCLType(int dtype) {
    switch (dtype) {
      case mshadow::kFloat32:
        return ncclFloat;
      case mshadow::kFloat16:
        return ncclHalf;
      case mshadow::kFloat64:
        return ncclDouble;
      case mshadow::kUint8:
        return ncclChar;
      case mshadow::kInt32:
        return ncclInt;
      default:
        LOG(FATAL) << "Unknown type passed to NCCL KVStore";
    }
    return nccl_NUM_TYPES;
  }

  /// \brief buffer for storing local values
  std::unordered_map<int, Entry> local_;
  /// \brief NCCL communicator storage
  std::map<std::vector<int32_t>, std::vector<ncclComm_t>> comms_;
  /// \brief CUDA streams used by NCCL
  std::vector<cudaStream_t> streams_;
};
}  // namespace kvstore
}  // namespace mxnet
#endif  // MXNET_USE_NCCL
#endif  // MXNET_KVSTORE_KVSTORE_NCCL_H_

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

/**
 * @file   kvstore_dist.h
 * @brief  distributed implementation based on ps-lite
 */
#ifndef MXNET_KVSTORE_KVSTORE_DIST_H_
#define MXNET_KVSTORE_KVSTORE_DIST_H_
#include <string>
#include <vector>
#include <algorithm>
#include <bitset>
#include <utility>
#include "./kvstore_local.h"
#include "mxnet/engine.h"
#include "ps/ps.h"
#include "./kvstore_dist_server.h"
#include "../ndarray/ndarray_function.h"
#if MKL_EXPERIMENTAL == 1
#include <mkl_memory.h>
#include "../operator/mkl/mkl_memory-inl.h"
#include "../operator/mkl/mkl_util-inl.h"
#endif
namespace mxnet {
namespace kvstore {

/**
 * \brief distributed kvstore
 *
 * for a worker node, it always guarantees that all push and pull issued from
 * this worker on the same key are serialized. namely push(3) and then pull(3),
 * then the data pulled is always containing the modification from the push(3).
 *
 * it's the server node's job to control the data consistency among all
 * workers. see details on \ref ServerHandle::Start
 */
class KVStoreDist : public KVStoreLocal {
 public:
  explicit KVStoreDist(bool use_device_comm)
      : KVStoreLocal(use_device_comm), ps_worker_(nullptr), server_(nullptr) {
    if (IsWorkerNode()) {
      ps_worker_ = new ps::KVWorker<real_t>(0);
      ps::StartAsync("mxnet\0");
      //what happens during recovery?
      if (!ps::Postoffice::Get()->is_recovery()) {
        ps::Postoffice::Get()->Barrier(
          ps::kWorkerGroup + ps::kServerGroup + ps::kScheduler);
      }
    }
    bigarray_bound_ = dmlc::GetEnv("MXNET_KVSTORE_BIGARRAY_BOUND", 1000 * 1000);
    log_verbose_ = dmlc::GetEnv("MXNET_KVSTORE_DIST_ROW_SPARSE_VERBOSE", false);
  }

  virtual ~KVStoreDist() {
    Engine::Get()->WaitForAll();
    if (IsWorkerNode()) {
      if (barrier_before_exit_) {
        Barrier();
        if (get_rank() == 0) {
          // stop the executor at servers
          SendCommandToServers(kStopServer, "");
        }
      }
      ps::Finalize(barrier_before_exit_);
      delete ps_worker_;
    }
  }

  void set_updater(const Updater& updater) override {
    CHECK(updater) << "invalid updater";
    if (IsServerNode()) {
      CHECK_NOTNULL(server_)->set_updater(updater);
    } else {
      updater_ = updater;
    }
  }

  virtual void SetCompress(const std::string& compress, const float pos_threshold,
                     const float neg_threshold) override {
    KVStoreLocal::SetCompress(compress, pos_threshold, neg_threshold);
    if (get_rank() == 0) {
      SendCommandToServers(kSetCompress, compress_);
    }
    //this fails. everyone just waits. why?
//    Barrier();
//    ps::Postoffice::Get()->Barrier(ps::kWorkerGroup + ps::kServerGroup);
  }

  void Barrier() override {
    ps::Postoffice::Get()->Barrier(ps::kWorkerGroup);
  }

  void SendCommandToServers(int cmd_id,
                            const std::string& cmd_body) override {
    CHECK_NOTNULL(ps_worker_);
    ps_worker_->Wait(ps_worker_->Request(cmd_id, cmd_body, ps::kServerGroup));
  }

  int get_group_size() const override { return ps::NumWorkers(); }

  int get_rank() const override { return ps::MyRank(); }

  int get_num_dead_node(int node_id, int timeout) const override {
    int number = 0;
    auto dead_nodes = ps::Postoffice::Get()->GetDeadNodes(timeout);
    const auto& watch_nodes = ps::Postoffice::Get()->GetNodeIDs(node_id);
    std::unordered_set<int> watch_set(watch_nodes.begin(), watch_nodes.end());
    for (int r : dead_nodes) {
      if (watch_set.find(r) != watch_set.end()) number++;
    }
    return number;
  }

  void RunServer(const Controller& controller) override {
    CHECK(!IsWorkerNode());
    if (IsServerNode()) {
      server_ = new KVStoreDistServer();
      server_->set_controller(controller);
    }

    ps::StartAsync("mxnet_server\0");
    if (!ps::Postoffice::Get()->is_recovery()) {
      ps::Postoffice::Get()->Barrier(
        ps::kWorkerGroup + ps::kServerGroup + ps::kScheduler);
    }
    if (server_) server_->Run();
    ps::Finalize();
    if (server_) {
      delete server_;
    }
    server_ = nullptr;
  }

 private:
  void InitImpl(const std::vector<int>& keys,
                const std::vector<NDArray>& values) override {
    CheckUnique(keys);
    for (size_t i = 0; i < keys.size(); ++i) {
      comm_->Init(keys[i], values[i].storage_type(), values[i].shape(), values[i].dtype());
    }
    if (get_rank() == 0) {
      Push_(keys, values, 0, false);
      // wait until the push is finished
      for (const auto& v : values) {
        v.WaitToWrite();
      }
    } else {
      // do nothing
    }
    if (!ps::Postoffice::Get()->is_recovery()) {
      Barrier();
    }
  }

  void PushImpl(const std::vector<int>& keys,
                const std::vector<NDArray>& values,
                int priority) override {
    Push_(keys, values, priority, true);
  }

  void PullImpl(const std::vector<int>& keys,
                const std::vector<NDArray*>& values,
                int priority) override {
    std::vector<int> uniq_keys;
    std::vector<std::vector<NDArray*> > grouped_vals;
    GroupKVPairsPull(keys, values, &uniq_keys, &grouped_vals);
    for (size_t i = 0; i < uniq_keys.size(); ++i) {
      int key = uniq_keys[i];
      // use the same array for merging to guarantee that pull always happens
      // after the previous push on this key
      auto& recv_buf = comm_buf_[key];
      const auto storage_type = grouped_vals[i][0]->storage_type();
      CHECK_EQ(storage_type, kDefaultStorage)
               << "Expected stype of value to be kDefaultStorage";
      if (recv_buf.is_none()) {
        // it may happen for the first time a no-rank-0 worker pull the weight.
        recv_buf = NDArray(grouped_vals[i][0]->shape(), pinned_ctx_,
                           true, grouped_vals[i][0]->dtype());
      }
      bool is_compressed = (compress_!="none");
      auto pull_from_servers = [this, key, recv_buf, is_compressed](
          RunContext rctx, Engine::CallbackOnComplete cb) {
        // convert to ps keys
        size_t size = recv_buf.shape().Size();
        PSKV& pskv = EncodeKey(key, size, false, is_compressed);
#if MKL_EXPERIMENTAL == 1
        mkl_set_tblob_eager_mode(recv_buf.data());
#endif
        real_t* data = recv_buf.data().dptr<real_t>();
        // false means not to delete data when SArray is deleted

        auto vals = new ps::SArray<real_t>(data, size, false);
        // issue pull

        CHECK_NOTNULL(ps_worker_)->ZPull(
          pskv.keys, vals, &pskv.lens, kDefaultPushPull, [vals, cb](){ delete vals; cb(); });
      };

      CHECK_NOTNULL(Engine::Get())->PushAsync(
          pull_from_servers,
          pinned_ctx_,
          {},
          {recv_buf.var()},
          FnProperty::kNormal,
          priority,
          PROFILER_MESSAGE("KVStoreDistDefaultPull"));
      comm_->Broadcast(key, recv_buf, grouped_vals[i], priority);
    }
  }

  void PullRowSparseImpl(const std::vector<int>& keys,
                         const std::vector<std::pair<NDArray*, NDArray>>& val_rowids,
                         int priority = 0) override {
    std::vector<int> uniq_keys;
    std::vector<std::vector<std::pair<NDArray*, NDArray>>> grouped_val_rowids;
    GroupKVPairsPullRsp(keys, val_rowids, &uniq_keys, &grouped_val_rowids);

    for (size_t i = 0; i < uniq_keys.size(); ++i) {
      int key = uniq_keys[i];
      // use the same array for merging to guarantee that pull always happens
      // after the previous push on this key
      auto& recv_buf = comm_buf_[key];
      auto& grouped_val_rowid = grouped_val_rowids[i];
      const auto storage_type = grouped_val_rowid[0].first->storage_type();
      CHECK_EQ(storage_type, kRowSparseStorage)
               << "expected kRowSparseStorage, but got " << storage_type;
      if (recv_buf.is_none()) {
        // it may happen for the first time a no-rank-0 worker pull the weight.
        recv_buf = NDArray(storage_type, grouped_val_rowid[0].first->shape(),
                           pinned_ctx_, true, grouped_val_rowid[0].first->dtype());
      }
      auto &target_val_rowids = grouped_val_rowids[i];
      const size_t num_vals = target_val_rowids.size();
      size_t num_rows = 0;
      // TODO(haibin) refactor this for loop
      for (size_t i = 0; i < num_vals; i++) {
        auto &row_id = target_val_rowids[i].second;
        NDArray indices = row_id.Copy(pinned_ctx_);
        Unique(&indices, priority);
        target_val_rowids[i].second = indices;
        num_rows += indices.shape().Size();
      }
      if (num_vals > 1) {
        // TODO(haibin) aggregate over all unique indices
        LOG(FATAL) << "RowSparsePull with multiple values is not implemented yet";
      } else {
        auto& indices = target_val_rowids[0].second;
        PullRowSparse_(key, &recv_buf, indices, priority);
        comm_->BroadcastRowSparse(key, recv_buf, grouped_val_rowid, num_vals == 1, priority);
      }
    }
  }

  void Push_(const std::vector<int>& keys,
             const std::vector<NDArray>& values,
             int priority,
             bool do_merge) {
    // first aggregate the values over keys
    std::vector<int> uniq_keys;
    std::vector<std::vector<NDArray> > grouped_vals;
    GroupKVPairsPush(keys, values, &uniq_keys, &grouped_vals);

    for (size_t i = 0; i < uniq_keys.size(); ++i) {
      // merge over devices
      int key = uniq_keys[i];
      const auto& vals = grouped_vals[i];
      NDArray merged = do_merge ? comm_->Reduce(key, vals, priority) : vals[0];
      const auto storage_type = merged.storage_type();

      auto& comm_buf = comm_buf_[key];
      if (merged.ctx().dev_mask() == cpu::kDevMask) {
        // make sure the previous push/pull is completed
        comm_buf.WaitToWrite();
        comm_buf = merged;  // avoid memory copy
      } else {
        if (comm_buf.is_none()) {
          if (storage_type == kDefaultStorage) {
            comm_buf = NDArray(merged.shape(), pinned_ctx_, true, merged.dtype());
          } else {
            comm_buf = NDArray(storage_type, merged.shape(), pinned_ctx_, true, merged.dtype());
          }
        }
        CopyFromTo(merged, &comm_buf);
      }
      auto& small_buf = comm_small_buf_[key];
      auto& res_buf = residual_[key];
      if (compress_ != "none") {
        // Init the small buffer and residual_ buffer for quantize
        if (small_buf.is_none()) {
          int bits = compress_ == "2bit" ? 16 : 32;
          long int small_size = merged.shape().Size() % bits == 0 ?
                        merged.shape().Size() / bits + 3 :
                        merged.shape().Size() / bits + 4;
          // small buffer for quantize
          small_buf = NDArray(TShape{small_size}, comm_buf.ctx(), false, comm_buf.dtype());
          // residual buffer for quantize
          res_buf = NDArray(merged.shape(), comm_buf.ctx(), false, comm_buf.dtype());
          res_buf = 0;
          if (pos_thre_.is_none()) {
            // positive threshold
            pos_thre_ = NDArray(TShape{1}, comm_buf.ctx(), false, mshadow::kFloat32);
            pos_thre_ = pos_threshold_;
            // negative threshold
            neg_thre_ = NDArray(TShape{1}, comm_buf.ctx(), false, mshadow::kFloat32);
            neg_thre_ = neg_threshold_;
          }
        }

        if (compress_ == "2bit") {
          Quantize(comm_buf, &small_buf, &res_buf, pos_thre_, neg_thre_, compress_, priority);
        //  small_buf.WaitToRead();
          //res_buf.WaitToRead();
          //std::cout<<"Original data is "<<*((float *) comm_buf.data().dptr_)<<std::endl;
          //std::bitset<sizeof(float)*CHAR_BIT> foo(*reinterpret_cast<unsigned long*>((((float *) small_buf.data().dptr_)+3)));
          //std::cout<<"Compressed buf is "<<*((float *) small_buf.data().dptr_)<<" "
            //       << *(((float *) small_buf.data().dptr_)+1) << " "
              //     << *(((float *) small_buf.data().dptr_)+2) << " "
                //   << foo << " " << *(((float *) small_buf.data().dptr_)+3) << std::endl;
          //std::cout<<"Res buf is "<< *((float *) res_buf.data().dptr_) <<std::endl;
        } else {
          LOG(FATAL) << "Unsupported quantization";
        }
      }
      // push to servers
      if (storage_type == kDefaultStorage) {
        if (compress_ == "none") {
          PushDefault(key, comm_buf, priority);
        } else {
          PushCompressed(key, comm_buf, small_buf, priority);
        }
      } else if (storage_type == kRowSparseStorage) {
        PushRowSparse(key, comm_buf, priority);
      } else {
        LOG(FATAL) << "unknown storage type";
      }
    }
  }


  void PushCompressed(int key, NDArray& comm_buf, NDArray &send_buf, int priority){
    std::cout<<"send buf data"<<* (float*) (send_buf.data().dptr_)<<" "<<* ((float*)send_buf.data().dptr_+3)<<std::endl;
    auto& comm_small_send_buf = comm_small_send_buf_[key];
    PSKV& pskv = EncodeCompressedKey(key, send_buf.shape().Size(), true);
    std::vector<Engine::VarHandle> const_vars;
    const_vars.push_back(comm_buf.var());
    if (comm_buf.shape().Size() > bigarray_bound_) {
      if (comm_small_send_buf.is_none()) {
        comm_small_send_buf = NDArray(TShape{pskv.size}, comm_buf.ctx(), false, comm_buf.dtype());
      }
      size_t prev_from = 3;
      size_t prev_to = 0;
      int cursize = 0;
      int original_size = comm_buf.shape().Size();
      CHECK_GT(original_size,0);
      NDArray meta = send_buf.Slice(0,3);
      for(size_t i = 0; i<pskv.keys.size(); i++) {
        NDArray part_meta = comm_small_send_buf.Slice(prev_to, prev_to+3);
        NDArray part_data = comm_small_send_buf.Slice(prev_to+3, prev_to+pskv.lens[i]);
        CopyFromTo(meta, &part_meta);
        int part_num_orig = (pskv.lens[i]-3)*16;
        part_meta.At(2) = part_num_orig;
        cursize += part_num_orig;
        CopyFromTo(send_buf.Slice(prev_from, prev_from+pskv.lens[i]-3), &part_data);
        prev_to += pskv.lens[i];
        prev_from += pskv.lens[i]-3;
      }
      comm_small_send_buf.WaitToRead();
      std::cout<<"commsmallsendbuf data"<<* (float*) (comm_small_send_buf.data().dptr_)<<" "<<* ((float*)comm_small_send_buf.data().dptr_+1)<<" "<<* ((float*)comm_small_send_buf.data().dptr_+2)<<" "<<* ((float*)comm_small_send_buf.data().dptr_+3)<<std::endl;

      CHECK_EQ(original_size, cursize);
      const_vars.push_back(comm_small_send_buf.var());
    } else {
      //if compress is set, then send_buf is different from comm_buf
      const_vars.push_back(send_buf.var());
    }

    auto push_to_servers =
      [this, key, pskv, comm_buf, &comm_small_send_buf, send_buf](RunContext rctx, Engine::CallbackOnComplete cb) {
        // convert to ps keys
        size_t size = 0;
        real_t* data = nullptr;
        if ( comm_buf.shape().Size() > bigarray_bound_ ){
          data = comm_small_send_buf.data().dptr<real_t>();
          #if MKL_EXPERIMENTAL == 1
          mkl_set_tblob_eager_mode(comm_small_send_buf.data());
          #endif
          size = comm_small_send_buf.shape().Size();
        } else {
          data = send_buf.data().dptr<real_t>();
          #if MKL_EXPERIMENTAL == 1
          mkl_set_tblob_eager_mode(send_buf.data());
          #endif
          size = send_buf.shape().Size();
        }
        // do push. false means no delete
        ps::SArray<real_t> vals(data, size, false);
        CHECK_NOTNULL(ps_worker_)->ZPush(
          pskv.keys, vals, pskv.lens, kDefaultPushPull, [cb]() { cb(); });
      };
    Engine::Get()->PushAsync(
      push_to_servers,
      pinned_ctx_,
      const_vars,
      {},
      FnProperty::kNormal,
      priority,
      PROFILER_MESSAGE("KVStoreDistCompressedPush"));
  }

  void PushDefault(int key, NDArray &send_buf, int priority){
    auto push_to_servers =
        [this, key, send_buf](RunContext rctx, Engine::CallbackOnComplete cb) {
          // convert to ps keys
          size_t size = 0;
          real_t* data = nullptr;
          size = send_buf.shape().Size();
          data = send_buf.data().dptr<real_t>();
#if MKL_EXPERIMENTAL == 1
          mkl_set_tblob_eager_mode(send_buf.data());
#endif
          PSKV& pskv = EncodeKey(key, size, true);
          // do push. false means no delete
          ps::SArray<real_t> vals(data, size, false);
          CHECK_NOTNULL(ps_worker_)->ZPush(
              pskv.keys, vals, pskv.lens, kDefaultPushPull, [cb]() { cb(); });
        };
    Engine::Get()->PushAsync(
        push_to_servers,
        pinned_ctx_,
        {send_buf.var()},
        {},
        FnProperty::kNormal,
        priority,
        PROFILER_MESSAGE("KVStoreDistDefaultPush"));
  }

  // pull row sparse weight into `recv_buf` based on indices given by `indices`
  void PullRowSparse_(const int key, NDArray *recv_buf, const NDArray& indices, int priority) {
    using namespace rowsparse;
    auto pull_from_servers = [this, key, recv_buf, indices]
                             (RunContext rctx, Engine::CallbackOnComplete cb) {
      // allocate memory for the buffer
      size_t num_rows = indices.shape().Size();
      recv_buf->CheckAndAlloc({mshadow::Shape1(num_rows)});
#if MKL_EXPERIMENTAL == 1
      mkl_set_tblob_eager_mode(recv_buf->data());
#endif
      real_t* data = recv_buf->data().dptr<real_t>();
      auto indices_data = indices.data();
      const auto offsets = indices_data.dptr<int64_t>();
      const auto unit_len = recv_buf->shape().ProdShape(1, recv_buf->shape().ndim());
      const int64_t size = num_rows * unit_len;
       // convert to ps keys in row sparse format
      PSKV& pskv = EncodeRowSparseKey(key, size, num_rows, offsets,
                                      unit_len, recv_buf->shape()[0]);
      if (this->log_verbose_) {
        LOG(INFO) << "worker " << get_rank() << " pull lens: " << pskv.lens << " keys: "
                  << pskv.keys << " size: " << size;
      }
      auto vals = new ps::SArray<real_t>(data, size, false);
      CHECK_NOTNULL(ps_worker_)->ZPull(pskv.keys, vals, &pskv.lens, kRowSparsePushPull,
        [vals, cb]() { delete vals; cb(); });
      // copy indices to recv_buf
      mshadow::Copy(recv_buf->aux_data(kIdx).FlatTo1D<cpu, int64_t>(),
                    indices_data.FlatTo1D<cpu, int64_t>());
    };
    CHECK_NOTNULL(Engine::Get())->PushAsync(
        pull_from_servers,
        pinned_ctx_,
        {indices.var()},
        {recv_buf->var()},
        FnProperty::kNormal,
        priority,
        PROFILER_MESSAGE("KVStoreDistRowSparsePull"));
  }

  // push row sparse gradient
  void PushRowSparse(int key, const NDArray &send_buf, int priority) {
    using namespace rowsparse;
    auto push_to_servers = [this, key, &send_buf]
                           (RunContext rctx, Engine::CallbackOnComplete cb) {
#if MKL_EXPERIMENTAL == 1
      mkl_set_tblob_eager_mode(send_buf.data());
#endif
      real_t* data = send_buf.data().dptr<real_t>();
      bool init = send_buf.storage_initialized();
      const int64_t num_rows = init ? send_buf.aux_shape(kIdx)[0] : 0;
      const auto offsets = init ? send_buf.aux_data(kIdx).dptr<int64_t>() : nullptr;
      const auto unit_len = send_buf.shape().ProdShape(1, send_buf.shape().ndim());
      const int64_t size = num_rows * unit_len;

       // convert to ps keys in row sparse format
      PSKV& pskv = EncodeRowSparseKey(key, size, num_rows, offsets,
                                      unit_len, send_buf.shape()[0]);
      if (this->log_verbose_) {
        LOG(INFO) << "worker " << get_rank() << " push lens: " << pskv.lens << " keys: "
                  << pskv.keys << " size: " << size;
      }
      ps::SArray<real_t> vals(data, size, false);
      CHECK_NOTNULL(ps_worker_)->ZPush(pskv.keys, vals, pskv.lens, kRowSparsePushPull, [cb]() {
        cb();
      });
    };
    Engine::Get()->PushAsync(
        push_to_servers,
        pinned_ctx_,
        {send_buf.var()},
        {},
        FnProperty::kNormal,
        priority,
        PROFILER_MESSAGE("KVStoreDistRowSparsePush"));
  }

  /**
   * \brief check if the keys are all unique
   */
  void CheckUnique(const std::vector<int>& keys) {
    auto keys_copy = keys;
    auto last = std::unique(keys_copy.begin(), keys_copy.end());
    CHECK_EQ(static_cast<size_t>(std::distance(keys_copy.begin(), last)),
             static_cast<size_t>(keys.size()));
  }

  /**
   * \brief struct for ps keys and lens
   */
  struct PSKV {
    ps::SArray<ps::Key> keys;  // n keys
    ps::SArray<int> lens;  // the length of the i-th value
    int size;
  };

  /**
   * \brief cache all key partitions
   */
  std::unordered_map<int, PSKV> ps_kv_;
  std::unordered_map<int, PSKV> push_ps_kv_;
  std::unordered_map<int, PSKV> pull_ps_kv_;
  /**
   * \brief serizelize EncodeRowSparseKey and EncodeKey
   */
  std::mutex mu_;

  size_t roundUp(size_t numToRound, size_t  multiple)
  {
    assert(multiple && ((multiple & (multiple - 1)) == 0));
    return (numToRound + multiple - 1) & -multiple;
  }


  PSKV& EncodeKey(int key, size_t size, bool is_push, bool is_compressed) {
    if (is_compressed) {
      return EncodeCompressedKey(key, size, is_push);
    } else {
      return EncodeKey(key, size, is_push);
    }
  }

  /**
   * \brief convert to keys in ps for compressed values
   * \brief buf_size will be size of recv_buf (original size) if pull
   * buf_size will be size of quantized array if push. Actual size of
   * send_buf in this case will add few counts of meta information
   * to each part if divided
   */
  inline PSKV& EncodeCompressedKey(int key, size_t buf_size, bool is_push) {
    size_t original_size = comm_buf_[key].shape().Size();
    size_t size = (is_push) ? buf_size : original_size;
    mu_.lock();
    PSKV& pskv = (is_push) ? push_ps_kv_[key] : pull_ps_kv_[key];
    mu_.unlock();
    if (!pskv.keys.empty()) {
      //will fail
//      CHECK_EQ(static_cast<size_t>(pskv.size), size)<< "The value size cannot be changed";
    } else {
      auto krs = ps::Postoffice::Get()->GetServerKeyRanges();
      int num_servers = krs.size();
      CHECK_GT(num_servers, 0);
      // a simple heuristic for load balance
      if (original_size < bigarray_bound_) {
        // send it to a single random picked server
        int server = (key * 9973) % num_servers;
        ps::Key ps_key = krs[server].begin() + key;
        CHECK_LT(ps_key, krs[server].end());
        pskv.keys.push_back(ps_key);
        pskv.lens.push_back(size);
        pskv.size = size;
      } else {
        // partition it to all servers
        pskv.size = 0;
        size_t final_size;
        if (is_push) {
          final_size = buf_size+3*(num_servers-1);
          for (int i = 0; i < num_servers; ++i) {
            //if pushing, divide size of compressed array into blocks of 16, so we don't split between a compressed value
            //if pulling, need to divide exact way as above did
            size_t part_size = is_push? (roundUp((size-3)/num_servers*(i+1), 16) - roundUp((size-3)/num_servers*(i), 16) + 3)
                                      : (roundUp((size)/num_servers*(i+1), 1) - roundUp((size)/num_servers*(i), 1));
            ps::Key ps_key = krs[i].begin() + key;
            CHECK_LT(ps_key, krs[i].end());
            pskv.keys.push_back(ps_key);

            //if last block was rounded up to beyond size of our data, set it to end of data
            if (i == num_servers-1 && ((pskv.size+part_size) > final_size)) {
              part_size = buf_size + 3*(num_servers-1) - pskv.size;
            }
            pskv.lens.push_back(part_size);
            pskv.size += part_size;
          }
          CHECK_EQ(static_cast<size_t>(pskv.size), final_size);
        } else {
          mu_.lock();
          PSKV& push_pskv = push_ps_kv_[key];
          mu_.unlock();
          for (int i=0; i<push_pskv.lens.size(); i++) {
            pskv.keys.push_back(push_pskv.keys[i]);
            pskv.lens.push_back(((push_pskv.lens[i])-3)*16);
            pskv.size += pskv.lens[i];
          }
          CHECK_EQ(pskv.size, original_size);
        }

      }
    }
    return pskv;
  }

  /**
   * \brief convert to keys in ps
   */
  inline PSKV& EncodeKey(int key, size_t size, bool is_push) {
    mu_.lock();
    PSKV& pskv = (is_push) ? push_ps_kv_[key] : pull_ps_kv_[key];
    mu_.unlock();
    if (!pskv.keys.empty()) {
      CHECK_EQ(static_cast<size_t>(pskv.size), size) << "The value size cannot be changed";
    } else {
      auto krs = ps::Postoffice::Get()->GetServerKeyRanges();
      int num_servers = krs.size();
      CHECK_GT(num_servers, 0);

      // a simple heuristic for load balance
      if (size < bigarray_bound_) {
        // send it to a single random picked server
        int server = (key * 9973) % num_servers;
        ps::Key ps_key = krs[server].begin() + key;
        CHECK_LT(ps_key, krs[server].end());
        pskv.keys.push_back(ps_key);
        pskv.lens.push_back(size);
        pskv.size = size;
      } else {
        // parition it to all servers
        pskv.size = 0;
        for (int i = 0; i < num_servers; ++i) {
          size_t part_size =
              static_cast<size_t>(round(static_cast<double>(size)/num_servers*(i+1))) -
              static_cast<size_t>(round(static_cast<double>(size)/num_servers*i));
          ps::Key ps_key = krs[i].begin() + key;
          CHECK_LT(ps_key, krs[i].end());
          pskv.keys.push_back(ps_key);
          pskv.lens.push_back(part_size);
          pskv.size += part_size;
        }
        CHECK_EQ(static_cast<size_t>(pskv.size), size);
      }
    }
    return pskv;
  }

  // TODO(haibin) this encoding method for row sparse keys doesn't allow cross-layer batching
  inline PSKV& EncodeRowSparseKey(const int key, const int64_t size, const int64_t num_rows,
                                  const int64_t *offsets, const size_t unit_len,
                                  const int64_t total_num_rows) {
    using namespace common;
    mu_.lock();
    PSKV& pskv = ps_kv_[key];
    mu_.unlock();
    pskv.keys.clear();
    pskv.lens.clear();
    // TODO(haibin) cache this information
    auto krs = ps::Postoffice::Get()->GetServerKeyRanges();
    int num_servers = krs.size();
    CHECK_GT(num_servers, 0);

    if (total_num_rows * unit_len >= bigarray_bound_) {
      pskv.size = 0;
      int64_t start_row = 0;
      // parition it to all servers
      for (int i = 0; i < num_servers; ++i) {
        // calculate partition ranges
        int64_t part_num_rows =
            llround(static_cast<double>(total_num_rows) / num_servers * (i + 1)) -
            llround(static_cast<double>(total_num_rows) / num_servers * i);
        auto end_row = start_row + part_num_rows;
        auto lb = std::lower_bound(offsets, offsets + num_rows, start_row);
        auto ub = std::upper_bound(offsets, offsets + num_rows, end_row - 1);
        ps::Key master_key = krs[i].begin() + key;
        pskv.keys.push_back(master_key);
        pskv.lens.push_back(0);
        for (auto offset = lb; offset < ub; offset++) {
          ps::Key ps_key = krs[i].begin() + key + (*offset - start_row);
          CHECK_LT(ps_key, krs[i].end());
          pskv.keys.push_back(ps_key);
          pskv.lens.push_back(unit_len);
          pskv.size += unit_len;
        }
        start_row = end_row;
      }
      CHECK_EQ(static_cast<size_t>(pskv.size), size);
    } else {
      // send it to a single random picked server
      int server = (key * 9973) % num_servers;
      ps::Key master_key = krs[server].begin() + key;
      pskv.keys.push_back(master_key);
      pskv.lens.push_back(0);
      for (int64_t i = 0; i < num_rows; i++) {
        ps::Key ps_key = krs[server].begin() + key + offsets[i];
        CHECK_LT(ps_key, krs[server].end());
        pskv.keys.push_back(ps_key);
        pskv.lens.push_back(unit_len);
      }
      pskv.size = size;
    }
    return pskv;
  }


  /**
   * \brief for worker to push and pull data
   */
  ps::KVWorker<real_t>* ps_worker_;
  /**
   * \brief the server handle
   */
  KVStoreDistServer* server_;
  /**
   * \brief threshold for partition
   */
  size_t bigarray_bound_;
  std::unordered_map<int, NDArray> comm_buf_;
  /// \brief small buffer for quantize
  std::unordered_map<int, NDArray> comm_small_buf_;
  std::unordered_map<int, NDArray> comm_small_send_buf_;
  /// \brief residual buffer for quantize
  std::unordered_map<int, NDArray> residual_;
  /// \brief threshold for quantize
  NDArray pos_thre_;
  NDArray neg_thre_;

  bool log_verbose_;
};

}  // namespace kvstore
}  // namespace mxnet


#endif  // MXNET_KVSTORE_KVSTORE_DIST_H_

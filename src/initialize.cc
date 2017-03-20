/*!
 *  Copyright (c) 2016 by Contributors
 * \file initialize.cc
 * \brief initialize mxnet library
 */
#include <dmlc/logging.h>
#include <mxnet/engine.h>
#include <mutex>

#include "engine/profiler.h"

namespace mxnet {

class LibraryInitializer {
 public:
  LibraryInitializer() {
    dmlc::InitLogging("mxnet");
#if MXNET_USE_PROFILER
    static std::once_flag dump_profile_flag;
    std::call_once(dump_profile_flag, []() {
      // ensure engine's and profiler's constructor are called before atexit.
      Engine::Get();
      engine::Profiler::Get();
      // DumpProfile will be called before engine's and profiler's destructor.
      std::atexit([](){
        engine::Profiler* profiler = engine::Profiler::Get();
        if (profiler->IsEnableOutput()) {
          profiler->DumpProfile();
        }
      });
    });
#endif
  }
};

static LibraryInitializer __library_init;
}  // namespace mxnet


// -*- mode: groovy -*-

// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// Jenkins pipeline
// See documents at https://jenkins.io/doc/book/pipeline/jenkinsfile/

// mxnet libraries
mx_lib = 'lib/libmxnet.so, lib/libmxnet.a, 3rdparty/dmlc-core/libdmlc.a, 3rdparty/tvm/nnvm/lib/libnnvm.a'
// for scala build, need to pass extra libs when run with dist_kvstore
mx_dist_lib = 'lib/libmxnet.so, lib/libmxnet.a, 3rdparty/dmlc-core/libdmlc.a, 3rdparty/tvm/nnvm/lib/libnnvm.a, 3rdparty/ps-lite/build/libps.a, deps/lib/libprotobuf-lite.a, deps/lib/libzmq.a'
// mxnet cmake libraries, in cmake builds we do not produce a libnvvm static library by default.
mx_cmake_lib = 'build/libmxnet.so, build/libmxnet.a, build/3rdparty/dmlc-core/libdmlc.a, build/tests/mxnet_unit_tests, build/3rdparty/openmp/runtime/src/libomp.so'
// mxnet cmake libraries, in cmake builds we do not produce a libnvvm static library by default.
mx_cmake_lib_debug = 'build/libmxnet.so, build/libmxnet.a, build/3rdparty/dmlc-core/libdmlc.a, build/tests/mxnet_unit_tests'
mx_cmake_mkldnn_lib = 'build/libmxnet.so, build/libmxnet.a, build/3rdparty/dmlc-core/libdmlc.a, build/tests/mxnet_unit_tests, build/3rdparty/openmp/runtime/src/libomp.so, build/3rdparty/mkldnn/src/libmkldnn.so.0'
mx_mkldnn_lib = 'lib/libmxnet.so, lib/libmxnet.a, lib/libiomp5.so, lib/libmkldnn.so.0, lib/libmklml_intel.so, 3rdparty/dmlc-core/libdmlc.a, 3rdparty/tvm/nnvm/lib/libnnvm.a'
mx_tensorrt_lib = 'lib/libmxnet.so, lib/libnvonnxparser_runtime.so.0, lib/libnvonnxparser.so.0, lib/libonnx_proto.so, lib/libonnx.so'
mx_lib_cpp_examples = 'lib/libmxnet.so, lib/libmxnet.a, 3rdparty/dmlc-core/libdmlc.a, 3rdparty/tvm/nnvm/lib/libnnvm.a, 3rdparty/ps-lite/build/libps.a, deps/lib/libprotobuf-lite.a, deps/lib/libzmq.a, build/cpp-package/example/lenet, build/cpp-package/example/alexnet, build/cpp-package/example/googlenet, build/cpp-package/example/lenet_with_mxdataiter, build/cpp-package/example/resnet, build/cpp-package/example/mlp, build/cpp-package/example/mlp_cpu, build/cpp-package/example/mlp_gpu, build/cpp-package/example/test_score, build/cpp-package/example/test_optimizer'
mx_lib_cpp_examples_cpu = 'build/libmxnet.so, build/cpp-package/example/mlp_cpu'

// timeout in minutes
max_time = 120


// Python unittest for CPU
// Python 2
def python2_ut(docker_container_name) {
  timeout(time: max_time, unit: 'MINUTES') {
    utils.docker_run(docker_container_name, 'unittest_ubuntu_python2_cpu', false)
  }
}

// Python 3
def python3_ut(docker_container_name) {
  timeout(time: max_time, unit: 'MINUTES') {
    utils.docker_run(docker_container_name, 'unittest_ubuntu_python3_cpu', false)
  }
}

// Python 3
def python3_ut_asan(docker_container_name) {
  timeout(time: max_time, unit: 'MINUTES') {
    utils.docker_run(docker_container_name, 'unittest_ubuntu_python3_cpu_asan', false)
  }
}

def python3_ut_mkldnn(docker_container_name) {
  timeout(time: max_time, unit: 'MINUTES') {
    utils.docker_run(docker_container_name, 'unittest_ubuntu_python3_cpu_mkldnn', false)
  }
}

// GPU test has two parts. 1) run unittest on GPU, 2) compare the results on
// both CPU and GPU
// Python 2
def python2_gpu_ut(docker_container_name) {
  timeout(time: max_time, unit: 'MINUTES') {
    utils.docker_run(docker_container_name, 'unittest_ubuntu_python2_gpu', true)
  }
}

// Python 3
def python3_gpu_ut(docker_container_name) {
  timeout(time: max_time, unit: 'MINUTES') {
    utils.docker_run(docker_container_name, 'unittest_ubuntu_python3_gpu', true)
  }
}

// Python 3 NOCUDNN
def python3_gpu_ut_nocudnn(docker_container_name) {
  timeout(time: max_time, unit: 'MINUTES') {
    utils.docker_run(docker_container_name, 'unittest_ubuntu_python3_gpu_nocudnn', true)
  }
}

def deploy_docs() {
  parallel 'Docs': {
    node(NODE_LINUX_CPU) {
      ws('workspace/docs') {
        timeout(time: max_time, unit: 'MINUTES') {
          utils.init_git()
          utils.docker_run('ubuntu_cpu', 'deploy_docs', false)
          sh "ci/other/ci_deploy_doc.sh ${env.BRANCH_NAME} ${env.BUILD_NUMBER}"
        }
      }
    }
  },
  'Julia docs': {
    node(NODE_LINUX_CPU) {
      ws('workspace/julia-docs') {
        timeout(time: max_time, unit: 'MINUTES') {
          utils.unpack_and_init('cpu', mx_lib)
          utils.docker_run('ubuntu_cpu', 'deploy_jl_docs', false)
        }
      }
    }
  }
}

node('mxnetlinux-cpu') {
  // Loading the utilities requires a node context unfortunately
  checkout scm
  utils = load('ci/Jenkinsfile_utils.groovy')
}
utils.assign_node_labels(linux_cpu: 'mxnetlinux-cpu', linux_gpu: 'mxnetlinux-gpu', linux_gpu_p3: 'mxnetlinux-gpu-p3', windows_cpu: 'mxnetwindows-cpu', windows_gpu: 'mxnetwindows-gpu')

utils.main_wrapper(
core_logic: {
  stage('Sanity Check') {
    parallel 'Lint': {
      node(NODE_LINUX_CPU) {
        ws('workspace/sanity-lint') {
          utils.init_git()
          utils.docker_run('ubuntu_cpu', 'sanity_check', false)
        }
      }
    },
    'RAT License': {
      node(NODE_LINUX_CPU) {
        ws('workspace/sanity-rat') {
          utils.init_git()
          utils.docker_run('ubuntu_rat', 'nightly_test_rat_check', false)
        }
      }
    }
  }

  stage('Build') {
    parallel 'GPU: CentOS 7': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-centos7-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('centos7_gpu', 'build_centos7_gpu', false)
            utils.pack_lib('centos7_gpu', mx_lib, true)
          }
        }
      }
    }
  } // End of stage('Build')

  stage('Tests') {
    parallel 'Python3: CentOS 7 GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/build-centos7-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            try {
              utils.unpack_and_init('centos7_gpu', mx_lib, true)
              utils.docker_run('centos7_gpu', 'unittest_centos7_gpu', true)
              utils.publish_test_coverage()
            } finally {
              utils.collect_test_results_unix('nosetests_gpu.xml', 'nosetests_python3_centos7_gpu.xml')
            }
          }
        }
      }
    }
  }

  stage('Deploy') {
    deploy_docs()
  }
}
,
failure_handler: {
  // Only send email if master or release branches failed
  if (currentBuild.result == "FAILURE" && (env.BRANCH_NAME == "master" || env.BRANCH_NAME.startsWith("v"))) {
    emailext body: 'Build for MXNet branch ${BRANCH_NAME} has broken. Please view the build at ${BUILD_URL}', replyTo: '${EMAIL}', subject: '[BUILD FAILED] Branch ${BRANCH_NAME} build ${BUILD_NUMBER}', to: '${EMAIL}'
  }
}
)

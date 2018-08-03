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
mx_cmake_mkldnn_lib = 'build/libmxnet.so, build/libmxnet.a, build/3rdparty/dmlc-core/libdmlc.a, build/tests/mxnet_unit_tests, build/3rdparty/openmp/runtime/src/libomp.so, build/3rdparty/mkldnn/src/libmkldnn.so.0'
mx_mkldnn_lib = 'lib/libmxnet.so, lib/libmxnet.a, lib/libiomp5.so, lib/libmkldnn.so.0, lib/libmklml_intel.so, 3rdparty/dmlc-core/libdmlc.a, 3rdparty/tvm/nnvm/lib/libnnvm.a'
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

node('mxnetlinux-cpu') {
  // Loading the utilities requires a node context unfortunately
  checkout scm
  utils = load('ci/Jenkinsfile_utils.groovy')
}
utils.assign_node_labels(linux_cpu: 'mxnetlinux-cpu', linux_gpu: 'mxnetlinux-gpu', linux_cpu_p3: 'mxnetlinux-gpu-p3', windows_cpu: 'mxnetwindows-cpu', windows_gpu: 'mxnetwindows-gpu')

utils.main_wrapper(
handler: {
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
    parallel 'CPU: CentOS 7': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-centos7-cpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('centos7_cpu', 'build_centos7_cpu', false)
            utils.pack_lib('centos7_cpu')
          }
        }
      }
    },
    'CPU: CentOS 7 MKLDNN': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-centos7-mkldnn') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('centos7_cpu', 'build_centos7_mkldnn', false)
            utils.pack_lib('centos7_mkldnn')
          }
        }
      }
    },
    'GPU: CentOS 7': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-centos7-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('centos7_gpu', 'build_centos7_gpu', false)
            utils.pack_lib('centos7_gpu')
          }
        }
      }
    },
    'CPU: Openblas': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-cpu-openblas') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('ubuntu_cpu', 'build_ubuntu_cpu_openblas', false)
            utils.pack_lib('cpu', mx_dist_lib)
          }
        }
      }
    },
    'CPU: Clang 3.9': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-cpu-clang39') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('ubuntu_cpu', 'build_ubuntu_cpu_clang39', false)
          }
        }
      }
    },
    'CPU: Clang 5': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-cpu-clang50') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('ubuntu_cpu', 'build_ubuntu_cpu_clang50', false)
          }
        }
      }
    },
    'CPU: Clang 3.9 MKLDNN': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-cpu-mkldnn-clang39') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('ubuntu_cpu', 'build_ubuntu_cpu_clang39_mkldnn', false)
            utils.pack_lib('mkldnn_cpu_clang3', mx_mkldnn_lib)
          }
        }
      }
    },
    'CPU: Clang 5 MKLDNN': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-cpu-mkldnn-clang50') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('ubuntu_cpu', 'build_ubuntu_cpu_clang50_mkldnn', false)
            utils.pack_lib('mkldnn_cpu_clang5', mx_mkldnn_lib)
          }
        }
      }
    },
    'CPU: MKLDNN': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-mkldnn-cpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('ubuntu_cpu', 'build_ubuntu_cpu_mkldnn', false)
            utils.pack_lib('mkldnn_cpu', mx_mkldnn_lib)
          }
        }
      }
    },
    'GPU: MKLDNN': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-mkldnn-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('ubuntu_build_cuda', 'build_ubuntu_gpu_mkldnn', false)
            utils.pack_lib('mkldnn_gpu', mx_mkldnn_lib)
          }
        }
      }
    },
    'GPU: MKLDNN_CUDNNOFF': {
       node(NODE_LINUX_CPU) {
         ws('workspace/build-mkldnn-gpu-nocudnn') {
           timeout(time: max_time, unit: 'MINUTES') {
             utils.init_git()
             utils.docker_run('ubuntu_build_cuda', 'build_ubuntu_gpu_mkldnn_nocudnn', false)
             utils.pack_lib('mkldnn_gpu_nocudnn', mx_mkldnn_lib)
           }
         }
       }
    },
    'GPU: CUDA9.1+cuDNN7': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('ubuntu_build_cuda', 'build_ubuntu_gpu_cuda91_cudnn7', false)
            utils.pack_lib('gpu', mx_dist_lib)
            stash includes: 'build/cpp-package/example/lenet', name: 'cpp_lenet'
            stash includes: 'build/cpp-package/example/alexnet', name: 'cpp_alexnet'
            stash includes: 'build/cpp-package/example/googlenet', name: 'cpp_googlenet'
            stash includes: 'build/cpp-package/example/lenet_with_mxdataiter', name: 'cpp_lenet_with_mxdataiter'
            stash includes: 'build/cpp-package/example/resnet', name: 'cpp_resnet'
            stash includes: 'build/cpp-package/example/mlp', name: 'cpp_mlp'
            stash includes: 'build/cpp-package/example/mlp_cpu', name: 'cpp_mlp_cpu'
            stash includes: 'build/cpp-package/example/mlp_gpu', name: 'cpp_mlp_gpu'
            stash includes: 'build/cpp-package/example/test_score', name: 'cpp_test_score'
            stash includes: 'build/cpp-package/example/test_optimizer', name: 'cpp_test_optimizer'
          }
        }
      }
    },
    'Amalgamation MIN': {
      node(NODE_LINUX_CPU) {
        ws('workspace/amalgamationmin') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('ubuntu_cpu', 'build_ubuntu_amalgamation_min', false)
          }
        }
      }
    },
    'Amalgamation': {
      node(NODE_LINUX_CPU) {
        ws('workspace/amalgamation') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('ubuntu_cpu', 'build_ubuntu_amalgamation', false)
          }
        }
      }
    },

    'GPU: CMake MKLDNN': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-cmake-mkldnn-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('ubuntu_gpu', 'build_ubuntu_gpu_cmake_mkldnn', false)
            utils.pack_lib('cmake_mkldnn_gpu', mx_cmake_mkldnn_lib)
          }
        }
      }
    },
    'GPU: CMake': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-cmake-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('ubuntu_gpu', 'build_ubuntu_gpu_cmake', false)
            utils.pack_lib('cmake_gpu', mx_cmake_lib)
          }
        }
      }
    },
    'Build CPU windows':{
      node(NODE_WINDOWS_CPU) {
        timeout(time: max_time, unit: 'MINUTES') {
          ws('workspace/build-cpu') {
            withEnv(['OpenBLAS_HOME=C:\\mxnet\\openblas', 'OpenCV_DIR=C:\\mxnet\\opencv_vc14', 'CUDA_PATH=C:\\CUDA\\v8.0']) {
              utils.init_git_win()
              bat """mkdir build_vc14_cpu
                call "C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\bin\\x86_amd64\\vcvarsx86_amd64.bat"
                cd build_vc14_cpu
                cmake -G \"Visual Studio 14 2015 Win64\" -DUSE_CUDA=0 -DUSE_CUDNN=0 -DUSE_NVRTC=0 -DUSE_OPENCV=1 -DUSE_OPENMP=1 -DUSE_PROFILER=1 -DUSE_BLAS=open -DUSE_LAPACK=1 -DUSE_DIST_KVSTORE=0 -DUSE_MKL_IF_AVAILABLE=0 ${env.WORKSPACE}"""
              bat 'C:\\mxnet\\build_vc14_cpu.bat'

              bat '''rmdir /s/q pkg_vc14_cpu
                mkdir pkg_vc14_cpu\\lib
                mkdir pkg_vc14_cpu\\python
                mkdir pkg_vc14_cpu\\include
                mkdir pkg_vc14_cpu\\build
                copy build_vc14_cpu\\Release\\libmxnet.lib pkg_vc14_cpu\\lib
                copy build_vc14_cpu\\Release\\libmxnet.dll pkg_vc14_cpu\\build
                xcopy python pkg_vc14_cpu\\python /E /I /Y
                xcopy include pkg_vc14_cpu\\include /E /I /Y
                xcopy 3rdparty\\dmlc-core\\include pkg_vc14_cpu\\include /E /I /Y
                xcopy 3rdparty\\mshadow\\mshadow pkg_vc14_cpu\\include\\mshadow /E /I /Y
                xcopy 3rdparty\\nnvm\\include pkg_vc14_cpu\\nnvm\\include /E /I /Y
                del /Q *.7z
                7z.exe a vc14_cpu.7z pkg_vc14_cpu\\
                '''
              stash includes: 'vc14_cpu.7z', name: 'vc14_cpu'
            }
          }
        }
      }
    },

    'Build GPU windows':{
      node(NODE_WINDOWS_CPU) {
        timeout(time: max_time, unit: 'MINUTES') {
          ws('workspace/build-gpu') {
            withEnv(['OpenBLAS_HOME=C:\\mxnet\\openblas', 'OpenCV_DIR=C:\\mxnet\\opencv_vc14', 'CUDA_PATH=C:\\CUDA\\v8.0']) {
            utils.init_git_win()
            bat """mkdir build_vc14_gpu
              call "C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\bin\\x86_amd64\\vcvarsx86_amd64.bat"
              cd build_vc14_gpu
              cmake -G \"NMake Makefiles JOM\" -DUSE_CUDA=1 -DUSE_CUDNN=1 -DUSE_NVRTC=1 -DUSE_OPENCV=1 -DUSE_OPENMP=1 -DUSE_PROFILER=1 -DUSE_BLAS=open -DUSE_LAPACK=1 -DUSE_DIST_KVSTORE=0 -DCUDA_ARCH_NAME=Manual -DCUDA_ARCH_BIN=52 -DCUDA_ARCH_PTX=52 -DCMAKE_CXX_FLAGS_RELEASE="/FS /MD /O2 /Ob2 /DNDEBUG" -DCMAKE_BUILD_TYPE=Release -DUSE_MKL_IF_AVAILABLE=0 ${env.WORKSPACE}"""
            bat 'C:\\mxnet\\build_vc14_gpu.bat'
            bat '''rmdir /s/q pkg_vc14_gpu
              mkdir pkg_vc14_gpu\\lib
              mkdir pkg_vc14_gpu\\python
              mkdir pkg_vc14_gpu\\include
              mkdir pkg_vc14_gpu\\build
              copy build_vc14_gpu\\libmxnet.lib pkg_vc14_gpu\\lib
              copy build_vc14_gpu\\libmxnet.dll pkg_vc14_gpu\\build
              xcopy python pkg_vc14_gpu\\python /E /I /Y
              xcopy include pkg_vc14_gpu\\include /E /I /Y
              xcopy 3rdparty\\dmlc-core\\include pkg_vc14_gpu\\include /E /I /Y
              xcopy 3rdparty\\mshadow\\mshadow pkg_vc14_gpu\\include\\mshadow /E /I /Y
              xcopy 3rdparty\\nnvm\\include pkg_vc14_gpu\\nnvm\\include /E /I /Y
              del /Q *.7z
              7z.exe a vc14_gpu.7z pkg_vc14_gpu\\
              '''
            stash includes: 'vc14_gpu.7z', name: 'vc14_gpu'
            }
          }
        }
      }
    },
    'Build GPU MKLDNN windows':{
      node(NODE_WINDOWS_CPU) {
        timeout(time: max_time, unit: 'MINUTES') {
          ws('workspace/build-gpu') {
            withEnv(['OpenBLAS_HOME=C:\\mxnet\\openblas', 'OpenCV_DIR=C:\\mxnet\\opencv_vc14', 'CUDA_PATH=C:\\CUDA\\v8.0','BUILD_NAME=vc14_gpu_mkldnn']) {
            utils.init_git_win()
            bat """mkdir build_%BUILD_NAME%
              call "C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\bin\\x86_amd64\\vcvarsx86_amd64.bat"
              cd build_%BUILD_NAME%
              copy ${env.WORKSPACE}\\3rdparty\\mkldnn\\config_template.vcxproj.user ${env.WORKSPACE}\\config_template.vcxproj.user /y
              cmake -G \"NMake Makefiles JOM\" -DUSE_CUDA=1 -DUSE_CUDNN=1 -DUSE_NVRTC=1 -DUSE_OPENCV=1 -DUSE_OPENMP=1 -DUSE_PROFILER=1 -DUSE_BLAS=open -DUSE_LAPACK=1 -DUSE_DIST_KVSTORE=0 -DCUDA_ARCH_NAME=Manual -DCUDA_ARCH_BIN=52 -DCUDA_ARCH_PTX=52 -DUSE_MKLDNN=1 -DCMAKE_CXX_FLAGS_RELEASE="/FS /MD /O2 /Ob2 /DNDEBUG" -DCMAKE_BUILD_TYPE=Release ${env.WORKSPACE}"""
            bat '''
                call "C:\\Program Files (x86)\\Microsoft Visual Studio 14.0\\VC\\bin\\x86_amd64\\vcvarsx86_amd64.bat"
                cd build_%BUILD_NAME%
                set /a cores=%NUMBER_OF_PROCESSORS% * 2
                jom -j %cores%
                '''
            bat '''rmdir /s/q pkg_%BUILD_NAME%
              mkdir pkg_%BUILD_NAME%\\lib
              mkdir pkg_%BUILD_NAME%\\python
              mkdir pkg_%BUILD_NAME%\\include
              mkdir pkg_%BUILD_NAME%\\build
              copy build_%BUILD_NAME%\\libmxnet.lib pkg_%BUILD_NAME%\\lib
              copy build_%BUILD_NAME%\\libmxnet.dll pkg_%BUILD_NAME%\\build
              copy build_%BUILD_NAME%\\3rdparty\\mkldnn\\src\\mkldnn.dll pkg_%BUILD_NAME%\\build
              copy build_%BUILD_NAME%\\libiomp5md.dll pkg_%BUILD_NAME%\\build
              copy build_%BUILD_NAME%\\mklml.dll pkg_%BUILD_NAME%\\build
              xcopy python pkg_%BUILD_NAME%\\python /E /I /Y
              xcopy include pkg_%BUILD_NAME%\\include /E /I /Y
              xcopy 3rdparty\\dmlc-core\\include pkg_%BUILD_NAME%\\include /E /I /Y
              xcopy 3rdparty\\mshadow\\mshadow pkg_%BUILD_NAME%\\include\\mshadow /E /I /Y
              xcopy 3rdparty\\nnvm\\include pkg_%BUILD_NAME%\\nnvm\\include /E /I /Y
              del /Q *.7z
              7z.exe a %BUILD_NAME%.7z pkg_%BUILD_NAME%\\
              '''
            stash includes: 'vc14_gpu_mkldnn.7z', name: 'vc14_gpu_mkldnn'
            }
          }
        }
      }
    },
    'NVidia Jetson / ARMv8':{
      node(NODE_LINUX_CPU) {
        ws('workspace/build-jetson-armv8') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('jetson', 'build_jetson', false)
          }
        }
      }
    },
    'ARMv7':{
      node(NODE_LINUX_CPU) {
        ws('workspace/build-ARMv7') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('armv7', 'build_armv7', false)
          }
        }
      }
    },
    'ARMv6':{
      node(NODE_LINUX_CPU) {
        ws('workspace/build-ARMv6') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('armv6', 'build_armv6', false)
          }
        }
      }
    },
    'ARMv8':{
      node(NODE_LINUX_CPU) {
        ws('workspace/build-ARMv8') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('armv8', 'build_armv8', false)
          }
        }
      }
    },
    'Android / ARMv8':{
      node(NODE_LINUX_CPU) {
        ws('workspace/android64') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('android_armv8', 'build_android_armv8', false)
          }
        }
      }
    },
    'Android / ARMv7':{
      node(NODE_LINUX_CPU) {
        ws('workspace/androidv7') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.docker_run('android_armv7', 'build_android_armv7', false)
          }
        }
      }
    }

  } // End of stage('Build')

  stage('Tests') {
    parallel 'Python2: CPU': {
      node(NODE_LINUX_CPU) {
        ws('workspace/ut-python2-cpu') {
          try {
            utils.init_git()
            utils.unpack_lib('cpu')
            python2_ut('ubuntu_cpu')
            utils.publish_test_coverage()
          } finally {
            utils.collect_test_results_unix('nosetests_unittest.xml', 'nosetests_python2_cpu_unittest.xml')
            utils.collect_test_results_unix('nosetests_train.xml', 'nosetests_python2_cpu_train.xml')
            utils.collect_test_results_unix('nosetests_quantization.xml', 'nosetests_python2_cpu_quantization.xml')
          }
        }
      }
    },
    'Python3: CPU': {
      node(NODE_LINUX_CPU) {
        ws('workspace/ut-python3-cpu') {
          try {
            utils.init_git()
            utils.unpack_lib('cpu')
            python3_ut('ubuntu_cpu')
            utils.publish_test_coverage()
          } finally {
            utils.collect_test_results_unix('nosetests_unittest.xml', 'nosetests_python3_cpu_unittest.xml')
            utils.collect_test_results_unix('nosetests_quantization.xml', 'nosetests_python3_cpu_quantization.xml')
          }
        }
      }
    },
    'Python2: GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/ut-python2-gpu') {
          try {
            utils.init_git()
            utils.unpack_lib('gpu', mx_lib)
            python2_gpu_ut('ubuntu_gpu')
            utils.publish_test_coverage()
          } finally {
            utils.collect_test_results_unix('nosetests_gpu.xml', 'nosetests_python2_gpu.xml')
          }
        }
      }
    },
    'Python3: GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/ut-python3-gpu') {
          try {
            utils.init_git()
            utils.unpack_lib('gpu', mx_lib)
            python3_gpu_ut('ubuntu_gpu')
            utils.publish_test_coverage()
          } finally {
            utils.collect_test_results_unix('nosetests_gpu.xml', 'nosetests_python3_gpu.xml')
          }
        }
      }
    },
    'Python2: Quantize GPU': {
      node(NODE_LINUX_GPU_P3) {
        ws('workspace/ut-python2-quantize-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            try {
              utils.init_git()
              utils.unpack_lib('gpu', mx_lib)
              utils.docker_run('ubuntu_gpu', 'unittest_ubuntu_python2_quantization_gpu', true)
              utils.publish_test_coverage()
            } finally {
              utils.collect_test_results_unix('nosetests_quantization_gpu.xml', 'nosetests_python2_quantize_gpu.xml')
            }
          }
        }
      }
    },
    'Python3: Quantize GPU': {
      node(NODE_LINUX_GPU_P3) {
        ws('workspace/ut-python3-quantize-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            try {
              utils.init_git()
              utils.unpack_lib('gpu', mx_lib)
              utils.docker_run('ubuntu_gpu', 'unittest_ubuntu_python3_quantization_gpu', true)
              utils.publish_test_coverage()
            } finally {
              utils.collect_test_results_unix('nosetests_quantization_gpu.xml', 'nosetests_python3_quantize_gpu.xml')
            }
          }
        }
      }
    },
    'Python2: MKLDNN-CPU': {
      node(NODE_LINUX_CPU) {
        ws('workspace/ut-python2-mkldnn-cpu') {
          try {
            utils.init_git()
            utils.unpack_lib('mkldnn_cpu', mx_mkldnn_lib)
            python2_ut('ubuntu_cpu')
            utils.publish_test_coverage()
          } finally {
            utils.collect_test_results_unix('nosetests_unittest.xml', 'nosetests_python2_mkldnn_cpu_unittest.xml')
            utils.collect_test_results_unix('nosetests_train.xml', 'nosetests_python2_mkldnn_cpu_train.xml')
            utils.collect_test_results_unix('nosetests_quantization.xml', 'nosetests_python2_mkldnn_cpu_quantization.xml')
          }
        }
      }
    },
    'Python2: MKLDNN-GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/ut-python2-mkldnn-gpu') {
          try {
            utils.init_git()
            utils.unpack_lib('mkldnn_gpu', mx_mkldnn_lib)
            python2_gpu_ut('ubuntu_gpu')
            utils.publish_test_coverage()
          } finally {
            utils.collect_test_results_unix('nosetests_gpu.xml', 'nosetests_python2_mkldnn_gpu.xml')
          }
        }
      }
    },
    'Python3: MKLDNN-CPU': {
      node(NODE_LINUX_CPU) {
        ws('workspace/ut-python3-mkldnn-cpu') {
          try {
            utils.init_git()
            utils.unpack_lib('mkldnn_cpu', mx_mkldnn_lib)
            python3_ut_mkldnn('ubuntu_cpu')
            utils.publish_test_coverage()
          } finally {
            utils.collect_test_results_unix('nosetests_unittest.xml', 'nosetests_python3_mkldnn_cpu_unittest.xml')
            utils.collect_test_results_unix('nosetests_mkl.xml', 'nosetests_python3_mkldnn_cpu_mkl.xml')
          }
        }
      }
    },
    'Python3: MKLDNN-GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/ut-python3-mkldnn-gpu') {
          try {
            utils.init_git()
            utils.unpack_lib('mkldnn_gpu', mx_mkldnn_lib)
            python3_gpu_ut('ubuntu_gpu')
            utils.publish_test_coverage()
          } finally {
            utils.collect_test_results_unix('nosetests_gpu.xml', 'nosetests_python3_mkldnn_gpu.xml')
          }
        }
      }
    },
    'Python3: MKLDNN-GPU-NOCUDNN': {
      node(NODE_LINUX_GPU) {
        ws('workspace/ut-python3-mkldnn-gpu-nocudnn') {
          try {
            utils.init_git()
            utils.unpack_lib('mkldnn_gpu_nocudnn', mx_mkldnn_lib)
            python3_gpu_ut_nocudnn('ubuntu_gpu')
            utils.publish_test_coverage()
          } finally {
            utils.collect_test_results_unix('nosetests_gpu.xml', 'nosetests_python3_mkldnn_gpu_nocudnn.xml')
          }
        }
      }
    },
    'Python3: CentOS 7 CPU': {
      node(NODE_LINUX_CPU) {
        ws('workspace/build-centos7-cpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            try {
              utils.init_git()
              utils.unpack_lib('centos7_cpu')
              utils.docker_run('centos7_cpu', 'unittest_centos7_cpu', false)
              utils.publish_test_coverage()
            } finally {
              utils.collect_test_results_unix('nosetests_unittest.xml', 'nosetests_python3_centos7_cpu_unittest.xml')
              utils.collect_test_results_unix('nosetests_train.xml', 'nosetests_python3_centos7_cpu_train.xml')
            }
          }
        }
      }
    },
    'Python3: CentOS 7 GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/build-centos7-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            try {
              utils.init_git()
              utils.unpack_lib('centos7_gpu')
              utils.docker_run('centos7_gpu', 'unittest_centos7_gpu', true)
              utils.publish_test_coverage()
            } finally {
              utils.collect_test_results_unix('nosetests_gpu.xml', 'nosetests_python3_centos7_gpu.xml')
            }
          }
        }
      }
    },
    'Scala: CPU': {
      node(NODE_LINUX_CPU) {
        ws('workspace/ut-scala-cpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('cpu', mx_dist_lib)
            utils.docker_run('ubuntu_cpu', 'unittest_ubuntu_cpu_scala', false)
            utils.publish_test_coverage()
          }
        }
      }
    },
    'Clojure: CPU': {
      node(NODE_LINUX_CPU) {
        ws('workspace/ut-clojure-cpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('cpu', mx_dist_lib)
            utils.docker_run('ubuntu_cpu', 'unittest_ubuntu_cpu_clojure', false)
            utils.publish_test_coverage()
          }
        }
      }
    },
    'Perl: CPU': {
      node(NODE_LINUX_CPU) {
        ws('workspace/ut-perl-cpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('cpu')
            utils.docker_run('ubuntu_cpu', 'unittest_ubuntu_cpugpu_perl', false)
            utils.publish_test_coverage()
          }
        }
      }
    },
    'Perl: GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/ut-perl-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('gpu')
            utils.docker_run('ubuntu_gpu', 'unittest_ubuntu_cpugpu_perl', true)
            utils.publish_test_coverage()
          }
        }
      }
    },
    'Cpp: GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/ut-cpp-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('cmake_gpu', mx_cmake_lib)
            utils.docker_run('ubuntu_gpu', 'unittest_ubuntu_gpu_cpp', true)
            utils.publish_test_coverage()
          }
        }
      }
    },
    'Cpp: MKLDNN+GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/ut-cpp-mkldnn-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('cmake_mkldnn_gpu', mx_cmake_mkldnn_lib)
            utils.docker_run('ubuntu_gpu', 'unittest_ubuntu_gpu_cpp', true)
            utils.publish_test_coverage()
          }
        }
      }
    },
    'R: CPU': {
      node(NODE_LINUX_CPU) {
        ws('workspace/ut-r-cpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('cpu')
            utils.docker_run('ubuntu_cpu', 'unittest_ubuntu_cpu_R', false)
            utils.publish_test_coverage()
          }
        }
      }
    },
    'R: GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/ut-r-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('gpu')
            utils.docker_run('ubuntu_gpu', 'unittest_ubuntu_gpu_R', true)
            utils.publish_test_coverage()
          }
        }
      }
    },

    'Python 2: CPU Win':{
      node(NODE_WINDOWS_CPU) {
        timeout(time: max_time, unit: 'MINUTES') {
          ws('workspace/ut-python-cpu') {
            try {
              utils.init_git_win()
              unstash 'vc14_cpu'
              bat '''rmdir /s/q pkg_vc14_cpu
                7z x -y vc14_cpu.7z'''
              bat """xcopy C:\\mxnet\\data data /E /I /Y
                xcopy C:\\mxnet\\model model /E /I /Y
                call activate py2
                pip install mock
                set PYTHONPATH=${env.WORKSPACE}\\pkg_vc14_cpu\\python
                del /S /Q ${env.WORKSPACE}\\pkg_vc14_cpu\\python\\*.pyc
                C:\\mxnet\\test_cpu.bat"""
            } finally {
              utils.collect_test_results_windows('nosetests_unittest.xml', 'nosetests_unittest_windows_python2_cpu.xml')
            }
          }
        }
      }
    },
    'Python 3: CPU Win': {
      node(NODE_WINDOWS_CPU) {
        timeout(time: max_time, unit: 'MINUTES') {
          ws('workspace/ut-python-cpu') {
            try {
              utils.init_git_win()
              unstash 'vc14_cpu'
              bat '''rmdir /s/q pkg_vc14_cpu
                7z x -y vc14_cpu.7z'''
              bat """xcopy C:\\mxnet\\data data /E /I /Y
                xcopy C:\\mxnet\\model model /E /I /Y
                call activate py3
                set PYTHONPATH=${env.WORKSPACE}\\pkg_vc14_cpu\\python
                del /S /Q ${env.WORKSPACE}\\pkg_vc14_cpu\\python\\*.pyc
                C:\\mxnet\\test_cpu.bat"""
            } finally {
              utils.collect_test_results_windows('nosetests_unittest.xml', 'nosetests_unittest_windows_python3_cpu.xml')
            }
          }
        }
      }
    },
    'Python 2: GPU Win':{
      node(NODE_WINDOWS_GPU) {
        timeout(time: max_time, unit: 'MINUTES') {
          ws('workspace/ut-python-gpu') {
            try {
              utils.init_git_win()
              unstash 'vc14_gpu'
              bat '''rmdir /s/q pkg_vc14_gpu
                7z x -y vc14_gpu.7z'''
              bat """xcopy C:\\mxnet\\data data /E /I /Y
                xcopy C:\\mxnet\\model model /E /I /Y
                call activate py2
                pip install mock
                set PYTHONPATH=${env.WORKSPACE}\\pkg_vc14_gpu\\python
                del /S /Q ${env.WORKSPACE}\\pkg_vc14_gpu\\python\\*.pyc
                C:\\mxnet\\test_gpu.bat"""
            } finally {
              utils.collect_test_results_windows('nosetests_gpu_forward.xml', 'nosetests_gpu_forward_windows_python2_gpu.xml')
              utils.collect_test_results_windows('nosetests_gpu_operator.xml', 'nosetests_gpu_operator_windows_python2_gpu.xml')
            }
          }
        }
      }
    },
    'Python 3: GPU Win':{
      node(NODE_WINDOWS_GPU) {
        timeout(time: max_time, unit: 'MINUTES') {
          ws('workspace/ut-python-gpu') {
            try {
              utils.init_git_win()
              unstash 'vc14_gpu'
              bat '''rmdir /s/q pkg_vc14_gpu
                7z x -y vc14_gpu.7z'''
              bat """xcopy C:\\mxnet\\data data /E /I /Y
                xcopy C:\\mxnet\\model model /E /I /Y
                call activate py3
                set PYTHONPATH=${env.WORKSPACE}\\pkg_vc14_gpu\\python
                del /S /Q ${env.WORKSPACE}\\pkg_vc14_gpu\\python\\*.pyc
                C:\\mxnet\\test_gpu.bat"""
            } finally {
              utils.collect_test_results_windows('nosetests_gpu_forward.xml', 'nosetests_gpu_forward_windows_python3_gpu.xml')
              utils.collect_test_results_windows('nosetests_gpu_operator.xml', 'nosetests_gpu_operator_windows_python3_gpu.xml')
            }
          }
        }
      }
    },
    'Python 3: MKLDNN-GPU Win':{
      node(NODE_WINDOWS_GPU) {
        timeout(time: max_time, unit: 'MINUTES') {
          ws('workspace/ut-python-gpu') {
            try {
              utils.init_git_win()
              unstash 'vc14_gpu_mkldnn'
              bat '''rmdir /s/q pkg_vc14_gpu_mkldnn
                7z x -y vc14_gpu_mkldnn.7z'''
              bat """xcopy C:\\mxnet\\data data /E /I /Y
                xcopy C:\\mxnet\\model model /E /I /Y
                call activate py3
                set PYTHONPATH=${env.WORKSPACE}\\pkg_vc14_gpu_mkldnn\\python
                del /S /Q ${env.WORKSPACE}\\pkg_vc14_gpu_mkldnn\\python\\*.pyc
                C:\\mxnet\\test_gpu.bat"""
            } finally {
              utils.collect_test_results_windows('nosetests_gpu_forward.xml', 'nosetests_gpu_forward_windows_python3_gpu_mkldnn.xml')
              utils.collect_test_results_windows('nosetests_gpu_operator.xml', 'nosetests_gpu_operator_windows_python3_gpu_mkldnn.xml')
            }
          }
        }
      }
    },
    'Onnx CPU': {
      node(NODE_LINUX_CPU) {
        ws('workspace/it-onnx-cpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('cpu')
            utils.docker_run('ubuntu_cpu', 'integrationtest_ubuntu_cpu_onnx', false)
            utils.publish_test_coverage()
          }
        }
      }
    },
    'Python GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/it-python-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('gpu')
            utils.docker_run('ubuntu_gpu', 'integrationtest_ubuntu_gpu_python', true)
            utils.publish_test_coverage()
          }
        }
      }
    },
    // Disabled due to: https://github.com/apache/incubator-mxnet/issues/11407
    // 'Caffe GPU': {
    //   node(NODE_LINUX_GPU) {
    //     ws('workspace/it-caffe') {
    //       timeout(time: max_time, unit: 'MINUTES') {
    //         utils.init_git()
    //         utils.unpack_lib('gpu')
    //         utils.docker_run('ubuntu_gpu', 'integrationtest_ubuntu_gpu_caffe', true)
    //         utils.publish_test_coverage()
    //       }
    //     }
    //   }
    // },
    'cpp-package GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/it-cpp-package') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('gpu')
            unstash 'cpp_lenet'
            unstash 'cpp_alexnet'
            unstash 'cpp_googlenet'
            unstash 'cpp_lenet_with_mxdataiter'
            unstash 'cpp_resnet'
            unstash 'cpp_mlp'
            unstash 'cpp_mlp_cpu'
            unstash 'cpp_mlp_gpu'
            unstash 'cpp_test_score'
            unstash 'cpp_test_optimizer'
            utils.docker_run('ubuntu_gpu', 'integrationtest_ubuntu_gpu_cpp_package', true)
            utils.publish_test_coverage()
          }
        }
      }
    },
    'dist-kvstore tests GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/it-dist-kvstore') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('gpu')
            utils.docker_run('ubuntu_gpu', 'integrationtest_ubuntu_gpu_dist_kvstore', true)
            utils.publish_test_coverage()
          }
        }
      }
    },
    /*  Disabled due to master build failure:
     *  http://jenkins.mxnet-ci.amazon-ml.com/blue/organizations/jenkins/incubator-mxnet/detail/master/1221/pipeline/
     *  https://github.com/apache/incubator-mxnet/issues/11801

    'dist-kvstore tests CPU': {
      node(NODE_LINUX_CPU) {
        ws('workspace/it-dist-kvstore') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('cpu')
            utils.docker_run('ubuntu_cpu', 'integrationtest_ubuntu_cpu_dist_kvstore', false)
            utils.publish_test_coverage()
          }
        }
      }
    }, */
    'Scala: GPU': {
      node(NODE_LINUX_GPU) {
        ws('workspace/ut-scala-gpu') {
          timeout(time: max_time, unit: 'MINUTES') {
            utils.init_git()
            utils.unpack_lib('gpu', mx_dist_lib)
            utils.docker_run('ubuntu_gpu', 'integrationtest_ubuntu_gpu_scala', true)
            utils.publish_test_coverage()
          }
        }
      }
    }
  }

  stage('Deploy') {
    node(NODE_LINUX_CPU) {
      ws('workspace/docs') {
        timeout(time: max_time, unit: 'MINUTES') {
          utils.init_git()
          utils.docker_run('ubuntu_cpu', 'deploy_docs', false)
          sh "tests/ci_build/deploy/ci_deploy_doc.sh ${env.BRANCH_NAME} ${env.BUILD_NUMBER}"
        }
      }
    }
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

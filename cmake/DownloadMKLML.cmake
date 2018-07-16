# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

# This script will download mkldnn with dependencies such as intel omp and mklml

message(STATUS "downloading mkldnn...")

if(MSVC)
  set(MKL_NAME "mklml_win_2018.0.3.20180406")
  file(DOWNLOAD "https://github.com/intel/mkl-dnn/releases/download/v0.14/${MKL_NAME}.zip" "${CMAKE_CURRENT_BINARY_DIR}/mklml/${MKL_NAME}.zip" EXPECTED_MD5 "8DD73E7D3F19F004551809824C4E8970" SHOW_PROGRESS)
  file(DOWNLOAD "https://github.com/apache/incubator-mxnet/releases/download/utils/7z.exe" "${CMAKE_CURRENT_BINARY_DIR}/mklml/7z2.exe" EXPECTED_MD5 "E1CF766CF358F368EC97662D06EA5A4C" SHOW_PROGRESS)

  execute_process(COMMAND "${CMAKE_CURRENT_BINARY_DIR}/mklml/7z2.exe" "-o${CMAKE_CURRENT_BINARY_DIR}/mklml/" "-y")
  execute_process(COMMAND "${CMAKE_CURRENT_BINARY_DIR}/mklml/7z.exe" "x" "${CMAKE_CURRENT_BINARY_DIR}/mklml/${MKL_NAME}.zip" "-o${CMAKE_CURRENT_BINARY_DIR}/mklml/" "-y")
  set(MKLDNN_ROOT "${CMAKE_CURRENT_BINARY_DIR}/mklml/${MKL_NAME}")

  file(COPY ${MKLDNN_ROOT}/lib/libiomp5md.dll DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  file(COPY ${MKLDNN_ROOT}/lib/mklml.dll DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  file(COPY ${CMAKE_SOURCE_DIR}/3rdparty/mkldnn/config_template.vcxproj.user DESTINATION ${CMAKE_SOURCE_DIR})
elseif(UNIX)
  set(MKL_NAME "mklml_lnx_2018.0.3.20180406")
  file(DOWNLOAD "https://github.com/intel/mkl-dnn/releases/download/v0.14/${MKL_NAME}.tgz" "${CMAKE_CURRENT_BINARY_DIR}/mklml/${MKL_NAME}.tgz" EXPECTED_MD5 "DAF7EFC3C1C0036B447213004467A8AE" SHOW_PROGRESS)
  execute_process(COMMAND "tar" "-xzf" "${CMAKE_CURRENT_BINARY_DIR}/mklml/${MKL_NAME}.tgz" "-C" "${CMAKE_CURRENT_BINARY_DIR}/mklml/")
  set(MKLDNN_ROOT "${CMAKE_CURRENT_BINARY_DIR}/mklml/${MKL_NAME}")

  file(COPY ${MKLDNN_ROOT}/lib/libiomp5.so DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  file(COPY ${MKLDNN_ROOT}/lib/libmklml_gnu.so DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  file(COPY ${MKLDNN_ROOT}/lib/libmklml_intel.so DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
elseif(APPLE)
  set(MKL_NAME "mklml_mac_2018.0.3.20180406.tgz")
  file(DOWNLOAD "https://github.com/intel/mkl-dnn/releases/download/v0.14/${MKL_NAME}.tgz" "${CMAKE_CURRENT_BINARY_DIR}/mklml/${MKL_NAME}.tgz" EXPECTED_MD5 "23a6f7fd04fb1fa6de0d52a2ec5a2a14" SHOW_PROGRESS)
  execute_process(COMMAND "tar" "-xzf" "${CMAKE_CURRENT_BINARY_DIR}/mklml/${MKL_NAME}.tgz" "-C" "${CMAKE_CURRENT_BINARY_DIR}/mklml/")
  set(MKLDNN_ROOT "${CMAKE_CURRENT_BINARY_DIR}/mklml/${MKL_NAME}")

  file(COPY ${MKLDNN_ROOT}/lib/libiomp5.dylib DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
  file(COPY ${MKLDNN_ROOT}/lib/libmklml.dylib DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
else()
  message(FATAL_ERROR "Wrong platform")
endif()

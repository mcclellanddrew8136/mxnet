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

set(BLAS "Open" CACHE STRING "Selected BLAS library")
set_property(CACHE BLAS PROPERTY STRINGS "Atlas;Open;MKL")

if(DEFINED USE_BLAS)
  set(BLAS "${USE_BLAS}")
else()
  if(USE_MKL_IF_AVAILABLE)
    if(NOT MKL_FOUND)
      find_package(MKL)
    endif()
    if(MKL_FOUND)
      set(BLAS "MKL")
    endif()
  endif()
endif()

if(BLAS STREQUAL "Atlas" OR BLAS STREQUAL "atlas")
  find_package(Atlas REQUIRED)
  include_directories(SYSTEM ${Atlas_INCLUDE_DIR})
  list(APPEND mshadow_LINKER_LIBS ${Atlas_LIBRARIES})
  add_definitions(-DMSHADOW_USE_CBLAS=1)
  add_definitions(-DMSHADOW_USE_MKL=0)
  add_definitions(-DMXNET_USE_BLAS_ATLAS=1)
elseif(BLAS STREQUAL "Open" OR BLAS STREQUAL "open")
  find_package(OpenBLAS REQUIRED)
  include_directories(SYSTEM ${OpenBLAS_INCLUDE_DIR})
  list(APPEND mshadow_LINKER_LIBS ${OpenBLAS_LIB})
  add_definitions(-DMSHADOW_USE_CBLAS=1)
  add_definitions(-DMSHADOW_USE_MKL=0)
  add_definitions(-DMXNET_USE_BLAS_OPEN=1)
  if(NOT MSVC)
    # check if we need to link to omp
    execute_process(COMMAND ${CMAKE_NM} -g ${OpenBLAS_LIB}
                    COMMAND grep omp_get_num_threads
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE OPENBLAS_USES_OMP_OUT
                    RESULT_VARIABLE OPENBLAS_USES_OMP_RET)
    if(NOT OPENBLAS_USES_OMP_OUT STREQUAL "" AND NOT OPENBLAS_USES_OMP_RET AND NOT USE_OPENMP)
      message("Openblas uses OMP, automatically linking to it")
      find_package(OpenMP REQUIRED)
      message("OpenMP_CXX_LIBRARIES is ${OpenMP_CXX_LIBRARIES}")
      list(APPEND mshadow_LINKER_LIBS "${OpenMP_CXX_LIBRARIES}")
    endif()
    # check if we need to link to gfortran
    execute_process(COMMAND ${CMAKE_NM} -g ${OpenBLAS_LIB}
                    COMMAND grep gfortran
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE OPENBLAS_USES_GFORTRAN_OUT
                    RESULT_VARIABLE OPENBLAS_USES_GFORTRAN_RET)
    if(NOT OPENBLAS_USES_GFORTRAN_OUT STREQUAL "" AND NOT OPENBLAS_USES_GFORTRAN_RET)
      message("Openblas uses GFortran, automatically linking to it")
      file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/temp/CMakeLists.txt"
      "cmake_minimum_required(VERSION ${CMAKE_VERSION})
project(CheckFortran Fortran)
set(CMAKE_Fortran_COMPILER gfortran)
file(WRITE \"${CMAKE_CURRENT_BINARY_DIR}/temp/FortranDir.cmake\"
\"
set(FORTRAN_DIR \\\"\$\{CMAKE_Fortran_IMPLICIT_LINK_DIRECTORIES\}\\\")
\")
")
      execute_process(
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/temp/
        COMMAND ${CMAKE_COMMAND} .
      )
      set(FORTRAN_DIR "")
      include(build/temp/FortranDir.cmake)
      find_library(FORTRAN_LIB NAMES gfortran HINTS ${FORTRAN_DIR})
      message("FORTRAN_DIR is ${FORTRAN_DIR}")
      message("FORTRAN_LIB is ${FORTRAN_LIB}")
      list(APPEND mshadow_LINKER_LIBS ${FORTRAN_LIB})
      file(REMOVE_RECURSE "${CMAKE_CURRENT_BINARY_DIR}/temp/")
    endif()
    # check the lapack flavor of openblas
    include(CheckSymbolExists)
    check_symbol_exists(OPENBLAS_USE64BITINT "${OpenBLAS_INCLUDE_DIR}/openblas_config.h" OPENBLAS_ILP64)
    if(OPENBLAS_ILP64)
      message("Using ILP64 OpenBLAS")
      if(NOT USE_INT64_TENSOR_SIZE)
        message(FATAL_ERROR "Must set USE_INT64_TENSOR_SIZE=1 when using ILP64 OpenBLAS")
      endif()
    else()
      message("Using LP64 OpenBLAS")
    endif()
    if(EXISTS "${OpenBLAS_INCLUDE_DIR}/lapacke.h")
      message("Detected lapacke.h, automatically using the LAPACKE interface")
      add_definitions(-DMXNET_USE_LAPACKE_INTERFACE=1)
      set(USE_LAPACKE_INTERFACE 1)
      if(OPENBLAS_ILP64)
        message("Detected ILP64 LAPACKE")
        add_definitions(-DMXNET_USE_ILP64_LAPACKE=1)
      endif()
    else()
      execute_process(COMMAND ${CMAKE_NM} -g ${OpenBLAS_LIB}
                      COMMAND grep sgetri_
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                      OUTPUT_VARIABLE OPENBLAS_CONTAINS_C_LAPACK_OUT
                      RESULT_VARIABLE OPENBLAS_CONTAINS_C_LAPACK_RET)
      if(OPENBLAS_CONTAINS_C_LAPACK_OUT STREQUAL ""
         AND NOT OPENBLAS_CONTAINS_C_LAPACK_RET
         AND USE_LAPACK)
        list(APPEND mshadow_LINKER_LIBS lapack)
      endif()
    endif()
  endif()
elseif(BLAS STREQUAL "MKL" OR BLAS STREQUAL "mkl")
  if (USE_INT64_TENSOR_SIZE)
    set(MKL_USE_ILP64 ON CACHE BOOL "enable using ILP64 in MKL" FORCE)
  else()
    if(MKL_USE_ILP64)
      message(FATAL_ERROR "MKL_USE_ILP64 cannot be set without USE_INT64_TENSOR_SIZE; "
              "Please set USE_INT64_TENSOR_SIZE instead of MKL_USE_ILP64.")
    endif()
  endif()
  find_package(MKL REQUIRED)
  include_directories(SYSTEM ${MKL_INCLUDE_DIR})
  list(APPEND mshadow_LINKER_LIBS ${MKL_LIBRARIES})
  add_definitions(-DMSHADOW_USE_CBLAS=0)
  add_definitions(-DMSHADOW_USE_MKL=1)
  add_definitions(-DMXNET_USE_BLAS_MKL=1)
elseif(BLAS STREQUAL "apple")
  find_package(Accelerate REQUIRED)
  include_directories(SYSTEM ${Accelerate_INCLUDE_DIR})
  list(APPEND mshadow_LINKER_LIBS ${Accelerate_LIBRARIES})
  add_definitions(-DMSHADOW_USE_MKL=0)
  add_definitions(-DMSHADOW_USE_CBLAS=1)
  add_definitions(-DMXNET_USE_BLAS_APPLE=1)
endif()

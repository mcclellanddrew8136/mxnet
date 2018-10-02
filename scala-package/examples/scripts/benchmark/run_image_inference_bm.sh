#!/bin/bash

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

set -e

echo $OSTYPE
platform=linux-x86_64-cpu

if [[ "$OSTYPE" == "darwin"* ]]; then
        platform=osx-x86_64-cpu
fi


MXNET_ROOT=$(cd "$(dirname $0)/../../../.."; pwd)
CLASS_PATH=$MXNET_ROOT/scala-package/assembly/$platform/target/*:$MXNET_ROOT/scala-package/examples/target/*:$MXNET_ROOT/scala-package/examples/target/classes/lib/*:$MXNET_ROOT/scala-package/infer/target/*

MODEL_NAME=$1

RUNS=$2

BATCHSIZE=$3

# model dir
MODEL_PATH_PREFIX=$4
# input image
INPUT_IMG=$5
# which input image dir
INPUT_DIR=$6

java -Xmx8G -Dmxnet.traceLeakedObjects=true -cp $CLASS_PATH \
	org.apache.mxnetexamples.benchmark.ScalaInferenceBenchmark \
	--example $MODEL_NAME \
	--count $RUNS \
	--batchSize $BATCHSIZE \
	--model-path-prefix $MODEL_PATH_PREFIX \
	--input-image $INPUT_IMG \
	--input-dir $INPUT_DIR \


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

from mxnet import np, npx, use_np
from common import setup_module, teardown_module, with_environment

@use_np
@with_environment('MXNET_ENGINE_TYPE', 'NaiveEngine')
def test_18927():
    """test for no error when dealing with zero-size array in bipartite matching"""
    arr = np.random.rand(0,2)
    npx.bipartite_matching(arr, threshold=0.1)

@use_np
@with_environment('MXNET_ENGINE_TYPE', 'NaiveEngine')
def test_18933_batch_0():
    arr = np.random.rand(0,1,1) # batch = 0
    gamma = np.random.rand(1)
    beta = np.random.rand(1)
    npx.instance_norm(arr, gamma, beta)

@use_np
@with_environment('MXNET_ENGINE_TYPE', 'NaiveEngine')
def test_18933_channel_0():
    arr = np.random.rand(1,0,1) # channel = 0
    gamma = np.random.rand(1)
    beta = np.random.rand(1)
    npx.instance_norm(arr, gamma, beta)

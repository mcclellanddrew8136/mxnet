#!/usr/bin/env python3

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

# coding: utf-8
# pylint: disable=arguments-differ

# This test checks dynamic loading of custom library into MXNet
# and checks end to end compute of a simple 2D gemm custom op

import mxnet as mx
import os

path = os.path.abspath('gemm_lib.so')
mx.library.load(path)

a = mx.nd.array([[1,2,3],[4,5,6]])
b = mx.nd.array([[7],[8],[9]])

print("--------start ndarray compute---------")
print(mx.nd.my_gemm(a,b))
print(mx.nd.state_gemm(a,b))

print("--------start symbolic compute--------")
s = mx.sym.Variable('s')
t = mx.sym.Variable('t')
c = mx.sym.my_gemm(s,t)
d = mx.sym.state_gemm(s,t)

in_grad = [mx.nd.empty((2,3)),mx.nd.empty((3,1))]
in_grad2 = [mx.nd.empty((2,3)),mx.nd.empty((3,1))]

exe = c.bind(ctx=mx.cpu(),args={'s':a,'t':b},args_grad=in_grad)
exe2 = d.bind(ctx=mx.cpu(),args={'s':a,'t':b},args_grad=in_grad2)

out = exe.forward()
print(out)

out2 = exe2.forward()
out2 = exe2.forward()
print(out2)

# baseline forward
e = mx.sym.linalg.gemm2(s,t)
in_grad3 = [mx.nd.empty((2,3)),mx.nd.empty((3,1))]
exe3 = e.bind(ctx=mx.cpu(),args={'s':a,'t':b},args_grad=in_grad3)
out3 = exe3.forward()
print(out3)

print("--------start backward compute--------")
out_grad = mx.nd.ones((2,1))
exe.backward([out_grad])
print(in_grad)
exe2.backward([out_grad])
print(in_grad2)
exe3.backward([out_grad])
print(in_grad3)

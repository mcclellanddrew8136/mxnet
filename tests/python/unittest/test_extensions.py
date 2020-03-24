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

# This test checks if dynamic loading of library into MXNet is successful

import os
import platform
import unittest
import mxnet as mx
import numpy as np
from mxnet import nd
from mxnet.gluon import nn
from mxnet.base import MXNetError
from mxnet.test_utils import download, is_cd_run, assert_almost_equal, default_context

base_path = os.path.join(os.path.dirname(__file__), "../../..")
def check_platform():
    return platform.machine() not in ['x86_64', 'AMD64']

@unittest.skipIf(check_platform(), "not all machine types supported")
@unittest.skipIf(is_cd_run(), "continuous delivery run - ignoring test")
def test_custom_op():
    # possible places to find library file
    if (os.name=='posix'):
        lib = 'libcustomop_lib.so'
        if os.path.exists(lib):
            fname = lib
        elif os.path.exists(os.path.join(base_path,'build/'+lib)):
            fname = os.path.join(base_path,'build/'+lib)
        else:
            raise MXNetError("library %s not found " % lib)
    elif (os.name=='nt'):
        lib = 'libcustomop_lib.dll'
        if os.path.exists('windows_package\\lib\\'+lib):
            fname = 'windows_package\\lib\\'+lib
        else:
            raise MXNetError("library %s not found " % lib)

    fname = os.path.abspath(fname)
    # load the library containing gemm custom operators
    mx.library.load(fname)

    # test symbol 2D gemm custom operators
    s = mx.sym.Variable('s')
    t = mx.sym.Variable('t')
    c = mx.sym.my_gemm(s,t)
    d = mx.sym.state_gemm(s,t)
    # baseline gemm from MXNet
    base = mx.sym.linalg.gemm2(s,t)

    # get some random input matrices
    dim_n, dim_k, dim_m = tuple(np.random.randint(1, 5, size=3))
    mat1 = mx.nd.random.uniform(-10, 10, shape=(dim_n, dim_k), ctx=mx.cpu())
    mat2 = mx.nd.random.uniform(-10, 10, shape=(dim_k, dim_m), ctx=mx.cpu())

    # intermediate ndarrays to be populated by gradient compute
    in_grad1 = [mx.nd.empty((dim_n,dim_k),ctx=mx.cpu()),mx.nd.empty((dim_k,dim_m),ctx=mx.cpu())]
    in_grad2 = [mx.nd.empty((dim_n,dim_k),ctx=mx.cpu()),mx.nd.empty((dim_k,dim_m),ctx=mx.cpu())]
    in_grad_base = [mx.nd.empty((dim_n,dim_k),ctx=mx.cpu()),mx.nd.empty((dim_k,dim_m),ctx=mx.cpu())]

    exe1 = c.bind(ctx=mx.cpu(),args={'s':mat1,'t':mat2},args_grad=in_grad1)
    exe2 = d.bind(ctx=mx.cpu(),args={'s':mat1,'t':mat2},args_grad=in_grad2)
    exe_base = base.bind(ctx=mx.cpu(),args={'s':mat1,'t':mat2},args_grad=in_grad_base)

    out1 = exe1.forward()
    out2 = exe2.forward()
    # test stateful operator by calling it multiple times
    out2 = exe2.forward()
    out_base = exe_base.forward()

    # check that forward compute matches one executed by MXNet
    assert_almost_equal(out_base[0].asnumpy(), out1[0].asnumpy(), rtol=1e-3, atol=1e-3)
    assert_almost_equal(out_base[0].asnumpy(), out2[0].asnumpy(), rtol=1e-3, atol=1e-3)

    # random output grad ndarray for gradient update
    out_grad = mx.nd.ones((dim_n, dim_m), ctx=mx.cpu())
    exe1.backward([out_grad])
    exe2.backward([out_grad])
    exe_base.backward([out_grad])

    # check that gradient compute matches one executed by MXNet
    assert_almost_equal(in_grad_base[0].asnumpy(), in_grad1[0].asnumpy(), rtol=1e-3, atol=1e-3)
    assert_almost_equal(in_grad_base[0].asnumpy(), in_grad2[0].asnumpy(), rtol=1e-3, atol=1e-3)

@unittest.skipIf(check_platform(), "not all machine types supported")
@unittest.skipIf(is_cd_run(), "continuous delivery run - ignoring test")
def test_subgraph():
    # possible places to find library file
    if (os.name=='posix'):
        lib = 'libsubgraph_lib.so'
        if os.path.exists(lib):
            # plain make build, when run in the CI
            fname = lib
        elif os.path.exists(os.path.join(base_path, 'build/'+lib)):
            # plain cmake build when run in the CI
            fname = os.path.join(base_path, 'build/'+lib)
        else:
            raise MXNetError("library %s not found " % lib)
    elif (os.name=='nt'):
        lib = 'libsubgraph_lib.dll'
        if os.path.exists('windows_package\\lib\\'+lib):
            # plain make build, when run in the CI
            fname = 'windows_package\\lib\\'+lib
        else:
            # plain cmake build when run in the CI
            raise MXNetError("library %s not found " % lib)

    fname = os.path.abspath(fname)
    mx.library.load(fname)

    # test simple graph with add, exp and log operators, library supports exp/log
    a = mx.sym.var('a')
    b = mx.sym.var('b')
    c = a + b
    d = mx.sym.exp(c)
    sym = mx.sym.log(d)

    args = {'a':mx.nd.ones((3,2),ctx=mx.cpu()), 'b':mx.nd.ones((3,2),ctx=mx.cpu())}
    arg_array = [mx.nd.ones((3,2),dtype='float32',ctx=mx.cpu()),
                 mx.nd.ones((3,2),dtype='float32',ctx=mx.cpu())]

    # baseline - regular execution in MXNet
    exe = sym.bind(ctx=mx.cpu(), args=args)
    out = exe.forward()

    # without propogating shapes/types, passing a custom option to subgraph prop "myOpt"
    # should not create subgraph since subgraph prop requires type info
    mysym1 = sym.optimize_for("myProp", myOpt='yello')
    exe1 = mysym1.bind(ctx=mx.cpu(), args=args)
    out1 = exe1.forward()
    # check that result matches one executed by MXNet
    assert_almost_equal(out[0].asnumpy(), out1[0].asnumpy(), rtol=1e-3, atol=1e-3)

    # with propogating shapes/types, rejecting subgraph
    # this tests creating the subgraph and having the subgraph prop reject it
    mysym2 = sym.optimize_for("myProp", arg_array, reject=True)
    exe2 = mysym2.bind(ctx=mx.cpu(), args=args)
    out2 = exe2.forward()
    # check that result matches one executed by MXNet
    assert_almost_equal(out[0].asnumpy(), out2[0].asnumpy(), rtol=1e-3, atol=1e-3)

    # with propogating shapes/types
    mysym3 = sym.optimize_for("myProp",arg_array)
    exe3 = mysym3.bind(ctx=mx.cpu(), args=args)
    out3 = exe3.forward()
    # check that result matches one executed by MXNet
    assert_almost_equal(out[0].asnumpy(), out3[0].asnumpy(), rtol=1e-3, atol=1e-3)

    # Gluon Hybridize partitioning with shapes/types
    sym_block = nn.SymbolBlock(sym, [a,b])
    sym_block.initialize()
    sym_block.hybridize(backend='myProp')
    out4 = sym_block(mx.nd.ones((3,2)),mx.nd.ones((3,2)))
    # check that result matches one executed by MXNet
    assert_almost_equal(out[0].asnumpy(), out4[0].asnumpy(), rtol=1e-3, atol=1e-3)

    # Gluon Hybridize partitioning with shapes/types
    sym_block2 = nn.SymbolBlock(sym, [a,b])
    sym_block2.initialize()
    a_data = mx.nd.ones((3,2))
    b_data = mx.nd.ones((3,2))
    sym_block2.optimize_for(a_data, b_data, backend='myProp')
    sym_block2.export('optimized')
    sym_block3 = nn.SymbolBlock.imports('optimized-symbol.json',['a','b'],
                                        'optimized-0000.params')

    out5 = sym_block3(a_data, b_data)
    # check that result matches one executed by MXNet
    assert_almost_equal(out[0].asnumpy(), out5[0].asnumpy(), rtol=1e-3, atol=1e-3)

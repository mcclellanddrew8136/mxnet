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

"""
MKL-DNN related test cases
"""

import mxnet as mx
import numpy as np
import sys,os,logging
from mxnet import gluon
from mxnet.gluon import nn
curr_path = os.path.dirname(os.path.abspath(os.path.expanduser(__file__)))
sys.path.append(os.path.join(curr_path, '../unittest/'))
from common import setup_module, with_seed
from nose.tools import raises
from mxnet.test_utils import assert_almost_equal


def test_mkldnn_install():
    """
    This test will verify that MXNet is built/installed correctly when
    compiled with Intel MKL-DNN library. The method will try to import
    the mxnet module and see if the mkldnn library is mapped to this
    process's address space.
    """
    logging.basicConfig(level=logging.INFO)

    if not sys.platform.startswith('linux'):
        logging.info("Bypass mkldnn install test for non-Linux OS")
        return

    try:
        #pylint: disable=unused-variable
        import mxnet as mx
    except (ImportError, OSError) as e:
        assert 0, "Import mxnet error: %s. Please double check your build/" \
            "install steps or environment variable settings" % str(e)

    pid = os.getpid()
    rc = os.system("cat /proc/" + str(pid) +
                   "/maps | grep libmkldnn > /dev/null")

    if rc == 0:
        logging.info("MXNet is built/installed correctly with MKL-DNN")
    else:
        assert 0, "MXNet is built/installed incorrectly with MKL-DNN, please " \
            "double check your build/install steps or environment " \
            "variable settings"


def test_mkldnn_model():
    """
    This test will run a sample model for couple of iterations.
    """

    import mxnet as mx
    model = os.path.join(os.path.dirname(os.path.realpath(__file__)), "data",
                         "test_mkldnn_test_mkldnn_model_model1.json")
    shape = (32, 3, 300, 300)
    ctx = mx.cpu()

    sym = mx.sym.load(model)
    args = sym.list_arguments()
    shapes = sym.infer_shape(data=shape)

    def get_tensors(args, shapes, ctx):
        return {x: mx.nd.ones(y, ctx) for x, y in zip(args, shapes)}

    inputs = get_tensors(args, shapes[0], ctx)
    grads = get_tensors(args, shapes[0], ctx)

    try:
        exe = sym.bind(ctx, inputs, args_grad=grads)
        for _ in range(2):
            exe.forward(is_train=True)
            for y in exe.outputs:
                y.wait_to_read()
            exe.backward()
            for y in exe.grad_arrays:
                y.wait_to_read()
    except:  # pylint: disable=bare-except
        assert 0, "test_mkldnn_model exception in bind and execution"


def test_mkldnn_engine_threading():
    """
    This test will trigger mkldnn engine on different thread of execution.
    The test will first kickoff simple model calculation, and then uses a
    gluon data iterator to trigger different thread context, and executes
    the model on this new thread.
    """

    import mxnet as mx
    from mxnet import gluon, nd

    net = gluon.nn.HybridSequential()
    with net.name_scope():
        net.add(gluon.nn.Conv2D(channels=32, kernel_size=3, activation=None))
    net.collect_params().initialize(ctx=mx.cpu())
    class Dummy(gluon.data.Dataset):
        def __len__(self):
            return 2
        def __getitem__(self, key):
            return key, np.ones((3, 224, 224)), np.ones((10, ))

    loader = gluon.data.DataLoader(Dummy(), batch_size=2, num_workers=1)

    X = (32, 3, 32, 32)
    # trigger mkldnn execution thread
    y = net(nd.array(np.ones(X))).asnumpy()

    # Use Gluon dataloader to trigger different thread.
    # below line triggers different execution thread
    for _ in loader:
        y = net(nd.array(np.ones(X))).asnumpy()
        # output should have 0.3376348
        assert_almost_equal(y[0, 0, 0, 0], 0.3376348)
        break


def test_mkldnn_ndarray_slice():
    """
    This test will trigger gluon computation on mkldnn with ndarray slice
    """

    ctx = mx.cpu()
    net = gluon.nn.HybridSequential()
    with net.name_scope():
        net.add(gluon.nn.Conv2D(channels=32, kernel_size=3, activation=None))
        net.collect_params().initialize(ctx=ctx)
        x = mx.nd.array(np.ones([32, 3, 224, 224]), ctx)
        y = net(x)

        # trigger computation on ndarray slice
        assert_almost_equal(y[0].asnumpy()[0, 0, 0], 0.3376348)

@with_seed()
def test_reshape_before_conv():
    """
    This test will test gluon Conv2d computation on mkldnn with ndarray reshape
    """
    class Net(gluon.HybridBlock):
        def __init__(self, **kwargs):
            super(Net, self).__init__(**kwargs)
            with self.name_scope():
                self.conv0 = nn.Conv2D(10, (3, 3))
                self.conv1 = nn.Conv2D(5, (3, 3))

        def hybrid_forward(self, F, x):
            x_reshape = x.reshape((0, 0, 20, 5))
            y = self.conv0(x_reshape)
            y_reshape = y.reshape((0, 0, 9, 6))
            out = self.conv1(y_reshape)
            return out
    x = mx.nd.random.uniform(shape=(2, 4, 10, 10))
    x.attach_grad()
    net = Net()
    net.collect_params().initialize()
    with mx.autograd.record():
        out1 = net(x)
    out1.backward()
    dx1 = x.grad
    net.hybridize()
    with mx.autograd.record():
        out2 = net(x)
    out2.backward()
    mx.test_utils.assert_almost_equal(dx1.asnumpy(), x.grad.asnumpy(), rtol=1e-5, atol=1e-6)
    mx.test_utils.assert_almost_equal(out1.asnumpy(), out2.asnumpy(), rtol=1e-5, atol=1e-6)


@with_seed()
def test_slice_before_conv():
    """
    This test will test gluon Conv2d computation on mkldnn with ndarray slice
    """
    class Net(gluon.HybridBlock):
        def __init__(self, **kwargs):
            super(Net, self).__init__(**kwargs)
            with self.name_scope():
                self.conv0 = nn.Conv2D(4, (3, 3))
                self.conv1 = nn.Conv2D(4, (3, 3))

        def hybrid_forward(self, F, x):
            x_slice = x.slice(begin=(0, 0, 0, 0), end=(2, 4, 10, 10))
            y = self.conv0(x_slice)
            y_slice = y.slice(begin=(1, 0, 2, 2), end=(2, 1, 7, 7))
            out = self.conv1(y_slice)
            return out
    x = mx.nd.random.uniform(shape=(2, 10, 10, 10))
    x.attach_grad()
    net = Net()
    net.collect_params().initialize()
    with mx.autograd.record():
        out1 = net(x)
    out1.backward()
    dx1 = x.grad
    net.hybridize()
    with mx.autograd.record():
        out2 = net(x)
    out2.backward()
    mx.test_utils.assert_almost_equal(dx1.asnumpy(), x.grad.asnumpy(), rtol=1e-5, atol=1e-6)
    mx.test_utils.assert_almost_equal(out1.asnumpy(), out2.asnumpy(), rtol=1e-5, atol=1e-6)


@with_seed()
def test_slice_reshape_before_conv():
    """
    This test will test gluon Conv2d computation on mkldnn with ndarray reshape and slice
    """
    class Net(gluon.HybridBlock):
        def __init__(self, **kwargs):
            super(Net, self).__init__(**kwargs)
            with self.name_scope():
                self.conv0 = nn.Conv2D(4, (3, 3))
                self.conv1 = nn.Conv2D(4, (3, 3))

        def hybrid_forward(self, F, x):
            x_slice = x.slice(begin=(0, 0, 0, 0), end=(2, 4, 8, 9))
            y = self.conv0(x_slice)
            y_reshape = y.reshape((0, 0, 14, 3))
            out = self.conv1(y_reshape)
            return out
    x = mx.nd.random.uniform(shape=(2, 10, 10, 10))
    x.attach_grad()
    net = Net()
    net.collect_params().initialize()
    with mx.autograd.record():
        out1 = net(x)
    out1.backward()
    dx1 = x.grad
    net.hybridize()
    with mx.autograd.record():
        out2 = net(x)
    out2.backward()
    mx.test_utils.assert_almost_equal(dx1.asnumpy(), x.grad.asnumpy(), rtol=1e-5, atol=1e-6)
    mx.test_utils.assert_almost_equal(out1.asnumpy(), out2.asnumpy(), rtol=1e-5, atol=1e-6)


def test_mkldnn_sum_inplace_with_cpu_layout():
    """
    test inplace sum with mkldnn layout and cpu data
    """

    x_shape = (32, 3, 224, 224)
    x_npy = np.ones(x_shape)
    y_shape = (32, 32, 222, 222)
    y_npy = np.ones(y_shape)
    x = mx.sym.Variable("x")
    y = mx.sym.Variable("y")
    z = mx.symbol.Convolution(data=x, num_filter=32, kernel=(3, 3))
    z = mx.sym.add_n(z, y)
    exe = z.simple_bind(ctx=mx.cpu(), x=x_shape, y=y_shape)
    out = exe.forward(is_train=False, x=x_npy, y=y_npy)[0]
    assert_almost_equal(out[0].asnumpy()[0, 0, 0], 1.0)


if __name__ == '__main__':
    test_mkldnn_install()

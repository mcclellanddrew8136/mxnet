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

import mxnet as mx
from mxnet import gluon
import numpy as np
from numpy.testing import assert_allclose
import unittest
from mxnet.test_utils import almost_equal


def test_rnn():
    cell = gluon.rnn.RNNCell(100, prefix='rnn_')
    inputs = [mx.sym.Variable('rnn_t%d_data'%i) for i in range(3)]
    outputs, _ = cell.unroll(3, inputs)
    outputs = mx.sym.Group(outputs)
    assert sorted(cell.collect_params().keys()) == ['rnn_h2h_bias', 'rnn_h2h_weight', 'rnn_i2h_bias', 'rnn_i2h_weight']
    assert outputs.list_outputs() == ['rnn_t0_out_output', 'rnn_t1_out_output', 'rnn_t2_out_output']

    args, outs, auxs = outputs.infer_shape(rnn_t0_data=(10,50), rnn_t1_data=(10,50), rnn_t2_data=(10,50))
    assert outs == [(10, 100), (10, 100), (10, 100)]


def test_lstm():
    cell = gluon.rnn.LSTMCell(100, prefix='rnn_')
    inputs = [mx.sym.Variable('rnn_t%d_data'%i) for i in range(3)]
    outputs, _ = cell.unroll(3, inputs)
    outputs = mx.sym.Group(outputs)
    assert sorted(cell.collect_params().keys()) == ['rnn_h2h_bias', 'rnn_h2h_weight', 'rnn_i2h_bias', 'rnn_i2h_weight']
    assert outputs.list_outputs() == ['rnn_t0_out_output', 'rnn_t1_out_output', 'rnn_t2_out_output']

    args, outs, auxs = outputs.infer_shape(rnn_t0_data=(10,50), rnn_t1_data=(10,50), rnn_t2_data=(10,50))
    assert outs == [(10, 100), (10, 100), (10, 100)]


def test_lstm_forget_bias():
    forget_bias = 2.0
    stack = gluon.rnn.SequentialRNNCell()
    stack.add(gluon.rnn.LSTMCell(100, i2h_bias_initializer=mx.init.LSTMBias(forget_bias), prefix='l0_'))
    stack.add(gluon.rnn.LSTMCell(100, i2h_bias_initializer=mx.init.LSTMBias(forget_bias), prefix='l1_'))

    dshape = (32, 1, 200)
    data = mx.sym.Variable('data')

    sym, _ = stack.unroll(1, data, merge_outputs=True)
    mod = mx.mod.Module(sym, label_names=None, context=mx.cpu(0))
    mod.bind(data_shapes=[('data', dshape)], label_shapes=None)

    mod.init_params()

    bias_argument = next(x for x in sym.list_arguments() if x.endswith('i2h_bias'))
    expected_bias = np.hstack([np.zeros((100,)),
                               forget_bias * np.ones(100, ), np.zeros((2 * 100,))])
    assert_allclose(mod.get_params()[0][bias_argument].asnumpy(), expected_bias)

@unittest.skip("Test fails intermittently. Temporarily disabled until fixed. Tracked at https://github.com/apache/incubator-mxnet/issues/10104")
def test_lstm_cpu_inference():
    # should behave the same as lstm cell
    EXPECTED_LSTM_OUTPUT = np.array([[[0.72045636, 0.72045636, 0.95215213, 0.95215213],
                                  [0.72045636, 0.72045636, 0.95215213, 0.95215213]],
                                 [[0.95215213, 0.95215213, 0.72045636, 0.72045636],
                                  [0.95215213, 0.95215213, 0.72045636, 0.72045636]]])
    x = mx.nd.ones(shape=(2, 2, 2))
    model = mx.gluon.rnn.LSTM(2, num_layers=6, bidirectional=True)
    model.initialize(mx.init.One())
    y = model(x).asnumpy()

    mx.test_utils.assert_almost_equal(y, EXPECTED_LSTM_OUTPUT,
                                      rtol=1e-3, atol=1e-5)
    

def test_gru():
    cell = gluon.rnn.GRUCell(100, prefix='rnn_')
    inputs = [mx.sym.Variable('rnn_t%d_data'%i) for i in range(3)]
    outputs, _ = cell.unroll(3, inputs)
    outputs = mx.sym.Group(outputs)
    assert sorted(cell.collect_params().keys()) == ['rnn_h2h_bias', 'rnn_h2h_weight', 'rnn_i2h_bias', 'rnn_i2h_weight']
    assert outputs.list_outputs() == ['rnn_t0_out_output', 'rnn_t1_out_output', 'rnn_t2_out_output']

    args, outs, auxs = outputs.infer_shape(rnn_t0_data=(10,50), rnn_t1_data=(10,50), rnn_t2_data=(10,50))
    assert outs == [(10, 100), (10, 100), (10, 100)]


def test_residual():
    cell = gluon.rnn.ResidualCell(gluon.rnn.GRUCell(50, prefix='rnn_'))
    inputs = [mx.sym.Variable('rnn_t%d_data'%i) for i in range(2)]
    outputs, _ = cell.unroll(2, inputs)
    outputs = mx.sym.Group(outputs)
    assert sorted(cell.collect_params().keys()) == \
           ['rnn_h2h_bias', 'rnn_h2h_weight', 'rnn_i2h_bias', 'rnn_i2h_weight']
    # assert outputs.list_outputs() == \
    #        ['rnn_t0_out_plus_residual_output', 'rnn_t1_out_plus_residual_output']

    args, outs, auxs = outputs.infer_shape(rnn_t0_data=(10, 50), rnn_t1_data=(10, 50))
    assert outs == [(10, 50), (10, 50)]
    outputs = outputs.eval(rnn_t0_data=mx.nd.ones((10, 50)),
                           rnn_t1_data=mx.nd.ones((10, 50)),
                           rnn_i2h_weight=mx.nd.zeros((150, 50)),
                           rnn_i2h_bias=mx.nd.zeros((150,)),
                           rnn_h2h_weight=mx.nd.zeros((150, 50)),
                           rnn_h2h_bias=mx.nd.zeros((150,)))
    expected_outputs = np.ones((10, 50))
    assert np.array_equal(outputs[0].asnumpy(), expected_outputs)
    assert np.array_equal(outputs[1].asnumpy(), expected_outputs)


def test_residual_bidirectional():
    cell = gluon.rnn.ResidualCell(
            gluon.rnn.BidirectionalCell(
                gluon.rnn.GRUCell(25, prefix='rnn_l_'),
                gluon.rnn.GRUCell(25, prefix='rnn_r_')))

    inputs = [mx.sym.Variable('rnn_t%d_data'%i) for i in range(2)]
    outputs, _ = cell.unroll(2, inputs, merge_outputs=False)
    outputs = mx.sym.Group(outputs)
    assert sorted(cell.collect_params().keys()) == \
           ['rnn_l_h2h_bias', 'rnn_l_h2h_weight', 'rnn_l_i2h_bias', 'rnn_l_i2h_weight',
            'rnn_r_h2h_bias', 'rnn_r_h2h_weight', 'rnn_r_i2h_bias', 'rnn_r_i2h_weight']
    # assert outputs.list_outputs() == \
    #        ['bi_t0_plus_residual_output', 'bi_t1_plus_residual_output']

    args, outs, auxs = outputs.infer_shape(rnn_t0_data=(10, 50), rnn_t1_data=(10, 50))
    assert outs == [(10, 50), (10, 50)]
    outputs = outputs.eval(rnn_t0_data=mx.nd.ones((10, 50))+5,
                           rnn_t1_data=mx.nd.ones((10, 50))+5,
                           rnn_l_i2h_weight=mx.nd.zeros((75, 50)),
                           rnn_l_i2h_bias=mx.nd.zeros((75,)),
                           rnn_l_h2h_weight=mx.nd.zeros((75, 25)),
                           rnn_l_h2h_bias=mx.nd.zeros((75,)),
                           rnn_r_i2h_weight=mx.nd.zeros((75, 50)),
                           rnn_r_i2h_bias=mx.nd.zeros((75,)),
                           rnn_r_h2h_weight=mx.nd.zeros((75, 25)),
                           rnn_r_h2h_bias=mx.nd.zeros((75,)))
    expected_outputs = np.ones((10, 50))+5
    assert np.array_equal(outputs[0].asnumpy(), expected_outputs)
    assert np.array_equal(outputs[1].asnumpy(), expected_outputs)


def test_stack():
    cell = gluon.rnn.SequentialRNNCell()
    for i in range(5):
        if i == 1:
            cell.add(gluon.rnn.ResidualCell(gluon.rnn.LSTMCell(100, prefix='rnn_stack%d_' % i)))
        else:
            cell.add(gluon.rnn.LSTMCell(100, prefix='rnn_stack%d_'%i))
    inputs = [mx.sym.Variable('rnn_t%d_data'%i) for i in range(3)]
    outputs, _ = cell.unroll(3, inputs)
    outputs = mx.sym.Group(outputs)
    keys = sorted(cell.collect_params().keys())
    for i in range(5):
        assert 'rnn_stack%d_h2h_weight'%i in keys
        assert 'rnn_stack%d_h2h_bias'%i in keys
        assert 'rnn_stack%d_i2h_weight'%i in keys
        assert 'rnn_stack%d_i2h_bias'%i in keys
    assert outputs.list_outputs() == ['rnn_stack4_t0_out_output', 'rnn_stack4_t1_out_output', 'rnn_stack4_t2_out_output']

    args, outs, auxs = outputs.infer_shape(rnn_t0_data=(10,50), rnn_t1_data=(10,50), rnn_t2_data=(10,50))
    assert outs == [(10, 100), (10, 100), (10, 100)]


def test_bidirectional():
    cell = gluon.rnn.BidirectionalCell(
            gluon.rnn.LSTMCell(100, prefix='rnn_l0_'),
            gluon.rnn.LSTMCell(100, prefix='rnn_r0_'),
            output_prefix='rnn_bi_')
    inputs = [mx.sym.Variable('rnn_t%d_data'%i) for i in range(3)]
    outputs, _ = cell.unroll(3, inputs)
    outputs = mx.sym.Group(outputs)
    assert outputs.list_outputs() == ['rnn_bi_t0_output', 'rnn_bi_t1_output', 'rnn_bi_t2_output']

    args, outs, auxs = outputs.infer_shape(rnn_t0_data=(10,50), rnn_t1_data=(10,50), rnn_t2_data=(10,50))
    assert outs == [(10, 200), (10, 200), (10, 200)]


def test_zoneout():
    cell = gluon.rnn.ZoneoutCell(gluon.rnn.RNNCell(100, prefix='rnn_'), zoneout_outputs=0.5,
                              zoneout_states=0.5)
    inputs = [mx.sym.Variable('rnn_t%d_data'%i) for i in range(3)]
    outputs, _ = cell.unroll(3, inputs)
    outputs = mx.sym.Group(outputs)

    args, outs, auxs = outputs.infer_shape(rnn_t0_data=(10,50), rnn_t1_data=(10,50), rnn_t2_data=(10,50))
    assert outs == [(10, 100), (10, 100), (10, 100)]


def check_rnn_forward(layer, inputs, deterministic=True):
    inputs.attach_grad()
    layer.collect_params().initialize()
    with mx.autograd.record():
        out = layer.unroll(3, inputs, merge_outputs=False)[0]
        mx.autograd.backward(out)
        out = layer.unroll(3, inputs, merge_outputs=True)[0]
        out.backward()

    np_out = out.asnumpy()
    np_dx = inputs.grad.asnumpy()

    layer.hybridize()

    with mx.autograd.record():
        out = layer.unroll(3, inputs, merge_outputs=False)[0]
        mx.autograd.backward(out)
        out = layer.unroll(3, inputs, merge_outputs=True)[0]
        out.backward()

    if deterministic:
        mx.test_utils.assert_almost_equal(np_out, out.asnumpy(), rtol=1e-3, atol=1e-5)
        mx.test_utils.assert_almost_equal(np_dx, inputs.grad.asnumpy(), rtol=1e-3, atol=1e-5)


def test_rnn_cells():
    check_rnn_forward(gluon.rnn.LSTMCell(100, input_size=200), mx.nd.ones((8, 3, 200)))
    check_rnn_forward(gluon.rnn.RNNCell(100, input_size=200), mx.nd.ones((8, 3, 200)))
    check_rnn_forward(gluon.rnn.GRUCell(100, input_size=200), mx.nd.ones((8, 3, 200)))

    bilayer = gluon.rnn.BidirectionalCell(gluon.rnn.LSTMCell(100, input_size=200),
                                       gluon.rnn.LSTMCell(100, input_size=200))
    check_rnn_forward(bilayer, mx.nd.ones((8, 3, 200)))

    check_rnn_forward(gluon.rnn.DropoutCell(0.5), mx.nd.ones((8, 3, 200)), False)

    check_rnn_forward(gluon.rnn.ZoneoutCell(gluon.rnn.LSTMCell(100, input_size=200),
                                         0.5, 0.2),
                      mx.nd.ones((8, 3, 200)), False)

    net = gluon.rnn.SequentialRNNCell()
    net.add(gluon.rnn.LSTMCell(100, input_size=200))
    net.add(gluon.rnn.RNNCell(100, input_size=100))
    net.add(gluon.rnn.GRUCell(100, input_size=100))
    check_rnn_forward(net, mx.nd.ones((8, 3, 200)))

def check_rnn_layer_forward(layer, inputs, states=None):
    layer.collect_params().initialize()
    inputs.attach_grad()
    with mx.autograd.record():
        out = layer(inputs, states)
        if states is not None:
            assert isinstance(out, tuple) and len(out) == 2
            out = out[0]
        else:
            assert isinstance(out, mx.nd.NDArray)
        out.backward()

    np_out = out.asnumpy()
    np_dx = inputs.grad.asnumpy()

    layer.hybridize()

    with mx.autograd.record():
        out = layer(inputs, states)
        if states is not None:
            assert isinstance(out, tuple) and len(out) == 2
            out = out[0]
        else:
            assert isinstance(out, mx.nd.NDArray)
        out.backward()

    mx.test_utils.assert_almost_equal(np_out, out.asnumpy(), rtol=1e-3, atol=1e-5)
    mx.test_utils.assert_almost_equal(np_dx, inputs.grad.asnumpy(), rtol=1e-3, atol=1e-5)


def test_rnn_layers():
    check_rnn_layer_forward(gluon.rnn.RNN(10, 2), mx.nd.ones((8, 3, 20)))
    check_rnn_layer_forward(gluon.rnn.RNN(10, 2), mx.nd.ones((8, 3, 20)), mx.nd.ones((2, 3, 10)))
    check_rnn_layer_forward(gluon.rnn.LSTM(10, 2), mx.nd.ones((8, 3, 20)))
    check_rnn_layer_forward(gluon.rnn.LSTM(10, 2), mx.nd.ones((8, 3, 20)), [mx.nd.ones((2, 3, 10)), mx.nd.ones((2, 3, 10))])
    check_rnn_layer_forward(gluon.rnn.GRU(10, 2), mx.nd.ones((8, 3, 20)))
    check_rnn_layer_forward(gluon.rnn.GRU(10, 2), mx.nd.ones((8, 3, 20)), mx.nd.ones((2, 3, 10)))

    net = gluon.nn.Sequential()
    net.add(gluon.rnn.LSTM(10, 2, bidirectional=True))
    net.add(gluon.nn.BatchNorm(axis=2))
    net.add(gluon.nn.Flatten())
    net.add(gluon.nn.Dense(3, activation='relu'))
    net.collect_params().initialize()
    with mx.autograd.record():
        net(mx.nd.ones((2, 3, 10))).backward()


def test_rnn_unroll_variant_length():
    # Test for imperative usage
    cell_list = []
    for base_cell_class in [gluon.rnn.RNNCell, gluon.rnn.LSTMCell, gluon.rnn.GRUCell]:
        cell_list.append(base_cell_class(20))
        cell_list.append(gluon.rnn.BidirectionalCell(
                         l_cell=base_cell_class(20),
                         r_cell=base_cell_class(20)))
        cell_list.append(gluon.contrib.rnn.VariationalDropoutCell(base_cell=base_cell_class(20)))
    stack_res_rnn_cell = gluon.rnn.SequentialRNNCell()
    stack_res_rnn_cell.add(gluon.rnn.ResidualCell(base_cell=gluon.rnn.RNNCell(20)))
    stack_res_rnn_cell.add(gluon.rnn.ResidualCell(base_cell=gluon.rnn.RNNCell(20)))
    cell_list.append(stack_res_rnn_cell)
    batch_size = 4
    max_length = 10
    valid_length = [3, 10, 5, 6]
    valid_length_nd = mx.nd.array(valid_length)
    for cell in cell_list:
        cell.collect_params().initialize()
        cell.hybridize()
        # Test for NTC layout
        data_nd = mx.nd.random.normal(0, 1, shape=(batch_size, max_length, 20))
        outs, states = cell.unroll(length=max_length, inputs=data_nd,
                                   valid_length=valid_length_nd,
                                   merge_outputs=True,
                                   layout='NTC')
        for i, ele_length in enumerate(valid_length):
            # Explicitly unroll each sequence and compare the final states and output
            ele_out, ele_states = cell.unroll(length=ele_length,
                                              inputs=data_nd[i:(i+1), :ele_length, :],
                                              merge_outputs=True,
                                              layout='NTC')
            assert_allclose(ele_out.asnumpy(), outs[i:(i+1), :ele_length, :].asnumpy(),
                            atol=1E-4, rtol=1E-4)
            if ele_length < max_length:
                # Check the padded outputs are all zero
                assert_allclose(outs[i:(i+1), ele_length:max_length, :].asnumpy(), 0)
            for valid_out_state, gt_state in zip(states, ele_states):
                assert_allclose(valid_out_state[i:(i+1)].asnumpy(), gt_state.asnumpy(),
                                atol=1E-4, rtol=1E-4)

        # Test for TNC layout
        data_nd = mx.nd.random.normal(0, 1, shape=(max_length, batch_size, 20))
        outs, states = cell.unroll(length=max_length, inputs=data_nd,
                                   valid_length=valid_length_nd,
                                   layout='TNC')
        for i, ele_length in enumerate(valid_length):
            # Explicitly unroll each sequence and compare the final states and output
            ele_out, ele_states = cell.unroll(length=ele_length,
                                              inputs=data_nd[:ele_length, i:(i+1), :],
                                              merge_outputs=True,
                                              layout='TNC')
            assert_allclose(ele_out.asnumpy(), outs[:ele_length, i:(i + 1), :].asnumpy(),
                            atol=1E-4, rtol=1E-4)
            if ele_length < max_length:
                # Check the padded outputs are all zero
                assert_allclose(outs[ele_length:max_length, i:(i+1), :].asnumpy(), 0)
            for valid_out_state, gt_state in zip(states, ele_states):
                assert_allclose(valid_out_state[i:(i+1)].asnumpy(), gt_state.asnumpy(),
                                atol=1E-4, rtol=1E-4)
    # For symbolic test, we need to make sure that it can be binded and run
    data = mx.sym.var('data', shape=(4, 10, 2))
    cell = gluon.rnn.RNNCell(100)
    valid_length = mx.sym.var('valid_length', shape=(4,))
    outs, states = cell.unroll(length=10, inputs=data, valid_length=valid_length,
                               merge_outputs=True, layout='NTC')
    mod = mx.mod.Module(states[0], data_names=('data', 'valid_length'), label_names=None,
                        context=mx.cpu())
    mod.bind(data_shapes=[('data', (4, 10, 2)), ('valid_length', (4,))], label_shapes=None)
    mod.init_params()
    mod.forward(mx.io.DataBatch([mx.random.normal(0, 1, (4, 10, 2)), mx.nd.array([3, 6, 10, 2])]))
    mod.get_outputs()[0].asnumpy()


def test_cell_fill_shape():
    cell = gluon.rnn.LSTMCell(10)
    cell.hybridize()
    check_rnn_forward(cell, mx.nd.ones((2, 3, 7)))
    assert cell.i2h_weight.shape[1] == 7, cell.i2h_weight.shape[1]

def test_layer_fill_shape():
    layer = gluon.rnn.LSTM(10)
    layer.hybridize()
    check_rnn_layer_forward(layer, mx.nd.ones((3, 2, 7)))
    print(layer)
    assert layer.i2h_weight[0].shape[1] == 7, layer.i2h_weight[0].shape[1]


if __name__ == '__main__':
    import nose
    nose.runmodule()

# coding: utf-8
# pylint: disable=no-member, invalid-name, protected-access, no-self-use
# pylint: disable=too-many-branches, too-many-arguments, no-self-use
# pylint: disable=too-many-lines, arguments-differ
"""Definition of various recurrent neural network layers."""
from __future__ import print_function

from ... import ndarray
from ..nn import Block
from . import rnn_cell


class _RNNLayer(Block):
    """Implementation of recurrent layers."""
    def __init__(self, hidden_size, num_layers, layout,
                 dropout, bidirectional, input_size,
                 i2h_weight_initializer, h2h_weight_initializer,
                 i2h_bias_initializer, h2h_bias_initializer,
                 mode, **kwargs):
        super(_RNNLayer, self).__init__(**kwargs)
        assert layout == 'TNC' or layout == 'NTC', \
            "Invalid layout %s; must be one of ['TNC' or 'NTC']"%layout
        self._hidden_size = hidden_size
        self._num_layers = num_layers
        self._mode = mode
        self._layout = layout
        self._dropout = dropout
        self._dir = 2 if bidirectional else 1
        self._input_size = input_size
        self._i2h_weight_initializer = i2h_weight_initializer
        self._h2h_weight_initializer = h2h_weight_initializer
        self._i2h_bias_initializer = i2h_bias_initializer
        self._h2h_bias_initializer = h2h_bias_initializer

        self._gates = {'rnn_relu': 1, 'rnn_tanh': 1, 'lstm': 4, 'gru': 3}[mode]

        self.i2h_weight = []
        self.h2h_weight = []
        self.i2h_bias = []
        self.h2h_bias = []

        ng, ni, nh = self._gates, input_size, hidden_size
        for i in range(num_layers):
            for j in (['l', 'r'] if self._dir == 2 else ['l']):
                self.i2h_weight.append(
                    self.params.get('%s%d_i2h_weight'%(j, i), shape=(ng*nh, ni),
                                    init=i2h_weight_initializer,
                                    allow_deferred_init=True))
                self.h2h_weight.append(
                    self.params.get('%s%d_h2h_weight'%(j, i), shape=(ng*nh, nh),
                                    init=h2h_weight_initializer,
                                    allow_deferred_init=True))
                self.i2h_bias.append(
                    self.params.get('%s%d_i2h_bias'%(j, i), shape=(ng*nh,),
                                    init=i2h_bias_initializer,
                                    allow_deferred_init=True))
                self.h2h_bias.append(
                    self.params.get('%s%d_h2h_bias'%(j, i), shape=(ng*nh,),
                                    init=h2h_bias_initializer,
                                    allow_deferred_init=True))
            ni = nh * self._dir

        self._unfused = self._unfuse()

    def __repr__(self):
        s = '{name}({mapping}, {_layout}'
        if self._num_layers != 1:
            s += ', num_layers={_num_layers}'
        if self._dropout != 0:
            s += ', dropout={_dropout}'
        if self._dir == 2:
            s += ', bidirectional'
        s += ')'
        mapping = ('{_input_size} -> {_hidden_size}'.format(**self.__dict__) if self._input_size
                   else self._hidden_size)
        return s.format(name=self.__class__.__name__,
                        mapping=mapping,
                        **self.__dict__)

    def state_info(self, batch_size=0):
        raise NotImplementedError

    def _unfuse(self):
        """Unfuses the fused RNN in to a stack of rnn cells."""
        get_cell = {'rnn_relu': lambda **kwargs: rnn_cell.RNNCell(self._hidden_size,
                                                                  activation='relu',
                                                                  **kwargs),
                    'rnn_tanh': lambda **kwargs: rnn_cell.RNNCell(self._hidden_size,
                                                                  activation='tanh',
                                                                  **kwargs),
                    'lstm': lambda **kwargs: rnn_cell.LSTMCell(self._hidden_size,
                                                               **kwargs),
                    'gru': lambda **kwargs: rnn_cell.GRUCell(self._hidden_size,
                                                             **kwargs)}[self._mode]

        stack = rnn_cell.SequentialRNNCell(prefix=self.prefix, params=self.params)
        with stack.name_scope():
            ni = self._input_size
            for i in range(self._num_layers):
                kwargs = {'input_size': ni,
                          'i2h_weight_initializer': self._i2h_weight_initializer,
                          'h2h_weight_initializer': self._h2h_weight_initializer,
                          'i2h_bias_initializer': self._i2h_bias_initializer,
                          'h2h_bias_initializer': self._h2h_bias_initializer}
                if self._dir == 2:
                    stack.add(rnn_cell.BidirectionalCell(
                        get_cell(prefix='l%d_'%i, **kwargs),
                        get_cell(prefix='r%d_'%i, **kwargs)))
                else:
                    stack.add(get_cell(prefix='l%d_'%i, **kwargs))

                if self._dropout > 0 and i != self._num_layers - 1:
                    stack.add(rnn_cell.DropoutCell(self._dropout))

                ni = self._hidden_size * self._dir

        return stack

    def begin_state(self, batch_size=0, func=ndarray.zeros, **kwargs):
        """Initial state for this cell.

        Parameters
        ----------
        batch_size: int
            Only required for `NDArray` API. Size of the batch ('N' in layout).
            Dimension of the input.
        func : callable, default `symbol.zeros`
            Function for creating initial state.

            For Symbol API, func can be `symbol.zeros`, `symbol.uniform`,
            `symbol.var` etc. Use `symbol.var` if you want to directly
            feed input as states.

            For NDArray API, func can be `ndarray.zeros`, `ndarray.ones`, etc.

        **kwargs :
            Additional keyword arguments passed to func. For example
            `mean`, `std`, `dtype`, etc.

        Returns
        -------
        states : nested list of Symbol
            Starting states for the first RNN step.
        """
        states = []
        for i, info in enumerate(self.state_info(batch_size)):
            if info is not None:
                info.update(kwargs)
            else:
                info = kwargs
            states.append(func(name='%sh0_%d'%(self.prefix, i), **info))
        return states

    def forward(self, inputs, states):
        if isinstance(states, ndarray.NDArray):
            states = [states]
        batch_size = states[0].shape[self._layout.find('N')]
        for state, info in zip(states, self.state_info(batch_size)):
            if state.shape != info['shape']:
                raise ValueError(
                    "Invalid recurrent state shape. Expecting %s, got %s."%(
                        str(info['shape']), str(state.shape)))
        if self._input_size == 0:
            for i in range(self._dir):
                self.i2h_weight[i].shape = (self._gates*self._hidden_size, inputs.shape[2])
                self.i2h_weight[i]._finish_deferred_init()
        if inputs.context.device_type == 'gpu':
            return self._forward_gpu(inputs, states)
        return self._forward_cpu(inputs, states)

    def _forward_cpu(self, inputs, states):
        ns = len(states)
        axis = self._layout.find('T')
        states = sum(zip(*((j for j in i) for i in states)), ())
        outputs, states = self._unfused.unroll(
            inputs.shape[axis], inputs, states,
            layout=self._layout, merge_outputs=True)
        new_states = []
        for i in range(ns):
            state = ndarray.concat(*(j.reshape((1,)+j.shape) for j in states[i::ns]), dim=0)
            new_states.append(state)

        return outputs, new_states

    def _forward_gpu(self, inputs, states):
        if self._layout == 'NTC':
            inputs = ndarray.swapaxes(inputs, dim1=0, dim2=1)
        ctx = inputs.context
        params = sum(zip(self.i2h_weight, self.h2h_weight), ())
        params += sum(zip(self.i2h_bias, self.h2h_bias), ())
        params = (i.data(ctx).reshape((-1,)) for i in params)
        params = ndarray.concat(*params, dim=0)

        rnn = ndarray.RNN(inputs, params, *states, state_size=self._hidden_size,
                          num_layers=self._num_layers, bidirectional=self._dir == 2,
                          p=self._dropout, state_outputs=True, mode=self._mode)

        if self._mode == 'lstm':
            outputs, states = rnn[0], [rnn[1], rnn[2]]
        else:
            outputs, states = rnn[0], [rnn[1]]

        if self._layout == 'NTC':
            outputs = ndarray.swapaxes(outputs, dim1=0, dim2=1)

        return outputs, states


class RNN(_RNNLayer):
    r"""Applies a multi-layer Elman RNN with `tanh` or `ReLU` non-linearity to an input sequence.

    For each element in the input sequence, each layer computes the following
    function:

    .. math::
        h_t = \tanh(w_{ih} * x_t + b_{ih}  +  w_{hh} * h_{(t-1)} + b_{hh})

    where :math:`h_t` is the hidden state at time `t`, and :math:`x_t` is the hidden
    state of the previous layer at time `t` or :math:`input_t` for the first layer.
    If nonlinearity='relu', then `ReLU` is used instead of `tanh`.

    Parameters
    ----------
    hidden_size: int
        The number of features in the hidden state h.
    num_layers: int, default 1
        Number of recurrent layers.
    activation: {'relu' or 'tanh'}, default 'tanh'
        The activation function to use.
    layout : str, default 'TNC'
        The format of input and output tensors. T, N and C stand for
        sequence length, batch size, and feature dimensions respectively.
    dropout: float, default 0
        If non-zero, introduces a dropout layer on the outputs of each
        RNN layer except the last layer.
    bidirectional: bool, default False
        If `True`, becomes a bidirectional RNN.
    i2h_weight_initializer : str or Initializer
        Initializer for the input weights matrix, used for the linear
        transformation of the inputs.
    h2h_weight_initializer : str or Initializer
        Initializer for the recurrent weights matrix, used for the linear
        transformation of the recurrent state.
    i2h_bias_initializer : str or Initializer
        Initializer for the bias vector.
    h2h_bias_initializer : str or Initializer
        Initializer for the bias vector.
    input_size: int, default 0
        The number of expected features in the input x.
        If not specified, it will be inferred from input.
    prefix : str or None
        Prefix of this `Block`.
    params : ParameterDict or None
        Shared Parameters for this `Block`.


    Input shapes:
        The input shape depends on `layout`. For `layout='TNC'`, the
        input has shape `(sequence_length, batch_size, input_size)`


    Output shape:
        The output shape depends on `layout`. For `layout='TNC'`, the
        output has shape `(sequence_length, batch_size, num_hidden)`.
        If `bidirectional` is True, output shape will instead be
        `(sequence_length, batch_size, 2*num_hidden)`

    Recurrent state shape:
        The recurrent state's shape is `(num_layers, batch_size, num_hidden)`.
        If `bidirectional` is True, state shape will instead be
        `(2*num_layers, batch_size, num_hidden)`


    Examples
    --------
    >>> layer = mx.gluon.rnn.RNN(100, 3)
    >>> layer.initialize()
    >>> input = mx.nd.random_uniform(shape=(5, 3, 10))
    >>> h0 = mx.nd.random_uniform(shape=(3, 3, 100))
    >>> output, hn = layer(input, h0)
    """
    def __init__(self, hidden_size, num_layers=1, activation='relu',
                 layout='TNC', dropout=0, bidirectional=False,
                 i2h_weight_initializer=None, h2h_weight_initializer=None,
                 i2h_bias_initializer='zeros', h2h_bias_initializer='zeros',
                 input_size=0, **kwargs):
        super(RNN, self).__init__(hidden_size, num_layers, layout,
                                  dropout, bidirectional, input_size,
                                  i2h_weight_initializer, h2h_weight_initializer,
                                  i2h_bias_initializer, h2h_bias_initializer,
                                  'rnn_'+activation, **kwargs)

    def state_info(self, batch_size=0):
        shape_dict = {
            'T': self._num_layers * self._dir,
            'N': batch_size,
            'C': self._hidden_size
        }

        return [{
            'shape': tuple(shape_dict[dim] for dim in self._layout),
            '__layout__': self._layout
        }]


class LSTM(_RNNLayer):
    r"""Applies a multi-layer long short-term memory (LSTM) RNN to an input sequence.

    For each element in the input sequence, each layer computes the following
    function:

    .. math::
        \begin{array}{ll}
        i_t = sigmoid(W_{ii} x_t + b_{ii} + W_{hi} h_{(t-1)} + b_{hi}) \\
        f_t = sigmoid(W_{if} x_t + b_{if} + W_{hf} h_{(t-1)} + b_{hf}) \\
        g_t = \tanh(W_{ig} x_t + b_{ig} + W_{hc} h_{(t-1)} + b_{hg}) \\
        o_t = sigmoid(W_{io} x_t + b_{io} + W_{ho} h_{(t-1)} + b_{ho}) \\
        c_t = f_t * c_{(t-1)} + i_t * g_t \\
        h_t = o_t * \tanh(c_t)
        \end{array}

    where :math:`h_t` is the hidden state at time `t`, :math:`c_t` is the
    cell state at time `t`, :math:`x_t` is the hidden state of the previous
    layer at time `t` or :math:`input_t` for the first layer, and :math:`i_t`,
    :math:`f_t`, :math:`g_t`, :math:`o_t` are the input, forget, cell, and
    out gates, respectively.

    Parameters
    ----------
    hidden_size: int
        The number of features in the hidden state h.
    num_layers: int, default 1
        Number of recurrent layers.
    layout : str, default 'TNC'
        The format of input and output tensors. T, N and C stand for
        sequence length, batch size, and feature dimensions respectively.
    dropout: float, default 0
        If non-zero, introduces a dropout layer on the outputs of each
        RNN layer except the last layer.
    bidirectional: bool, default False
        If `True`, becomes a bidirectional RNN.
    i2h_weight_initializer : str or Initializer
        Initializer for the input weights matrix, used for the linear
        transformation of the inputs.
    h2h_weight_initializer : str or Initializer
        Initializer for the recurrent weights matrix, used for the linear
        transformation of the recurrent state.
    i2h_bias_initializer : str or Initializer, default 'lstmbias'
        Initializer for the bias vector. By default, bias for the forget
        gate is initialized to 1 while all other biases are initialized
        to zero.
    h2h_bias_initializer : str or Initializer
        Initializer for the bias vector.
    input_size: int, default 0
        The number of expected features in the input x.
        If not specified, it will be inferred from input.
    prefix : str or None
        Prefix of this `Block`.
    params : `ParameterDict` or `None`
        Shared Parameters for this `Block`.


    Input shapes:
        The input shape depends on `layout`. For `layout='TNC'`, the
        input has shape `(sequence_length, batch_size, input_size)`

    Output shape:
        The output shape depends on `layout`. For `layout='TNC'`, the
        output has shape `(sequence_length, batch_size, num_hidden)`.
        If `bidirectional` is True, output shape will instead be
        `(sequence_length, batch_size, 2*num_hidden)`

    Recurrent state shape:
        The recurrent state is a list of two NDArrays. Both has shape
        `(num_layers, batch_size, num_hidden)`.
        If `bidirectional` is True, state shape will instead be
        `(2*num_layers, batch_size, num_hidden)`.


    Examples
    --------
    >>> layer = mx.gluon.rnn.LSTM(100, 3)
    >>> layer.initialize()
    >>> input = mx.nd.random_uniform(shape=(5, 3, 10))
    >>> h0 = mx.nd.random_uniform(shape=(3, 3, 100))
    >>> c0 = mx.nd.random_uniform(shape=(3, 3, 100))
    >>> output, hn = layer(input, [h0, c0])
    """
    def __init__(self, hidden_size, num_layers=1, layout='TNC',
                 dropout=0, bidirectional=False, input_size=0,
                 i2h_weight_initializer=None, h2h_weight_initializer=None,
                 i2h_bias_initializer='zeros', h2h_bias_initializer='zeros',
                 **kwargs):
        super(LSTM, self).__init__(hidden_size, num_layers, layout,
                                   dropout, bidirectional, input_size,
                                   i2h_weight_initializer, h2h_weight_initializer,
                                   i2h_bias_initializer, h2h_bias_initializer,
                                   'lstm', **kwargs)

    def state_info(self, batch_size=0):
        shape_dict = {
            'T': self._num_layers * self._dir,
            'N': batch_size,
            'C': self._hidden_size
        }

        return [{
            'shape': tuple(shape_dict[dim] for dim in self._layout),
            '__layout__': self._layout
        }, {
            'shape': tuple(shape_dict[dim] for dim in self._layout),
            '__layout__': self._layout
        }]


class GRU(_RNNLayer):
    r"""Applies a multi-layer gated recurrent unit (GRU) RNN to an input sequence.

    For each element in the input sequence, each layer computes the following
    function:

    .. math::
        \begin{array}{ll}
        r_t = sigmoid(W_{ir} x_t + b_{ir} + W_{hr} h_{(t-1)} + b_{hr}) \\
        i_t = sigmoid(W_{ii} x_t + b_{ii} + W_hi h_{(t-1)} + b_{hi}) \\
        n_t = \tanh(W_{in} x_t + b_{in} + r_t * (W_{hn} h_{(t-1)}+ b_{hn})) \\
        h_t = (1 - i_t) * n_t + i_t * h_{(t-1)} \\
        \end{array}

    where :math:`h_t` is the hidden state at time `t`, :math:`x_t` is the hidden
    state of the previous layer at time `t` or :math:`input_t` for the first layer,
    and :math:`r_t`, :math:`i_t`, :math:`n_t` are the reset, input, and new gates, respectively.

    Parameters
    ----------
    hidden_size: int
        The number of features in the hidden state h
    num_layers: int, default 1
        Number of recurrent layers.
    layout : str, default 'TNC'
        The format of input and output tensors. T, N and C stand for
        sequence length, batch size, and feature dimensions respectively.
    dropout: float, default 0
        If non-zero, introduces a dropout layer on the outputs of each
        RNN layer except the last layer
    bidirectional: bool, default False
        If True, becomes a bidirectional RNN.
    i2h_weight_initializer : str or Initializer
        Initializer for the input weights matrix, used for the linear
        transformation of the inputs.
    h2h_weight_initializer : str or Initializer
        Initializer for the recurrent weights matrix, used for the linear
        transformation of the recurrent state.
    i2h_bias_initializer : str or Initializer
        Initializer for the bias vector.
    h2h_bias_initializer : str or Initializer
        Initializer for the bias vector.
    input_size: int, default 0
        The number of expected features in the input x.
        If not specified, it will be inferred from input.
    prefix : str or None
        Prefix of this `Block`.
    params : ParameterDict or None
        Shared Parameters for this `Block`.


    Input shapes:
        The input shape depends on `layout`. For `layout='TNC'`, the
        input has shape `(sequence_length, batch_size, input_size)`

    Output shape:
        The output shape depends on `layout`. For `layout='TNC'`, the
        output has shape `(sequence_length, batch_size, num_hidden)`.
        If `bidirectional` is True, output shape will instead be
        `(sequence_length, batch_size, 2*num_hidden)`

    Recurrent state shape:
        The recurrent state's shape is `(num_layers, batch_size, num_hidden)`.
        If `bidirectional` is True, state shape will instead be
        `(2*num_layers, batch_size, num_hidden)`


    Examples
    --------
    >>> layer = mx.gluon.rnn.GRU(100, 3)
    >>> layer.initialize()
    >>> input = mx.nd.random_uniform(shape=(5, 3, 10))
    >>> h0 = mx.nd.random_uniform(shape=(3, 3, 100))
    >>> output, hn = layer(input, h0)
    """
    def __init__(self, hidden_size, num_layers=1, layout='TNC',
                 dropout=0, bidirectional=False, input_size=0,
                 i2h_weight_initializer=None, h2h_weight_initializer=None,
                 i2h_bias_initializer='zeros', h2h_bias_initializer='zeros',
                 **kwargs):
        super(GRU, self).__init__(hidden_size, num_layers, layout,
                                  dropout, bidirectional, input_size,
                                  i2h_weight_initializer, h2h_weight_initializer,
                                  i2h_bias_initializer, h2h_bias_initializer,
                                  'gru', **kwargs)

    def state_info(self, batch_size=0):
        shape_dict = {
            'T': self._num_layers * self._dir,
            'N': batch_size,
            'C': self._hidden_size
        }

        return [{
            'shape': tuple(shape_dict[dim] for dim in self._layout),
            '__layout__': self._layout
        }]

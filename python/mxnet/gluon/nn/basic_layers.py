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
# pylint: disable= arguments-differ
"""Basic neural network layers."""

from ..block import Block, HybridBlock
from ..utils import _indent


class Sequential(Block):
    """Stacks `Block`s sequentially.

    Example::

        net = nn.Sequential()
        # use net's name_scope to give child Blocks appropriate names.
        with net.name_scope():
            net.add(nn.Dense(10, activation='relu'))
            net.add(nn.Dense(20))
    """
    def __init__(self, prefix=None, params=None):
        super(Sequential, self).__init__(prefix=prefix, params=params)

    def add(self, block):
        """Adds block on top of the stack."""
        self.register_child(block)

    def forward(self, x):
        for block in self._children:
            x = block(x)
        return x

    def __repr__(self):
        s = '{name}(\n{modstr}\n)'
        modstr = '\n'.join(['  ({key}): {block}'.format(key=key,
                                                        block=_indent(block.__repr__(), 2))
                            for key, block in enumerate(self._children)
                            if isinstance(block, Block)])
        return s.format(name=self.__class__.__name__,
                        modstr=modstr)

    def __getitem__(self, i):
        return self._children[i]

    def __len__(self):
        return len(self._children)


class HybridSequential(HybridBlock):
    """Stacks `HybridBlock`s sequentially.

    Example::

        net = nn.Sequential()
        # use net's name_scope to give child Blocks appropriate names.
        with net.name_scope():
            net.add(nn.Dense(10, activation='relu'))
            net.add(nn.Dense(20))
    """
    def __init__(self, prefix=None, params=None):
        super(HybridSequential, self).__init__(prefix=prefix, params=params)

    def add(self, block):
        """Adds block on top of the stack."""
        self.register_child(block)

    def hybrid_forward(self, F, x):
        for block in self._children:
            x = block(x)
        return x

    def __repr__(self):
        s = '{name}(\n{modstr}\n)'
        modstr = '\n'.join(['  ({key}): {block}'.format(key=key,
                                                        block=_indent(block.__repr__(), 2))
                            for key, block in enumerate(self._children)
                            if isinstance(block, Block)])
        return s.format(name=self.__class__.__name__,
                        modstr=modstr)

    def __getitem__(self, i):
        return self._children[i]

    def __len__(self):
        return len(self._children)


class Dense(HybridBlock):
    r"""Just your regular densely-connected NN layer.

    `Dense` implements the operation:
    `output = activation(dot(input, weight) + bias)`
    where `activation` is the element-wise activation function
    passed as the `activation` argument, `weight` is a weights matrix
    created by the layer, and `bias` is a bias vector created by the layer
    (only applicable if `use_bias` is `True`).

    Note: the input must be a tensor with rank 2. Use `flatten` to convert it
    to rank 2 manually if necessary.

    Parameters
    ----------
    units : int
        Dimensionality of the output space.
    activation : str
        Activation function to use. See help on `Activation` layer.
        If you don't specify anything, no activation is applied
        (ie. "linear" activation: `a(x) = x`).
    use_bias : bool
        Whether the layer uses a bias vector.
    flatten: bool
        Whether the input tensor should be flattened.
        If true, all but the first axis of input data are collapsed together.
        If false, all but the last axis of input data are kept the same, and the transformation
        applies on the last axis.
    weight_initializer : str or `Initializer`
        Initializer for the `kernel` weights matrix.
    bias_initializer: str or `Initializer`
        Initializer for the bias vector.
    in_units : int, optional
        Size of the input data. If not specified, initialization will be
        deferred to the first time `forward` is called and `in_units`
        will be inferred from the shape of input data.
    prefix : str or None
        See document of `Block`.
    params : ParameterDict or None
        See document of `Block`.


    If ``flatten`` is set to be True, then the shapes are:
    Input shape:
        An N-D input with shape
        `(batch_size, x1, x2, ..., xn) with x1 * x2 * ... * xn equal to in_units`.

    Output shape:
        The output would have shape `(batch_size, units)`.

    If ``flatten`` is set to be false, then the shapes are:
    Input shape:
        An N-D input with shape
        `(x1, x2, ..., xn, in_units)`.

    Output shape:
        The output would have shape `(x1, x2, ..., xn, units)`.
    """
    def __init__(self, units, activation=None, use_bias=True, flatten=True,
                 weight_initializer=None, bias_initializer='zeros',
                 in_units=0, **kwargs):
        super(Dense, self).__init__(**kwargs)
        self._flatten = flatten
        with self.name_scope():
            self._units = units
            self._in_units = in_units
            self.weight = self.params.get('weight', shape=(units, in_units),
                                          init=weight_initializer,
                                          allow_deferred_init=True)
            if use_bias:
                self.bias = self.params.get('bias', shape=(units,),
                                            init=bias_initializer,
                                            allow_deferred_init=True)
            else:
                self.bias = None
            if activation is not None:
                self.act = Activation(activation, prefix=activation+'_')
            else:
                self.act = None

    def hybrid_forward(self, F, x, weight, bias=None):
        act = F.FullyConnected(x, weight, bias, no_bias=bias is None, num_hidden=self._units,
                               flatten=self._flatten, name='fwd')
        if self.act is not None:
            act = self.act(act)
        return act

    def __repr__(self):
        s = '{name}({layout}, {act})'
        return s.format(name=self.__class__.__name__,
                        act=self.act if self.act else 'linear',
                        layout='{0} -> {1}'.format(self._in_units, self._units) if self._in_units
                        else self._units)


class Activation(HybridBlock):
    """Applies an activation function to input.

    Parameters
    ----------
    activation : str
        Name of activation function to use.
        See :func:`~mxnet.ndarray.Activation` for available choices.


    Input shape:
        Arbitrary.

    Output shape:
        Same shape as input.
    """
    def __init__(self, activation, **kwargs):
        self._act_type = activation
        super(Activation, self).__init__(**kwargs)

    def _alias(self):
        return self._act_type

    def hybrid_forward(self, F, x):
        return F.Activation(x, act_type=self._act_type, name='fwd')

    def __repr__(self):
        s = '{name}({_act_type})'
        return s.format(name=self.__class__.__name__,
                        **self.__dict__)


class Dropout(HybridBlock):
    """Applies Dropout to the input.

    Dropout consists in randomly setting a fraction `rate` of input units
    to 0 at each update during training time, which helps prevent overfitting.

    Parameters
    ----------
    rate : float
        Fraction of the input units to drop. Must be a number between 0 and 1.


    Input shape:
        Arbitrary.

    Output shape:
        Same shape as input.

    References
    ----------
        `Dropout: A Simple Way to Prevent Neural Networks from Overfitting
        <http://www.cs.toronto.edu/~rsalakhu/papers/srivastava14a.pdf>`_
    """
    def __init__(self, rate, **kwargs):
        super(Dropout, self).__init__(**kwargs)
        self._rate = rate

    def hybrid_forward(self, F, x):
        return F.Dropout(x, p=self._rate, name='fwd')

    def __repr__(self):
        s = '{name}(p = {_rate})'
        return s.format(name=self.__class__.__name__,
                        **self.__dict__)


class BatchNorm(HybridBlock):
    """Batch normalization layer (Ioffe and Szegedy, 2014).
    Normalizes the input at each batch, i.e. applies a transformation
    that maintains the mean activation close to 0 and the activation
    standard deviation close to 1.

    Parameters
    ----------
    axis : int, default 1
        The axis that should be normalized. This is typically the channels
        (C) axis. For instance, after a `Conv2D` layer with `layout='NCHW'`,
        set `axis=1` in `BatchNorm`. If `layout='NHWC'`, then set `axis=3`.
    momentum: float, default 0.9
        Momentum for the moving average.
    epsilon: float, default 1e-5
        Small float added to variance to avoid dividing by zero.
    center: bool, default True
        If True, add offset of `beta` to normalized tensor.
        If False, `beta` is ignored.
    scale: bool, default True
        If True, multiply by `gamma`. If False, `gamma` is not used.
        When the next layer is linear (also e.g. `nn.relu`),
        this can be disabled since the scaling
        will be done by the next layer.
    beta_initializer: str or `Initializer`, default 'zeros'
        Initializer for the beta weight.
    gamma_initializer: str or `Initializer`, default 'ones'
        Initializer for the gamma weight.
    moving_mean_initializer: str or `Initializer`, default 'zeros'
        Initializer for the moving mean.
    moving_variance_initializer: str or `Initializer`, default 'ones'
        Initializer for the moving variance.
    in_channels : int, default 0
        Number of channels (feature maps) in input data. If not specified,
        initialization will be deferred to the first time `forward` is called
        and `in_channels` will be inferred from the shape of input data.


    Input shape:
        Arbitrary.

    Output shape:
        Same shape as input.
    """
    def __init__(self, axis=1, momentum=0.9, epsilon=1e-5, center=True, scale=True,
                 beta_initializer='zeros', gamma_initializer='ones',
                 running_mean_initializer='zeros', running_variance_initializer='ones',
                 in_channels=0, **kwargs):
        super(BatchNorm, self).__init__(**kwargs)
        self._kwargs = {'axis': axis, 'eps': epsilon, 'momentum': momentum,
                        'fix_gamma': not scale}
        if in_channels != 0:
            self.in_channels = in_channels

        self.gamma = self.params.get('gamma', grad_req='write' if scale else 'null',
                                     shape=(in_channels,), init=gamma_initializer,
                                     allow_deferred_init=True,
                                     differentiable=scale)
        self.beta = self.params.get('beta', grad_req='write' if center else 'null',
                                    shape=(in_channels,), init=beta_initializer,
                                    allow_deferred_init=True,
                                    differentiable=center)
        self.running_mean = self.params.get('running_mean', grad_req='null',
                                            shape=(in_channels,),
                                            init=running_mean_initializer,
                                            allow_deferred_init=True,
                                            differentiable=False)
        self.running_var = self.params.get('running_var', grad_req='null',
                                           shape=(in_channels,),
                                           init=running_variance_initializer,
                                           allow_deferred_init=True,
                                           differentiable=False)

    def hybrid_forward(self, F, x, gamma, beta, running_mean, running_var):
        return F.BatchNorm(x, gamma, beta, running_mean, running_var,
                           name='fwd', **self._kwargs)

    def __repr__(self):
        s = '{name}({content}'
        if hasattr(self, 'in_channels'):
            s += ', in_channels={0}'.format(self.in_channels)
        s += ')'
        return s.format(name=self.__class__.__name__,
                        content=', '.join(['='.join([k, v.__repr__()])
                                           for k, v in self._kwargs.items()]))


class LeakyReLU(HybridBlock):
    """Leaky version of a Rectified Linear Unit.

    It allows a small gradient when the unit is not active::

        `f(x) = alpha * x for x < 0`,
        `f(x) = x for x >= 0`.

    Parameters
    ----------
    alpha : float
        slope coefficient for the negative half axis. Must be >= 0.


    Input shape:
        Arbitrary.

    Output shape:
        Same shape as input.
    """
    def __init__(self, alpha, **kwargs):
        super(LeakyReLU, self).__init__(**kwargs)
        self._alpha = alpha

    def hybrid_forward(self, F, x):
        return F.LeakyReLU(x, act_type='leaky', slope=self._alpha, name='fwd')

    def __repr__(self):
        s = '{name}({alpha})'
        return s.format(name=self.__class__.__name__,
                        alpha=self._alpha)


class Embedding(HybridBlock):
    """Turns non-negative integers (indexes/tokens) into dense vectors
    of fixed size. eg. [[4], [20]] -> [[0.25, 0.1], [0.6, -0.2]]


    Parameters
    ----------
    input_dim : int
        Size of the vocabulary, i.e. maximum integer index + 1.
    output_dim : int
        Dimension of the dense embedding.
    dtype : str or np.dtype, default 'float32'
        Data type of output embeddings.
    weight_initializer : Initializer
        Initializer for the `embeddings` matrix.


    Input shape:
        2D tensor with shape: `(N, M)`.

    Output shape:
        3D tensor with shape: `(N, M, output_dim)`.
    """
    def __init__(self, input_dim, output_dim, dtype='float32',
                 weight_initializer=None, **kwargs):
        super(Embedding, self).__init__(**kwargs)
        self._kwargs = {'input_dim': input_dim, 'output_dim': output_dim,
                        'dtype': dtype}
        self.weight = self.params.get('weight', shape=(input_dim, output_dim),
                                      init=weight_initializer,
                                      allow_deferred_init=True)

    def hybrid_forward(self, F, x, weight):
        return F.Embedding(x, weight, name='fwd', **self._kwargs)

    def __repr__(self):
        s = '{block_name}({input_dim} -> {output_dim}, {dtype})'
        return s.format(block_name=self.__class__.__name__,
                        **self._kwargs)


class Flatten(HybridBlock):
    """Flattens the input to two dimensional.

    Input shape:
        Arbitrary shape `(N, a, b, c, ...)`

    Output shape:
        2D tensor with shape: `(N, a*b*c...)`
    """
    def __init__(self, **kwargs):
        super(Flatten, self).__init__(**kwargs)

    def hybrid_forward(self, F, x):
        return x.reshape((0, -1))

    def __repr__(self):
        return self.__class__.__name__


class InstanceNorm(HybridBlock):
    """
    Applies instance normalization to the n-dimensional input array.
    This operator takes an n-dimensional input array where (n>2) and normalizes 
    the input using the following formula:
    .. math::

    out = \frac{x - mean[data]}{ \sqrt{Var[data]} + \epsilon} * gamma + beta
    This layer is similar to batch normalization layer (`BatchNorm`)
    with two differences: first, the normalization is
    carried out per example (instance), not over a batch. Second, the
    same normalization is applied both at test and train time. This
    operation is also known as `contrast normalization`.
    If the input data is of shape [batch, channel, spacial_dim1, spacial_dim2, ...],
    `gamma` and `beta` parameters must be vectors of shape [channel].
    This implementation is based on paper:
    .. [1] Instance Normalization: The Missing Ingredient for Fast Stylization,
       D. Ulyanov, A. Vedaldi, V. Lempitsky, 2016 (arXiv:1607.08022v2).
    Examples::

      // Input of shape (2,1,2)
      x = mx.nd.array([[[ 1.1,  2.2]],
                      [[ 3.3,  4.4]]])
      // Instance normalization is calculated with the above formula
      layer = InstanceNorm()
      layer.initialize(ctx=mx.cpu(0))
      layer(x)
      [[[-0.99998355  0.99998331]]
       [[-0.99998319  0.99998361]]]
      <NDArray 2x1x2 @cpu(0)>
    """
    def __init__(self, axis=1, momentum=0.9, epsilon=1e-5, center=True, scale=False,
                 beta_initializer='zeros', gamma_initializer='ones',
                 in_channels=0, **kwargs):
        super(InstanceNorm, self).__init__(**kwargs)
        self._kwargs = {'eps': epsilon}
        if in_channels != 0:
            self.in_channels = in_channels
        self.gamma = self.params.get('gamma', grad_req='write' if scale else 'null',
                                     shape=(in_channels,), init=gamma_initializer,
                                     allow_deferred_init=True)
        self.beta = self.params.get('beta', grad_req='write' if center else 'null',
                                    shape=(in_channels,), init=beta_initializer,
                                    allow_deferred_init=True)

    def hybrid_forward(self, F, x, gamma, beta):
        return F.InstanceNorm(x, gamma, beta,
                           name='fwd', **self._kwargs)

    def __repr__(self):
        s = '{name}({content}'
        if hasattr(self, 'in_channels'):
            s += ', in_channels={0}'.format(self.in_channels)
        s += ')'
        return s.format(name=self.__class__.__name__,
                        content=', '.join(['='.join([k, v.__repr__()])
                                           for k, v in self._kwargs.items()]))


class ReflectionPad(HybridBlock):
    """Pads the input tensor using the reflection of the input boundary.

    Args:
        pad_width int: the size of the padding. If is int, uses the same
            padding in all boundaries. 

    Shape:
        - Input: :math:`(N, C, H_{in}, W_{in})`
        - Output: :math:`(N, C, H_{out}, W_{out})` where
          :math:`H_{out} = H_{in} + paddingTop + paddingBottom`
          :math:`W_{out} = W_{in} + paddingLeft + paddingRight`

    Examples::

        m = nn.ReflectionPad(3)
        input = mx.nd.random_normal(shape=(16, 3, 224, 224))
        output = m(input)
    """
    def __init__(self, pad_width=None, **kwargs):
        super(ReflectionPad(, self).__init__(**kwargs)
        self.pad_width = pad_width
        
    def forward(self, x):
        return F.pad(x, mode='reflect', pad_width=self.pad_width)


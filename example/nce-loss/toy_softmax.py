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

# pylint: disable=invalid-name, missing-docstring, too-many-arguments
from __future__ import print_function

from collections import namedtuple
import logging
import random

import mxnet as mx
import numpy as np

ToyModel = namedtuple("ToyModel", ["ex", "symbol", "param_blocks"])


def get_net(vocab_size_):
    data = mx.sym.Variable('data')
    label = mx.sym.Variable('label')
    pred = mx.sym.FullyConnected(data=data, num_hidden=100)
    pred = mx.sym.FullyConnected(data=pred, num_hidden=vocab_size_)
    sm = mx.sym.SoftmaxOutput(data=pred, label=label)
    return sm


class SimpleBatch(object):
    def __init__(self, data_names, data, label_names, label):
        self.data = data
        self.label = label
        self.data_names = data_names
        self.label_names = label_names

    @property
    def provide_data(self):
        return [(n, x.shape) for n, x in zip(self.data_names, self.data)]

    @property
    def provide_label(self):
        return [(n, x.shape) for n, x in zip(self.label_names, self.label)]


class DataIter(mx.io.DataIter):
    def __init__(self, count, batch_size_, vocab_size_, num_label_, feature_size_):
        super(DataIter, self).__init__()
        self.batch_size = batch_size_
        self.count = count
        self.vocab_size = vocab_size_
        self.num_label = num_label_
        self.feature_size = feature_size_
        self.provide_data = [('data', (batch_size_, feature_size_))]
        self.provide_label = [('label', (self.batch_size,))]

    def mock_sample(self):
        ret = np.zeros(self.feature_size)
        rn = set()
        while len(rn) < 3:
            rn.add(random.randint(0, self.feature_size - 1))
        s = 0
        for k in rn:
            ret[k] = 1.0
            s *= self.feature_size
            s += k
        return ret, s % self.vocab_size

    def __iter__(self):
        for _ in range(self.count // self.batch_size):
            data = []
            label = []
            for _ in range(self.batch_size):
                d, l = self.mock_sample()
                data.append(d)
                label.append(l)
            data_all = [mx.nd.array(data)]
            label_all = [mx.nd.array(label)]
            data_names = ['data']
            label_names = ['label']
            yield SimpleBatch(data_names, data_all, label_names, label_all)

    def reset(self):
        pass


if __name__ == '__main__':
    head = '%(asctime)-15s %(message)s'
    logging.basicConfig(level=logging.DEBUG, format=head)

    batch_size = 128
    vocab_size = 10000
    feature_size = 100
    num_label = 6

    data_train = DataIter(100000, batch_size, vocab_size, num_label, feature_size)
    data_test = DataIter(1000, batch_size, vocab_size, num_label, feature_size)

    network = get_net(vocab_size)

    model = mx.mod.Module(
        symbol=network,
        data_names=[x[0] for x in data_train.provide_data],
        label_names=[y[0] for y in data_train.provide_label],
        context=[mx.cpu()]
    )

    model.fit(
        train_data=data_train,
        eval_data=data_test,
        num_epoch=20,
        optimizer='sgd',
        optimizer_params={'learning_rate': 0.03, 'momentum': 0.9, 'wd': 0.0000},
        initializer=mx.init.Xavier(factor_type='in', magnitude=2.34),
        batch_end_callback=mx.callback.Speedometer(batch_size, 50)
    )

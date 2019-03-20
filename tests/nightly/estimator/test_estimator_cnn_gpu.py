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

# Test gluon estimator on GPU using ResNet18

import os
import sys
import mxnet as mx
from mxnet import gluon
from mxnet.gluon import data
from mxnet.gluon.estimator import estimator, event_handler
from mxnet.gluon.model_zoo import vision

def load_data_mnist(batch_size, resize=None, num_workers=None):
    '''
    Load MNIST dataset
    '''
    transformer = []
    if resize:
        transformer += [data.vision.transforms.Resize(resize)]
    transformer += [data.vision.transforms.ToTensor()]
    transformer = data.vision.transforms.Compose(transformer)
    mnist_train = data.vision.MNIST(train=True)
    mnist_test = data.vision.MNIST(train=False)

    if num_workers is None:
        num_workers = 0 if sys.platform.startswith('win32') else 4

    train_iter = data.DataLoader(
        mnist_train.transform_first(transformer), batch_size, shuffle=True,
        num_workers=num_workers)
    test_iter = data.DataLoader(
        mnist_test.transform_first(transformer), batch_size, shuffle=False,
        num_workers=num_workers)
    return train_iter, test_iter

def test_estimator():
    '''
    Test estimator by training resnet18_v1 for 5 epochs on MNIST and verify accuracy
    '''
    model_name = 'resnet18_v1'
    batch_size = 128
    num_epochs = 5
    input_size = 224
    lr = 0.001
    # Set context
    if mx.context.num_gpus() > 0:
        context = mx.gpu(0)
    else:
        context = mx.cpu()
    # Get model
    net = vision.get_model(model_name, classes=10)
    # Load train and validation data
    train_data, test_data = load_data_mnist(batch_size, resize=input_size)
    # Define loss and evaluation metrics
    loss = gluon.loss.SoftmaxCrossEntropyLoss()
    acc = mx.metric.Accuracy()
    # Hybridize and initialize net
    net.hybridize()
    net.initialize(mx.init.Xavier(), ctx=context)
    # Define trainer
    trainer = gluon.Trainer(net.collect_params(), 'sgd', {'learning_rate': lr})
    # Define estimator
    est = estimator.Estimator(net=net,
                              loss=loss,
                              metrics=acc,
                              trainers=trainer,
                              context=context)
    # Call fit() to begin training
    est.fit(train_data=train_data,
            val_data=test_data,
            epochs=num_epochs,
            batch_size=batch_size)

    assert est.train_stats['train_'+acc.name][num_epochs-1] > 0.75

if __name__ == '__main__':
    test_estimator()

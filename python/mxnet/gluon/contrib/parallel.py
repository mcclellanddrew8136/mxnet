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

# pylint: disable=broad-except, redefined-builtin
"""Synchronized DataParallel"""
import threading
from ... import autograd
from ...ndarray import NDArray
from ..utils import split_and_load

__all__ = ['DataParallel', 'Barrier', 'parallel_apply', 'split_kwargs']


class Barrier(object):
    """Shared NDArray for cross device operation.

    A cross device operation that allows synchronized push and pull. It can be used in
    Cross-gpu Sycnhronized Batch Normalization and Sparse Blocks.

    Parameters
    ----------
    counter : int
        Number of deivces.
    operation : callable
        The cross device operation is applying (e.g. AllReduce).
    """
    def __init__(self, counter, operation):
        self.mutex = threading.Lock()
        self.all_tasks_done = threading.Condition(self.mutex)
        self.counter = counter
        self.op = operation
        self._clear()

    def push(self, x):
        """Push a NDArray from one of the device.
        Input:
            x (NDArray)

        Output:
            idx (int), the output index
        """
        with self.mutex:
            if self.push_tasks == 0:
                self._clear()
            self.list.append(x)
            idx = len(self.list) - 1
            self.push_tasks -= 1

        with self.all_tasks_done:
            if self.push_tasks == 0:
                self.all_tasks_done.notify_all()
            while self.push_tasks:
                self.all_tasks_done.wait()

        self._sync_op()
        return idx

    def pull(self, idx):
        """Pull the output to each device
        Input:
            idx (int)

        Output:
            out (NDArray)
        """
        return self.out[idx]

    def _sync_op(self):
        with self.mutex:
            if self.reduce_tasks == 1:
                assert(len(self.list) == self.counter)
                self.out = self.op(*self.list)
                if isinstance(self.out, (list, tuple)):
                    for xi in self.out:
                        xi.wait_to_read()
                else:
                    self.out.wait_to_read()
                self.reduce_tasks -= 1
            else:
                self.reduce_tasks -= 1

        with self.all_tasks_done:
            if self.reduce_tasks == 0:
                self.all_tasks_done.notify_all()
            while self.reduce_tasks:
                self.all_tasks_done.wait()

    def _clear(self):
        self.list = []
        self.push_tasks = self.counter
        self.reduce_tasks = self.counter

    def __len__(self):
        return len(self.list)

    def __repr__(self):
        return 'ParallelState'


class DataParallel(object):
    """Data parallelism

    Hide the difference of single/multiple GPUs to the user.
    Inputs and outputs are both list of NDArrays in different contexts.
    In the forward pass, the module is replicated on each device,
    and each replica handles a portion of the input. During the backwards
    pass, gradients from each replica are summed into the original module.

    Parameters
    ----------
    module : object
        Network to be parallelized.
    ctx_list : list
        A list of contexts
    sync : bool
        enable synchronization (default: False).


    Inputs:
        - **inputs**: list of input (NDArrays)

    Outputs:
        - **outputs**: list of output (NDArrays)


    Example::
        >>> ctx = [mx.gpu(0), mx.gpu(1)]
        >>> net = DataParallel(model, ctx_list=ctx)
        >>> y = net(x)
    """
    def __init__(self, module, ctx_list=None, sync=False):
        module.collect_params().reset_ctx(ctx=ctx_list)
        self.ctx_list = ctx_list
        self.module = module
        self.sync = sync

    def __call__(self, *inputs, **kwargs):
        if not self.ctx_list:
            return self.module(*inputs, **kwargs)
        inputs, kwargs = split_kwargs(inputs, kwargs, self.ctx_list)
        assert(len(inputs) == len(self.ctx_list))
        if len(self.ctx_list) == 1:
            return self.module(*inputs[0], **kwargs[0])
        return parallel_apply(self.module, inputs, kwargs, self.sync)

    def __repr__(self):
        return 'DataParallel:\n module = {' + self.module.__repr__() + '}'


def split_kwargs(inputs, kwargs, ctx_list, batch_axis=0):
    r"""Split with support for kwargs dictionary"""
    def split_map(obj):
        if isinstance(obj, NDArray):
            return split_and_load(obj, ctx_list, batch_axis)
        if isinstance(obj, tuple) and len(obj) > 0:
            return list(zip(*map(split_map, obj)))
        if isinstance(obj, list) and len(obj) > 0:
            return list(map(list, zip(*map(split_map, obj))))
        if isinstance(obj, dict) and len(obj) > 0:
            return list(map(type(obj), zip(*map(split_map, obj.items()))))
        return [obj for targets in ctx_list]
    inputs = split_map(inputs) if inputs else []
    kwargs = split_map(kwargs) if kwargs else []
    if len(inputs) < len(kwargs):
        inputs.extend([() for _ in range(len(kwargs) - len(inputs))])
    elif len(kwargs) < len(inputs):
        kwargs.extend([{} for _ in range(len(inputs) - len(kwargs))])
    inputs = tuple(inputs)
    kwargs = tuple(kwargs)
    return inputs, kwargs


def parallel_apply(module, inputs, kwargs_tup=None, sync=False):
    """Parallel applying model forward"""
    if kwargs_tup is not None:
        assert len(inputs) == len(kwargs_tup)
    else:
        kwargs_tup = ({},) * len(inputs)

    lock = threading.Lock()
    results = {}

    def _worker(i, module, input, kwargs, results, is_training, lock):
        try:
            if is_recording:
                with autograd.record(is_training):
                    output = tuple([module(*input, **kwargs)])
                    for out in output:
                        out.wait_to_read()
            else:
                output = module(*input, **kwargs)
                output.wait_to_read()
            with lock:
                results[i] = output
        except Exception as e:
            with lock:
                results[i] = e

    is_training = autograd.is_training()
    threads = [threading.Thread(target=_worker,
                                args=(i, module, input, kwargs, results,
                                      is_training, lock),
                               )
               for i, (input, kwargs) in
               enumerate(zip(inputs, kwargs_tup))]

    if sync:
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        outputs = []
        for i in range(len(inputs)):
            output = results[i]
            if isinstance(output, Exception):
                raise output
            outputs.append(output)
        return outputs
    else:
        outputs = [module(*input, **kwargs) for (input, kwargs) in zip(inputs, kwargs_tup)]
        return outputs

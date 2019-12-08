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
""" Key value store interface of MXNet for parameter synchronization."""
from __future__ import absolute_import

from array import array
import ctypes
import pickle
from ..ndarray import NDArray
from ..ndarray import _ndarray_cls
from ..base import _LIB, c_str_array, c_handle_array, c_array, c_array_buf, c_str
from ..base import check_call, string_types, mx_uint, py_str
from ..base import NDArrayHandle, KVStoreHandle
from .. import optimizer as opt
from ..profiler import set_kvstore_handle

__all__ = ['create', 'KVStoreBase']

def _ctype_key_value(keys, vals):
    """Returns ctype arrays for the key-value args, and the whether string keys are used.
    For internal use only.
    """
    if isinstance(keys, (tuple, list)):
        assert(len(keys) == len(vals))
        c_keys = []
        c_vals = []
        use_str_keys = None
        for key, val in zip(keys, vals):
            c_key_i, c_val_i, str_keys_i = _ctype_key_value(key, val)
            c_keys += c_key_i
            c_vals += c_val_i
            use_str_keys = str_keys_i if use_str_keys is None else use_str_keys
            assert(use_str_keys == str_keys_i), "inconsistent types of keys detected."
        c_keys_arr = c_array(ctypes.c_char_p, c_keys) if use_str_keys \
                     else c_array(ctypes.c_int, c_keys)
        c_vals_arr = c_array(ctypes.c_void_p, c_vals)
        return (c_keys_arr, c_vals_arr, use_str_keys)

    assert(isinstance(keys, (int,) + string_types)), \
           "unexpected type for keys: " + str(type(keys))
    use_str_keys = isinstance(keys, string_types)
    if isinstance(vals, NDArray):
        c_keys = c_str_array([keys]) if use_str_keys \
                 else c_array_buf(ctypes.c_int, array('i', [keys]))
        return (c_keys, c_handle_array([vals]), use_str_keys)
    else:
        for value in vals:
            assert(isinstance(value, NDArray))
        c_keys = c_str_array([keys] * len(vals)) if use_str_keys \
                 else c_array_buf(ctypes.c_int, array('i', [keys] * len(vals)))
        return (c_keys, c_handle_array(vals), use_str_keys)

def _ctype_dict(param_dict):
    """Returns ctype arrays for keys and values(converted to strings) in a dictionary"""
    assert(isinstance(param_dict, dict)), \
        "unexpected type for param_dict: " + str(type(param_dict))
    c_keys = c_array(ctypes.c_char_p, [c_str(k) for k in param_dict.keys()])
    c_vals = c_array(ctypes.c_char_p, [c_str(str(v)) for v in param_dict.values()])
    return (c_keys, c_vals)

class KVStoreBase(object):
    """An abstract key-value store interface for data parallel training."""

    def broadcast(self, key, value, out):
        """ Broadcast the `value` NDArray at rank 0 to all ranks,
        and store the result in `out`

        Parameters
        ----------
        key : str or int
            The key.

        value : NDArray
            The value corresponding to the key to broadcast

        out : NDArray, or list of NDArray
            Values corresponding to the key to store the result

        priority : int, optional
            The priority of the operation.
            Higher priority operations are likely to be executed before other actions.
        """
        raise NotImplementedError()

    def pushpull(self, key, value, out=None, priority=0):
        """ Performs push and pull a single value or a sequence of values from the store.

        This function is coalesced form of push and pull operations.

        `value` is pushed to the kvstore server for summation with the specified keys,
        and the results are pulled from the server to `out`. If `out` is not specified
        the pulled values are written to `value`.

        Parameters
        ----------
        key : str or int
            The key.

        value : NDArray, or list of NDArray
            Values corresponding to the keys.

        out: NDArray, or list of NDArray
            Values corresponding to the key.

        priority : int, optional
            The priority of the operation.
            Higher priority operations are likely to be executed before other actions.
        """
        raise NotImplementedError()

    def set_optimizer(self, optimizer):
        """ Registers an optimizer with the kvstore.

        When using a single machine, this function updates the local optimizer.
        If using multiple machines and this operation is invoked from a worker node,
        it will serialized the optimizer with pickle and send it to all servers.
        The function returns after all servers have been updated.

        Parameters
        ----------
        optimizer : KVStoreBase
            The new optimizer for the store
        """
        raise NotImplementedError()

    OPTIMIZER = 'optimizer'

    @staticmethod
    def query_capability(capability):
        """Queries if the KVStore type supports certain capability, such as optimizer algorithm,
        gradient compression, sparsity, etc.

        Parameters
        ----------
        capability: str
            The capability to query

        Returns
        -------
        result : bool
            Whether the capability is supported or not.
        """
        raise NotImplementedError()

    def save_optimizer_states(self, fname, dump_optimizer=False):
        """Saves the optimizer (updater) state to a file. This is often used when checkpointing
        the model during training.

        Parameters
        ----------
        fname : str
            Path to the output states file.
        dump_optimizer : bool, default False
            Whether to also save the optimizer itself. This would also save optimizer
            information such as learning rate and weight decay schedules.
        """
        raise NotImplementedError()

    def load_optimizer_states(self, fname):
        """Loads the optimizer (updater) state from the file.

        Parameters
        ----------
        fname : str
            Path to input states file.
        """
        raise NotImplementedError()

    @property
    def type(self):
        """ Returns the type of this kvstore.

        Returns
        -------
        type : str
            the string type
        """
        raise NotImplementedError()

    @property
    def rank(self):
        """ Returns the rank of this worker node.

        Returns
        -------
        rank : int
            The rank of this node, which is in range [0, num_workers())
        """
        raise NotImplementedError()

    @property
    def num_workers(self):
        """Returns the number of worker nodes.

        Returns
        -------
        size :int
            The number of worker nodes.
        """
        raise NotImplementedError()

    kv_registry = {}

    @staticmethod
    def register(klass):
        """Registers a new KVStore.
        Once a kvstore is registered, we can create an instance of this
        kvstore with `create` later.

        Examples
        --------
        >>> @mx.kvstore.KVStoreBase.register
        ... class MyKVStore(mx.kvstore.KVStoreBase):
        ...     pass
        >>> kv = mx.kv.create('MyKVStore')
        >>> print(type(kv))
        <class '__main__.MyKVStore'>
        """
        assert(isinstance(klass, type))
        name = klass.__name__.lower()
        if name in KVStoreBase.kv_registry:
            warnings.warn('WARNING: New kvstore %s.%s is overriding '
                          'existing kvstore %s.%s' %
                          (klass.__module__, klass.__name__,
                           KVStoreBase.kv_registry[name].__module__,
                           KVStoreBase.kv_registry[name].__name__))
        KVStoreBase.kv_registry[name] = klass
        return klass

def create(name='local'):
    """Creates a new KVStore.

    For single machine training, there are two commonly used types:

    ``local``: Copies all gradients to CPU memory and updates weights there.

    ``device``: Aggregates gradients and updates weights on GPUs. With this setting,
    the KVStore also attempts to use GPU peer-to-peer communication,
    potentially accelerating the communication.

    For distributed training, KVStore also supports a number of types:

    ``dist_sync``: Behaves similarly to ``local`` but with one major difference.
    With ``dist_sync``, batch-size now means the batch size used on each machine.
    So if there are ``n`` machines and we use batch size ``b``,
    then ``dist_sync`` behaves like ``local`` with batch size ``n * b``.

    ``dist_device_sync``: Identical to ``dist_sync`` with the difference similar
    to ``device`` vs ``local``.

    ``dist_async``: Performs asynchronous updates.
    The weights are updated whenever gradients are received from any machine.
    No two updates happen on the same weight at the same time. However, the order is not
    guaranteed.

    Parameters
    ----------
    name : {'local', 'device', 'nccl', 'dist_sync', 'dist_device_sync', 'dist_async', 'horovod'}
        The type of KVStore.
    Returns
    -------
    kv : KVStoreBase
        The created KVStore.
    """
    if not isinstance(name, string_types):
        raise TypeError('name must be a string')
    name = name.lower()
    # first lookup the registry
    if name in KVStoreBase.kv_registry:
        return KVStoreBase.kv_registry[name]()
    else:
        # fall back to the native kvstore implementation
        handle = KVStoreHandle()
        check_call(_LIB.MXKVStoreCreate(c_str(name),
                                        ctypes.byref(handle)))
        from .kvstore import KVStore
        kv = KVStore(handle)
        set_kvstore_handle(kv.handle)
        return kv

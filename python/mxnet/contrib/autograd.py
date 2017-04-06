# coding: utf-8
"""Autograd for NDArray."""
from __future__ import absolute_import
from __future__ import division

import ctypes
import functools
from ..base import _LIB, check_call
from ..base import mx_uint, NDArrayHandle, c_array
from ..ndarray import NDArray

def set_recording(recording):
    """Turn on or turn of operator recording.

    Parameters
    ----------
    recording: bool
    """
    check_call(_LIB.MXAutogradSetRecording(
        ctypes.c_int(recording)))

def mark_variables(variables):
    """Mark NDArrays as variables to compute gradient for autograd.

    Parameters
    ----------
    variables: list of NDArray
    """
    variable_handles = []
    for var in variables:
        variable_handles.append(var.handle)
    check_call(_LIB.MXAutogradMarkVariables(
        len(variable_handles),
        c_array(NDArrayHandle, variable_handles)))

def compute_gradient(outputs):
    """Compute the gradients of outputs w.r.t variables.

    Parameters
    ----------
    outputs: list of NDArray

    Returns
    -------
    gradients: list of NDArray
    """
    output_handles = []
    for arr in outputs:
        output_handles.append(arr.handle)

    num_grad = mx_uint()
    grad_handles = ctypes.POINTER(NDArrayHandle)()
    check_call(_LIB.MXAutogradComputeGradient(
        len(output_handles),
        c_array(NDArrayHandle, output_handles),
        ctypes.byref(num_grad),
        ctypes.byref(grad_handles)))
    return [NDArray(NDArrayHandle(grad_handles[i])) for i in range(num_grad.value)]

def grad_and_loss(func):
    """Return function that computes both gradient of arguments and loss value.

    Parameters
    ----------
    func: a python function
        The forward (loss) function.

    Returns
    -------
    grad_and_loss_func: a python function
        A function that would compute both the gradient of arguments and loss value.
    """
    @functools.wraps(func)
    def wrapped(*args):
        """Wrapped function."""
        for x in args:
            assert isinstance(x, NDArray), "type of autograd input should NDArray."
        mark_variables(args)
        set_recording(True)
        outputs = func(*args)
        set_recording(False)
        grad_vals = compute_gradient(
            outputs if isinstance(outputs, list) else [outputs])
        return grad_vals, outputs
    return wrapped

def grad(func):
    """Return function that computes gradient of arguments.

    Parameters
    ----------
    func: a python function
        The forward (loss) function.

    Returns
    -------
    grad_func: a python function
        A function that would compute the gradient of arguments.

    Examples
    --------
    >>> # autograd supports dynamic graph which is changed
    >>> # every instance
    >>> def func(x):
    >>>     r = random.randint(0, 1)
    >>>     if r % 2:
    >>>         return x**2
    >>>     else:
    >>>         return x/3
    >>> # use `grad(func)` to get the gradient function
    >>> for x in range(10):
    >>>     grad_func = grad(func)
    >>>     inputs = nd.array([[1, 2, 3], [4, 5, 6]])
    >>>     grad_vals = grad_func(inputs)
    """
    grad_with_loss_func = grad_and_loss(func)
    @functools.wraps(grad_with_loss_func)
    def wrapped(*args):
        return grad_with_loss_func(*args)[0]
    return wrapped

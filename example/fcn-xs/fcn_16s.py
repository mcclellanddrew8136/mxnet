# pylint: skip-file
import sys, os
import argparse
import mxnet as mx
import numpy as np
import logging
import symbol_fcnxs
import init_fcnxs
from data import FileIter
from solver import Solver

logger = logging.getLogger()
logger.setLevel(logging.INFO)
np.set_printoptions(threshold=np.nan)

img_dir = "./VOC2012"
train_lst = "train.lst"
val_lst = "val.lst"
fcn16s_model_prefix = "model_pascal/FCN16s_VGG16"
batch_size = 1
workspace = 1536
ctx = mx.gpu(0)
# ctx = mx.cpu()

def norm_stat(d):
    return mx.nd.norm(d)/np.sqrt(d.size)

def main():
    fcn16s = symbol_fcnxs.get_fcn16s_symbol(21, workspace)
    arg_names = fcn16s.list_arguments()
    print "arg_names=", arg_names
    arg_shapes, out_shapes, _ = fcn16s.infer_shape(data=(1,3,336,500))
    print "out_shapes[0]=", out_shapes[0]
    arg_shapes_dict = dict(zip(arg_names, arg_shapes))
    _, vgg16fc_arg_params, vgg16fc_aux_params = mx.model.load_checkpoint(args.prefix, args.epoch)
    # fcn16s_arg_params, fcn16s_aux_params = init_fcnxs.init_fcn16s_params(ctx, fcn16s, vgg16fc_arg_params, vgg16fc_aux_params)
    fcn16s_arg_params, fcn16s_aux_params = init_fcnxs.init_fcn16s_params_from_fcn32s(ctx, fcn16s, vgg16fc_arg_params, vgg16fc_aux_params)
    train_dataiter = FileIter(img_dir, train_lst)
    val_dataiter = FileIter(img_dir, val_lst)
    mon = mx.mon.Monitor(1, norm_stat)
    model = Solver(
        ctx                 = ctx,
        symbol              = fcn16s,
        begin_epoch         = 0,
        num_epoch           = 100,
        arg_params          = fcn16s_arg_params,
        aux_params          = fcn16s_aux_params,
        learning_rate       = 1e-12,
        momentum            = 0.99,
        wd                  = 0.0005,
        snapshot            = 1,
        monitor             = None)
    model.fit(
        train_data          = train_dataiter,
        eval_data           = val_dataiter,
        batch_end_callback  = mx.callback.Speedometer(batch_size, 50),
        epoch_end_callback  = mx.callback.do_checkpoint(fcn16s_model_prefix))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='training pascal voc segmentation using fcn-16s.')
    parser.add_argument('prefix', default='FCN16s_VGG16',
        help='The prefix(include path) of vgg16 model with mxnet format.')
    parser.add_argument('epoch', type=int, default=199,
        help='The epoch number of fcn16s model.')
    args = parser.parse_args()
    main()

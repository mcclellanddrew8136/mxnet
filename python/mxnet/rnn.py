from collections import namedtuple, deque
from copy import copy

from . import symbol

from .ndarray import NDArray, zeros

# TODO: those are to be stored in the symbols like attributes
# connecting output delayed by t steps to input
RecurrentRule = namedtuple('RecurrentRule', ['output', 'input', 't'])

def SimpleRecurrece(input_var, name, num_hidden, act_type='relu'):
    recurrent_name = name + '_recurrent'
    recurrent_var  = symbol.Variable(recurrent_name)
    recurrent_hid  = symbol.FullyConnected(data=recurrent_var, name=name+'_rhid', num_hidden=num_hidden)
    input_hid      = symbol.FullyConnected(data=input_var, name=name+'_ihid', num_hidden=num_hidden, no_bias=True)
    output         = symbol.Activation(data=input_hid+recurrent_hid, name=name, act_type=act_type)
    output_name    = name + '_output'

    return (output, RecurrentRule(output=output_name, input=recurrent_name, t=1))


class Sequencer(object):
    def __init__(self, sym, rules, ctx):
        self.orig_sym = sym
        self.rules    = rules
        self.sym      = self.extract_states(sym, rules)
        self.ctx      = ctx

    def infer_shape(self, **data_shapes):
        arg_shapes, out_shapes, aux_shapes = self.sym.infer_shape_partial(**data_shapes)
        out_names = self.sym.list_outputs()
        for rule in self.rules:
            data_shapes[rule.input] = out_shapes[out_names.index(rule.output)]
        return self.sym.infer_shape(**data_shapes)

    @property
    def state_names(self):
        return [x.input for x in self.rules]

    @property
    def max_delay(self):
        return max([x.t for x in self.rules])

    def _get_param_names(self, input_names):
        arg_names   = self.sym.list_arguments()
        param_names = list(set(arg_names) - set(input_names) - set(self.state_names))
        return param_names

    def _init_params(self, input_shapes, overwrite=False):
        arg_shapes, out_shapes, aux_shapes = self.infer_shape(**input_shapes)

        arg_names   = self.sym.list_arguments()
        input_names = input_shapes.keys()
        aux_names   = self.sym.list_auxiliary_states()
        param_names = self._get_param_names(input_names)

        param_name_shapes = [x for x in zip(arg_names, arg_shapes) if x[0] in param_names]
        arg_params = {k: nd.zeros(s) for k, s in param_name_shapes}
        aux_params = {k: nd.zeros(s) for k, s in zip(aux_names, aux_shapes)}

        for k, v in arg_params.items():
            if self.arg_params and k in self.arg_params and (not overwrite):
                arg_params[k][:] = self.arg_params[k]
            else:
                self.initializer(k, v)

        for k, v in aux_params.items():
            if self.aux_params and k in self.aux_params and (not overwrite):
                aux_params[k][:] = self.aux_params[k]
            else:
                self.initializer(k, v)

        self.arg_params = arg_params
        self.aux_params = aux_params
        return (arg_names, param_names, aux_names)

    def bind_executor(self, ctx, need_grad=False, **input_shapes):
        arg_shapes, out_shapes, aux_shapes = self.infer_shape(**input_shapes)
        arg_names = self.sym.list_arguments()
        param_names = self._get_param_names(input_shapes.keys())
        state_names = self.state_names

        arg_arrays = [zeros(s) for s in arg_shapes]
        aux_arrays = [zeros(s) for s in aux_shapes]
        grad_req   = ['null' for x in arg_arrays]
        for i in range(len(arg_names)):
            if arg_names[i] in param_names:
                grad_req[i] = 'add'
            elif arg_names[i] in state_names:
                grad_req[i] = 'write'

        grad_arrays = {arg_names[i]: zeros(arg_shapes[i]) \
                for i in range(len(arg_names)) if grad_req[i] != 'null'}

        return self.sym.bind(ctx, arg_ndarrays, grad_ndarrays, grad_req, aux_ndarrays)


    def fit(self, data, begin_epoch=0, end_epoch=1):
        input_shapes = dict(data.provide_data+data.provide_label)
        arg_names, param_names, aux_names = self._init_params(input_shapes)
        state_names = self.state_names
        data_names = [x[0] for x in data.provide_data]
        label_names = [x[0] for x in data.provide_label]
        output_names = self.sym.list_outputs()

        param_idx = [i for i in range(len(arg_names)) if arg_names[i] in param_names]
        state_idx = [i for i in range(len(arg_names)) if arg_names[i] in state_names]

        n_loss = len(self.orig_sym.list_outputs())
        n_state = len(self.sym.list_outputs()) - n_loss
        out_state_idx = range(n_loss, n_state+n_loss)

        exec_train = self.bind_executor(self.ctx, need_grad=True, **input_shapes)

        param_arrays = [exec_train.arg_arrays[i] for i in param_idx]
        state_arrays = [exec_train.arg_arrays[i] for i in state_idx]
        data_arrays  = [exec_train.arg_dict[name] for name in data_names]
        label_arrays = [exec_train.arg_dict[name] for name in label_names]

        param_grads  = [exec_train.grad_arrays[i] for i in param_idx]
        state_grads  = [exec_train.grad_arrays[i] for i in state_idx]

        out_state_grads = [exec_train.outputs[i].copyto(exec_train.outputs[i].context) \
                for i in out_state_idx]

        idx_rules = []
        for rule in self.rules:
            i_input = state_names.index(rule.input)
            i_output = output_names.index(rule.output)
            idx_rules.append(RecurrentRule(output=i_output, input=i_input, t=rule.t))

        for epoch in range(begin_epoch, end_epoch):
            train_data.reset()
            for batch in data:
                seq_len = batch.sequence_length
                fwd_states = deque(maxlen=self.max_delay)

                # forward pass through time
                for t in range(seq_len):
                    # assuming forward stage labels are not needed
                    self.load_data(batch.data_at(t), data_arrays)

                    # copy states over
                    for rule in idx_rules:
                        if t >= rule.t:
                            fwd_states[-rule.t][rule.output].copyto(state_arrays[rule.input])
                        else:
                            state_arrays[rule.input][:] = 0

                    # forward 1 step
                    exec_train.forward(is_train=True)

                    # save states
                    fwd_states.append([x.copyto(x.context) for x in exec_train.outputs])

                fwd_states.clear()
                bwd_states = deque(maxlen=self.max_delay)

                # backward pass through time
                for grad in param_grads:
                    grad[:] = 0

                for t in range(seq_len):
                    # load data and label
                    self.load_data(batch.data_at(seq_len-t-1), data_arrays)
                    self.load_data(batch.label_at(seq_len-t-1), label_arrays)

                    # copy state grads over
                    for rule in idx_rules:
                        if t >= rule.t:
                            bwd_states[-rule.t][rule.input].copyto(out_state_grads[rule.output-n_loss])
                        else:
                            out_state_grads[rule.output-n_loss][:] = 0

                    exec_train.backward(out_state_grads)

                    # save state gradients
                    bwd_states.append([x.copyto(x.context) for x in state_grads])

                bwd_states.clear()

                # update parameters









    @staticmethod
    def load_data(src, dst):
        for d_src, d_dst in zip(src, dst):
            d_src.copyto(d_dst)


    @staticmethod
    def extract_states(sym, rules):
        outputs = sym.list_outputs()
        states  = [x.output for x in rules]
        sym_all = sym.get_internals()
        sym_grp = symbol.Group([sym_all[x] for x in outputs+states])
        return sym_grp


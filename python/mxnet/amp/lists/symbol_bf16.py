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
"""Lists of functions whitelisted/blacklisted for automatic mixed precision in symbol API."""

from ...runtime import Features

# Functions that should be cast to lower precision
BF16_FUNCS = [
    'Convolution',
    'Deconvolution',
    'FullyConnected'
]
if Features.instance.is_enabled('ONEDNN'):
    BF16_FUNCS.extend([
        '_sg_onednn_conv',
        '_sg_onednn_fully_connected',
        '_sg_onednn_selfatt_qk',
        '_sg_onednn_selfatt_valatt'
    ])


# Functions that should not be casted, either because
# they are irrelevant (not used in the network itself
# like image transformations or optimizers) or they
# are dtype neutral (can work in both bf16 and fp32)
BF16_FP32_FUNCS = [
    '_contrib_AdaptiveAvgPooling2D',
    '_contrib_BatchNormWithReLU',
    'Activation',
    'BatchNorm',
    'LayerNorm',
    'LRN',
    'softmax',
    'log_softmax',
    'masked_softmax',
    'Pooling',
    '_npi_mean',
    '_npi_sum',
    '_npi_square',
    '_npi_sqrt',
    '_npi_exp',
    '_npi_tanh',
    '_npi_transpose',
    '_npx_reshape',
    '_npi_where',
    #'_contrib_quantize_asym', # used in rnn, which is hard to convert to bf16
    '_contrib_quantize_v2',
    #'_contrib_quantize', # not used anymore
    'sum',
    'mean',
    '_copy',
    'Reshape',
    'Flatten',
    'transpose',
    'expand_dims',
    'slice',
    'stack',
    'space_to_depth',
    '_split_v2',

    # no oneDNN support:
    'Cast',
    'where',
    'take',
]
# 'RNN', # GetEnv("MXNET_USE_ONEDNN_RNN", 1)

# Functions with multiple inputs, that need the same
# type of all their inputs
WIDEST_TYPE_CASTS = [
    'Concat',
    'dot',
    'batch_dot',
    'broadcast_add',
    'broadcast_sub',
    'broadcast_mul',
    'broadcast_div',
    'elemwise_add',
    'add_n',
    '_npi_dot',
    '_npi_add',
    '_npi_multiply',
    '_npi_subtract',
    '_npi_true_divide',
]
if Features.instance.is_enabled('ONEDNN'):
    WIDEST_TYPE_CASTS.extend([
        '_sg_onednn_batch_dot',
    ])

# Functions that when running with Bfloat16, the params that still need float32.
BF16_USE_FP32_PARAMS = {
    'BatchNormWithReLU': ["", "gamma", "beta", "moving_mean", "moving_var"],
    'BatchNorm': ["", "gamma", "beta", "moving_mean", "moving_var"],
}

# Functions that have to be cast to FP32 due to possible
# overflows
FP32_FUNCS = [
    'amp_cast',
    'amp_multicast',
    'BilinearSampler',
    'BlockGrad',
    'CTCLoss',
    'Correlation',
    'Crop',
    'Custom',
    'Dropout',
    'Embedding',
    'GridGenerator',
    'GroupNorm',
    'IdentityAttachKLSparseReg',
    'InstanceNorm',
    'L2Normalization',
    'LinearRegressionOutput',
    'LogisticRegressionOutput',
    'MAERegressionOutput',
    'MakeLoss',
    'Pad',
    'RNN',
    'ROIPooling',
    'SVMOutput',
    'SequenceLast',
    'SequenceMask',
    'SequenceReverse',
    'SliceChannel',
    'SoftmaxActivation',
    'SoftmaxOutput',
    'SpatialTransformer',
    'SwapAxis',
    'UpSampling',
    '_CachedOp',
    '_CachedOpThreadSafe',
    '_CrossDeviceCopy',
    '_CustomFunction',
    '_NDArray',
    '_Native',
    '_NoGradient',
    '_adabelief_update',
    '_adamw_update',
    '_arange',
    '_cond',
    '_contrib_BilinearResize2D',
    '_contrib_DeformablePSROIPooling',
    '_contrib_MultiBoxDetection',
    '_contrib_MultiBoxPrior',
    '_contrib_MultiBoxTarget',
    '_contrib_MultiProposal',
    '_contrib_PSROIPooling',
    '_contrib_Proposal',
    '_contrib_ROIAlign',
    '_contrib_RROIAlign',
    '_contrib_SyncBatchNorm',
    '_contrib_allclose',
    '_contrib_arange_like',
    '_contrib_bipartite_matching',
    '_contrib_boolean_mask',
    '_contrib_box_decode',
    '_contrib_box_encode',
    '_contrib_box_iou',
    '_contrib_box_nms',
    '_contrib_calibrate_entropy',
    '_contrib_count_sketch',
    '_contrib_dequantize',
    '_contrib_dgl_adjacency',
    '_contrib_dgl_csr_neighbor_non_uniform_sample',
    '_contrib_dgl_csr_neighbor_uniform_sample',
    '_contrib_dgl_graph_compact',
    '_contrib_dgl_subgraph',
    '_contrib_div_sqrt_dim',
    '_contrib_dynamic_reshape',
    '_contrib_edge_id',
    '_contrib_fft',
    '_contrib_getnnz',
    '_contrib_gradientmultiplier',
    '_contrib_group_adagrad_update',
    '_contrib_hawkesll',
    '_contrib_index_array',
    '_contrib_index_copy',
    '_contrib_interleaved_matmul_encdec_qk',
    '_contrib_interleaved_matmul_encdec_valatt',
    '_contrib_interleaved_matmul_selfatt_qk',
    '_contrib_interleaved_matmul_selfatt_valatt',
    '_contrib_intgemm_fully_connected',
    '_contrib_intgemm_maxabsolute',
    '_contrib_intgemm_prepare_data',
    '_contrib_intgemm_prepare_weight',
    '_contrib_intgemm_take_weight',
    '_contrib_quadratic',
    '_contrib_quantize',
    '_contrib_quantize_asym',
    '_contrib_quantized_act',
    '_contrib_quantized_batch_norm',
    '_contrib_quantized_concat',
    '_contrib_quantized_conv',
    '_contrib_quantized_elemwise_add',
    '_contrib_quantized_elemwise_mul',
    '_contrib_quantized_embedding',
    '_contrib_quantized_flatten',
    '_contrib_quantized_fully_connected',
    '_contrib_quantized_pooling',
    '_contrib_quantized_reshape',
    '_contrib_quantized_rnn',
    '_contrib_quantized_transpose',
    '_contrib_requantize',
    '_contrib_round_ste',
    '_contrib_sign_ste',
    '_contrib_sldwin_atten_context',
    '_contrib_sldwin_atten_mask_like',
    '_contrib_sldwin_atten_score',
    '_copyto',
    '_cvcopyMakeBorder',
    '_cvimdecode',
    '_cvimread',
    '_cvimresize',
    '_div_scalar',
    '_equal',
    '_equal_scalar',
    '_eye',
    '_foreach',
    '_full',
    '_grad_add',
    '_greater',
    '_greater_equal',
    '_greater_equal_scalar',
    '_greater_scalar',
    '_histogram',
    '_hypot',
    '_hypot_scalar',
    '_identity_with_attr_like_rhs',
    '_image_adjust_lighting',
    '_image_crop',
    '_image_flip_left_right',
    '_image_flip_top_bottom',
    '_image_normalize',
    '_image_random_brightness',
    '_image_random_color_jitter',
    '_image_random_contrast',
    '_image_random_crop',
    '_image_random_flip_left_right',
    '_image_random_flip_top_bottom',
    '_image_random_hue',
    '_image_random_lighting',
    '_image_random_resized_crop',
    '_image_random_saturation',
    '_image_resize',
    '_image_to_tensor',
    '_imdecode',
    '_lesser',
    '_lesser_equal',
    '_lesser_equal_scalar',
    '_lesser_scalar',
    '_linalg_det',
    '_linalg_extractdiag',
    '_linalg_extracttrian',
    '_linalg_gelqf',
    '_linalg_gemm',
    '_linalg_gemm2',
    '_linalg_inverse',
    '_linalg_makediag',
    '_linalg_maketrian',
    '_linalg_potrf',
    '_linalg_potri',
    '_linalg_slogdet',
    '_linalg_sumlogdiag',
    '_linalg_syevd',
    '_linalg_syrk',
    '_linalg_trmm',
    '_linalg_trsm',
    '_linspace',
    '_logical_and',
    '_logical_and_scalar',
    '_logical_or',
    '_logical_or_scalar',
    '_logical_xor',
    '_logical_xor_scalar',
    '_maximum',
    '_maximum_scalar',
    '_minimum',
    '_minimum_scalar',
    '_minus_scalar',
    '_mod',
    '_mod_scalar',
    '_mp_adabelief_update',
    '_mp_adamw_update',
    '_mul_scalar',
    '_multi_adabelief_update',
    '_multi_adamw_update',
    '_multi_lamb_update',
    '_multi_lans_update',
    '_multi_mp_adabelief_update',
    '_multi_mp_adamw_update',
    '_multi_mp_lamb_update',
    '_multi_mp_lans_update',
    '_not_equal',
    '_not_equal_scalar',
    '_np_reshape',
    '_npi_absolute',
    '_npi_add_scalar',
    '_npi_advanced_indexing',
    '_npi_advanced_indexing_multiple',
    '_npi_all',
    '_npi_any',
    '_npi_arange',
    '_npi_arccos',
    '_npi_arccosh',
    '_npi_arcsin',
    '_npi_arcsinh',
    '_npi_arctan',
    '_npi_arctan2',
    '_npi_arctan2_scalar',
    '_npi_arctanh',
    '_npi_argmax',
    '_npi_argmin',
    '_npi_around',
    '_npi_atleast_1d',
    '_npi_atleast_2d',
    '_npi_atleast_3d',
    '_npi_average',
    '_npi_bernoulli',
    '_npi_bincount',
    '_npi_bitwise_and',
    '_npi_bitwise_and_scalar',
    '_npi_bitwise_left_shift',
    '_npi_bitwise_left_shift_scalar',
    '_npi_bitwise_not',
    '_npi_bitwise_or',
    '_npi_bitwise_or_scalar',
    '_npi_bitwise_right_shift',
    '_npi_bitwise_right_shift_scalar',
    '_npi_bitwise_xor',
    '_npi_bitwise_xor_scalar',
    '_npi_blackman',
    '_npi_boolean_mask_assign_scalar',
    '_npi_boolean_mask_assign_tensor',
    '_npi_broadcast_to',
    '_npi_cbrt',
    '_npi_ceil',
    '_npi_choice',
    '_npi_cholesky',
    '_npi_column_stack',
    '_npi_copy',
    '_npi_copysign',
    '_npi_copysign_scalar',
    '_npi_cos',
    '_npi_cosh',
    '_npi_cross',
    '_npi_cumsum',
    '_npi_degrees',
    '_npi_delete',
    '_npi_diag',
    '_npi_diag_indices_from',
    '_npi_diagflat',
    '_npi_diagonal',
    '_npi_diff',
    '_npi_dsplit',
    '_npi_dstack',
    '_npi_ediff1d',
    '_npi_eig',
    '_npi_eigh',
    '_npi_eigvals',
    '_npi_eigvalsh',
    '_npi_einsum',
    '_npi_equal',
    '_npi_equal_scalar',
    '_npi_expm1',
    '_npi_exponential',
    '_npi_eye',
    '_npi_fill_diagonal',
    '_npi_fix',
    '_npi_flip',
    '_npi_floor',
    '_npi_floor_divide',
    '_npi_floor_divide_scalar',
    '_npi_fmax',
    '_npi_fmax_scalar',
    '_npi_fmin',
    '_npi_fmin_scalar',
    '_npi_fmod',
    '_npi_fmod_scalar',
    '_npi_full',
    '_npi_full_like',
    '_npi_gamma',
    '_npi_gcd',
    '_npi_gcd_scalar',
    '_npi_greater',
    '_npi_greater_equal',
    '_npi_greater_equal_scalar',
    '_npi_greater_scalar',
    '_npi_gumbel',
    '_npi_hamming',
    '_npi_hanning',
    '_npi_hsplit',
    '_npi_hstack',
    '_npi_hypot',
    '_npi_identity',
    '_npi_indices',
    '_npi_insert_scalar',
    '_npi_insert_slice',
    '_npi_insert_tensor',
    '_npi_interp',
    '_npi_isfinite',
    '_npi_isinf',
    '_npi_isnan',
    '_npi_isneginf',
    '_npi_isposinf',
    '_npi_kron',
    '_npi_laplace',
    '_npi_lcm',
    '_npi_lcm_scalar',
    '_npi_ldexp',
    '_npi_ldexp_scalar',
    '_npi_less',
    '_npi_less_equal',
    '_npi_less_equal_scalar',
    '_npi_less_scalar',
    '_npi_linspace',
    '_npi_log',
    '_npi_log10',
    '_npi_log1p',
    '_npi_log2',
    '_npi_logaddexp',
    '_npi_logaddexp_scalar',
    '_npi_logical_and',
    '_npi_logical_and_scalar',
    '_npi_logical_not',
    '_npi_logical_or',
    '_npi_logical_or_scalar',
    '_npi_logical_xor',
    '_npi_logical_xor_scalar',
    '_npi_logistic',
    '_npi_logspace',
    '_npi_lstsq',
    '_npi_matmul',
    '_npi_matrix_rank',
    '_npi_matrix_rank_none_tol',
    '_npi_max',
    '_npi_min',
    '_npi_mod',
    '_npi_mod_scalar',
    '_npi_moveaxis',
    '_npi_multinomial',
    '_npi_multiply_scalar',
    '_npi_nan_to_num',
    '_npi_negative',
    '_npi_norm',
    '_npi_normal',
    '_npi_normal_n',
    '_npi_not_equal',
    '_npi_not_equal_scalar',
    '_npi_ones',
    '_npi_pad',
    '_npi_pareto',
    '_npi_percentile',
    '_npi_pinv',
    '_npi_pinv_scalar_rcond',
    '_npi_polyval',
    '_npi_power',
    '_npi_power_scalar',
    '_npi_powerd',
    '_npi_prod',
    '_npi_qr',
    '_npi_radians',
    '_npi_rarctan2_scalar',
    '_npi_rayleigh',
    '_npi_rbitwise_left_shift_scalar',
    '_npi_rbitwise_right_shift_scalar',
    '_npi_rcopysign_scalar',
    '_npi_reciprocal',
    '_npi_repeats',
    '_npi_rfloor_divide_scalar',
    '_npi_rfmod_scalar',
    '_npi_rint',
    '_npi_rldexp_scalar',
    '_npi_rmod_scalar',
    '_npi_roll',
    '_npi_rollaxis',
    '_npi_rot90',
    '_npi_rpower_scalar',
    '_npi_rsubtract_scalar',
    '_npi_rtrue_divide_scalar',
    '_npi_share_memory',
    '_npi_sign',
    '_npi_sin',
    '_npi_sinh',
    '_npi_solve',
    '_npi_squeeze',
    '_npi_std',
    '_npi_subtract_scalar',
    '_npi_svd',
    '_npi_tan',
    '_npi_tensordot',
    '_npi_tensordot_int_axes',
    '_npi_tensorinv',
    '_npi_tensorsolve',
    '_npi_trace',
    '_npi_tri',
    '_npi_tril',
    '_npi_tril_indices',
    '_npi_triu',
    '_npi_true_divide_scalar',
    '_npi_trunc',
    '_npi_uniform',
    '_npi_uniform_n',
    '_npi_unique',
    '_npi_var',
    '_npi_vstack',
    '_npi_weibull',
    '_npi_where_lscalar',
    '_npi_where_rscalar',
    '_npi_where_scalar2',
    '_npi_zeros',
    '_npx_cond',
    '_npx_constraint_check',
    '_npx_deformable_convolution',
    '_npx_foreach',
    '_npx_index_add',
    '_npx_index_update',
    '_npx_modulated_deformable_convolution',
    '_npx_nonzero',
    '_npx_quantized_reshape',
    '_npx_quantized_transpose',
    '_npx_relu',
    '_npx_sigmoid',
    '_npx_while_loop',
    '_onehot_encode',
    '_ones',
    '_plus_scalar',
    '_power',
    '_power_scalar',
    '_random_binomial',
    '_random_exponential',
    '_random_exponential_like',
    '_random_gamma',
    '_random_gamma_like',
    '_random_generalized_negative_binomial',
    '_random_generalized_negative_binomial_like',
    '_random_negative_binomial',
    '_random_negative_binomial_like',
    '_random_normal',
    '_random_normal_like',
    '_random_pdf_dirichlet',
    '_random_pdf_exponential',
    '_random_pdf_gamma',
    '_random_pdf_generalized_negative_binomial',
    '_random_pdf_negative_binomial',
    '_random_pdf_normal',
    '_random_pdf_poisson',
    '_random_pdf_uniform',
    '_random_poisson',
    '_random_poisson_like',
    '_random_randint',
    '_random_uniform',
    '_random_uniform_like',
    '_ravel_multi_index',
    '_rdiv_scalar',
    '_rminus_scalar',
    '_rmod_scalar',
    '_rnn_param_concat',
    '_rpower_scalar',
    '_sample_binomial',
    '_sample_categorical',
    '_sample_exponential',
    '_sample_gamma',
    '_sample_generalized_negative_binomial',
    '_sample_multinomial',
    '_sample_negative_binomial',
    '_sample_normal',
    '_sample_poisson',
    '_sample_uniform',
    '_sample_unique_zipfian',
    '_scatter_set_nd',
    '_set_value',
    '_shuffle',
    '_slice_assign',
    '_slice_assign_scalar',
    '_sparse_adagrad_update',
    '_sparse_retain',
    '_square_sum',
    '_unravel_index',
    '_while_loop',
    '_zeros',
    '_zeros_without_dtype',
    'abs',
    'adam_update',
    'all_finite',
    'arccos',
    'arccosh',
    'arcsin',
    'arcsinh',
    'arctan',
    'arctanh',
    'argmax',
    'argmax_channel',
    'argmin',
    'argsort',
    'batch_take',
    'broadcast_axis',
    'broadcast_equal',
    'broadcast_greater',
    'broadcast_greater_equal',
    'broadcast_hypot',
    'broadcast_lesser',
    'broadcast_lesser_equal',
    'broadcast_like',
    'broadcast_logical_and',
    'broadcast_logical_or',
    'broadcast_logical_xor',
    'broadcast_maximum',
    'broadcast_minimum',
    'broadcast_mod',
    'broadcast_not_equal',
    'broadcast_power',
    'broadcast_to',
    'cast_storage',
    'cbrt',
    'ceil',
    'clip',
    'col2im',
    'cos',
    'cosh',
    'degrees',
    'depth_to_space',
    'diag',
    'digamma',
    'elemwise_div',
    'elemwise_mul',
    'elemwise_sub',
    'erf',
    'erfinv',
    'exp',
    'expm1',
    'fill_element_0index',
    'fix',
    'floor',
    'ftml_update',
    'ftrl_update',
    'gamma',
    'gammaln',
    'gather_nd',
    'hard_sigmoid',
    'im2col',
    'khatri_rao',
    'lamb_update_phase1',
    'lamb_update_phase2',
    'log',
    'log10',
    'log1p',
    'log2',
    'log_sigmoid',
    'logical_not',
    'make_loss',
    'masked_log_softmax',
    'max',
    'min',
    'mish',
    'moments',
    'mp_lamb_update_phase1',
    'mp_lamb_update_phase2',
    'mp_nag_mom_update',
    'mp_sgd_mom_update',
    'mp_sgd_update',
    'multi_all_finite',
    'multi_lars',
    'multi_mp_sgd_mom_update',
    'multi_mp_sgd_update',
    'multi_sgd_mom_update',
    'multi_sgd_update',
    'multi_sum_sq',
    'nag_mom_update',
    'nanprod',
    'nansum',
    'negative',
    'norm',
    'one_hot',
    'ones_like',
    'pick',
    'preloaded_multi_mp_sgd_mom_update',
    'preloaded_multi_mp_sgd_update',
    'preloaded_multi_sgd_mom_update',
    'preloaded_multi_sgd_update',
    'prod',
    'radians',
    'rcbrt',
    'reciprocal',
    'relu',
    'repeat',
    'reset_arrays',
    'reshape_like',
    'reverse',
    'rint',
    'rmsprop_update',
    'rmspropalex_update',
    'round',
    'rsqrt',
    'scatter_nd',
    'sgd_mom_update',
    'sgd_update',
    'shape_array',
    'sigmoid',
    'sign',
    'signsgd_update',
    'signum_update',
    'sin',
    'sinh',
    'size_array',
    'slice_axis',
    'slice_like',
    'smooth_l1',
    'softmax_cross_entropy',
    'softmin',
    'softsign',
    'sort',
    'sqrt',
    'square',
    'squeeze',
    'tan',
    'tanh',
    'tile',
    'topk',
    'trunc',
    'zeros_like',
]

# Functions that have to be cast to FP32 only for
# some values of their parameters
CONDITIONAL_FP32_FUNCS = [
    ('LeakyReLU', 'act_type', ['selu']),
]

LOSS_OUTPUT_FUNCTIONS = [
    'SoftmaxOutput'
]

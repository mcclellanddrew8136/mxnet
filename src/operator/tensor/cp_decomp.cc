#include "./cp_decomp-inl.h"
#include <cmath>
#include <random>
#include <vector>
#include <utility>
#include <numeric>

#if MSHADOW_USE_MKL
extern "C" {
  #include <mkl.h>
}
#else
#error "The current implementation is only for MKL"
#endif


namespace mxnet {
namespace op {  

using namespace mshadow;
using namespace mshadow::expr;

template <typename T>
std::vector<std::vector<T> > cart_product
	(const std::vector<std::vector<T> > &v) {
	std::vector<std::vector<T> > s = {{}};
	for (const auto &u : v) {
		std::vector<std::vector<T> > r;
		for (const auto y : u) {
			for (const auto &prod : s) {
			  r.push_back(prod);
			  r.back().push_back(y);
			}
		}
		s = std::move(r);
	}
	return s;
}

template <int order, typename DType>
DType TensorAt(const Tensor<cpu, order, DType> &t,
  const std::vector<index_t> &coord) {
  if (coord.size() == 1)
    return t[coord[0]].dptr_[0];

  std::vector<index_t> coord_tail(coord.begin() + 1, coord.end());
  return TensorAt(t[coord[0]], coord_tail);
}

template <int order, typename DType>
inline void Unfold
  (Tensor<cpu, 2, DType> &unfolding,
  const Tensor<cpu, order, DType> &t,
  int mode,
  Stream<cpu> *stream) {
	std::vector<std::vector<index_t> > v;
  for (size_t id_mode = 0; id_mode < order; ++id_mode) {
    if (id_mode == mode)
      continue;

    std::vector<index_t> u(t.size(id_mode));
    std::iota(u.begin(), u.end(), 0);
    v.push_back(u);
  }

  std::vector<std::vector<index_t> > coords = cart_product(v);
  for (index_t i = 0; i < t.size(mode); ++i) {
    for (index_t j = 0; j < (index_t) coords.size(); ++j) {
      std::vector<index_t> coord_ = coords[j];
      coord_.insert(coord_.begin() + mode, i);

      unfolding[i][j] = TensorAt(t, coord_);
    }
  }
}


template <int order, typename DType>
inline int CPDecompUpdate
  (Tensor<cpu, 1, DType> &eigvals,
  std::vector<Tensor<cpu, 2, DType> > &factors,
  const Tensor<cpu, order, DType> &unfolding,
  int mode,
  Tensor<cpu, 2, DType> &kr_prod,
  Tensor<cpu, 2, DType> &hd_prod,
  Stream<cpu> *stream = NULL); 

template <typename DType>
inline bool CPDecompConverged
  (const Tensor<cpu, 1, DType> &eigvals,
  const std::vector<Tensor<cpu, 2, DType> > &factors_T,
  const Tensor<cpu, 1, DType> &oldEigvals,
  const std::vector<Tensor<cpu, 2, DType> > &oldFactors_T,
  DType eps);

template <int order, typename DType>
inline int CPDecompForward
  (Tensor<cpu, 1, DType> &eigvals,
  std::vector<Tensor<cpu, 2, DType> > &factors_T,
  const Tensor<cpu, order, DType> &in,
  int k, 
  DType eps,
  int max_iter,
  Stream<cpu> *stream) {
  CHECK_EQ(eigvals.size(0), k);
  CHECK_EQ(factors_T.size(), order);
  CHECK(k >= 1);
  for (size_t i = 0; i < order; ++i) {
    CHECK_EQ(factors_T[i].size(0), k);
    CHECK_EQ(factors_T[i].size(1), in.size(i));
  }

  // Return value
  int info;

  // in is unfolded mode-1 tensor
  // Transform it into mode-2, mode-3 tensors
  int tensor_size = 1;
  for (size_t i = 0; i < order; ++i)
    tensor_size *= in.size(i);

  std::vector<TensorContainer<cpu, 2, DType> > unfoldings;
  for (size_t id_mode = 0; id_mode < order; ++id_mode) {
    unfoldings.emplace_back
      (Shape2(in.size(id_mode), tensor_size / in.size(id_mode)));
    Unfold(unfoldings[id_mode], in, id_mode);
  }

  // Allocate space for old factor matrices A, B, C, etc,
  // transposed as well, with the same shapes as factors_T
  TensorContainer<cpu, 1, DType> oldEigvals(Shape1(k));
  std::vector<TensorContainer<cpu, 2, DType> > oldFactors_T;
  for (size_t id_mode = 0; id_mode < order; ++id_mode)
    oldFactors_T.emplace_back(factors_T[id_mode].shape_);

  // The intermediate tensors are reused for efficiency
  // We store the transpose of all intermediate and final
  // Khatri-Rao products for convenience of computation
  //
  // As across the modes, the intermediate Khatri-Rao products
  // will eventually take different shapes, kr_prod_T is 
  // first indexed by mode, then by the number of Khatri-Rao
  // products already done
  std::vector<std::vector<TensorContainer<cpu, 2, DType> > > kr_prod_T;
  int kr_length;
  for (size_t id_mode = 0; id_mode < order; ++id_mode) {
    kr_length = 1;
   
    std::vector<TensorContainer<cpu, 2, DType> > kr_prod_T_;
    kr_prod_T_.emplace_back(Shape2(k, kr_length));
    kr_prod_T_[0] = 1;

    for (int q = order - 1; q >= 0; --q) {
      if (q == id_mode)
        continue;

      kr_length *= in.size(q);
      kr_prod_T_.emplace_back(Shape2(k, kr_length));
    }

    kr_prod_T.push_back(kr_prod_T_);
  }

  // Hadamard product
  TensorContainer<cpu, 2, DType> hd_prod(Shape2(k, k));

  // Randomly initialise factor matrices
  // TODO implement seed for random generator
  std::default_random_engine generator;
  std::normal_distribution<double> normal(0.0, 1.0);

  for (size_t id_mode = 0; id_mode < order; ++id_mode) {
    for (size_t i = 0; i < k; ++i) { 
      for (size_t j = 0; j < factors_T[id_mode].size(1); ++j)
        factors_T[id_mode][i][j] = normal(generator);
    }
  }

  // ALS  
  int iter = 0;
  while (iter < max_iter 
      && (iter == 0 || !CPDecompConverged(eigvals, factors_T, 
                                   oldEigvals, oldFactors_T, eps))) {
    Copy(oldEigvals, eigvals);
    for (size_t id_mode = 0; id_mode < order; ++id_mode)
      Copy(oldFactors_T[id_mode], factors_T[id_mode]);

    for (size_t id_mode = 0; id_mode < order; ++id_mode) {
      info = CPDecompUpdate
        (eigvals, factors_T, 
        unfoldings[id_mode], id_mode,
        kr_prod_T[id_mode], hd_prod,
        stream);
      if (info != 0)
        return info;
    }

    ++iter;
  }

  return 0;
}

template <typename Device, typename DType>
int posv(int n, int nrhs, DType *a, int lda, DType *b, int ldb);

template <>
int posv<cpu, float>(int n, int nrhs, 
    float *a, int lda, float *b, int ldb) {
  return LAPACKE_sposv(LAPACK_ROW_MAJOR, 'U', n, nrhs, a, lda, b, ldb); 
}

template <>
int posv<cpu, double>(int n, int nrhs, 
    double *a, int lda, double *b, int ldb) {
  return LAPACKE_dposv(LAPACK_ROW_MAJOR, 'U', n, nrhs, a, lda, b, ldb); 
}

template <typename Device, typename DType>
DType nrm2(int n, DType *a, int lda);

template <>
float nrm2<cpu, float>(int n, float *a, int lda) {
  return cblas_snrm2(n, a, lda);
}

template <>
double nrm2<cpu, double>(int n, double *a, int lda) {
  return cblas_dnrm2(n, a, lda);
}

template <int order, typename DType>
inline int CPDecompUpdate
  (Tensor<cpu, 1, DType> &eigvals,
  std::vector<Tensor<cpu, 2, DType> > &factors_T,
  const Tensor<cpu, order, DType> &unfolding,
  int mode,
  std::vector<Tensor<cpu, 2, DType> > &kr_prod_T,
  Tensor<cpu, 2, DType> &hd_prod,
  Stream<cpu> *stream = NULL) {
  int k = eigvals.size(0);

  CHECK_EQ(factors_T.size(), order);
  for (size_t i = 0; i < order; ++i)
    CHECK_EQ(factors_T[i].size(0), k);

  CHECK(mode >= 0 && mode < order);
  
  // Return value
  int info;

  // Compute khatri-rao product of C\odot B ...
  // and hadamard product of C^T C * B^T B ...
  int kr_length = 1;
  int id_kr_prod = 1;
  int d;

  hd_prod = 1;

  for (int id_mode = order - 1; id_mode >= 0; --id_mode) {
    if (id_mode == mode)
      continue;
    
    d = factors_T[id_mode].size(1);
    for (index_t i = 0; i < k; ++i) {
      expr::BLASEngine<cpu, DType>::SetStream
        (kr_prod_T[id_kr_prod][i].stream_);
      expr::BLASEngine<cpu, DType>::ger
        (kr_prod_T[id_kr_prod][i].stream_,
        d, kr_length,
        1,
        factors_T[id_mode][i].dptr_, 1,
        kr_prod_T[id_kr_prod - 1][i].dptr_, 1,
        kr_prod_T[id_kr_prod][i].dptr_, d);
    }
    kr_length *= d;
    ++id_kr_prod;

    hd_prod = hd_prod * 
        implicit_dot(factors_T[id_mode], factors_T[id_mode].T());
  }

  TensorContainer<cpu, 2, DType> rhs_T(Shape2(k, unfolding.size(0)));
  rhs_T = implicit_dot(kr_prod_T[order - 1], unfolding.T());

  // In order to compute rhs pinv(hd_prod) we try to solve for X 
  // such that
  //
  //     hd_prod X^T = rhs^T
  //
  // and update factors_T[mode] to be X^T

  info = posv<cpu, DType>(k, unfolding.size(0), 
      hd_prod.dptr_, hd_prod.stride_, 
      rhs_T.dptr_, rhs_T.stride_);
  if (info != 0) {
    return info; 
  }
  Copy(factors_T[mode], rhs_T);

  for (index_t j = 0; j < k; ++j) {
    // Compute the L2-norm of Column j of factors[mode]
    eigvals[j] = nrm2(factors_T[mode].size(1), factors_T[mode][j].dptr_, 1);
    
    // Normalise Column j of factors[mode]
    factors_T[mode][j] = factors_T[mode][j] / eigvals[j];
  }

  return 0;
}

template <typename DType>
inline bool CPDecompConverged
  (const Tensor<cpu, 1, DType> &eigvals,
  const std::vector<Tensor<cpu, 2, DType> > &factors_T,
  const Tensor<cpu, 1, DType> &oldEigvals,
  const std::vector<Tensor<cpu, 2, DType> > &oldFactors_T,
  DType eps) {
  int k = eigvals.size(0);

  TensorContainer<cpu, 1, DType> eigval_diff(eigvals.shape_);
  eigval_diff = eigvals - oldEigvals;
  if (nrm2(k, eigval_diff.dptr_, 1) > eps * nrm2(k, oldEigvals.dptr_, 1))
    return false;

  int d;
  for (size_t p = 0; p < factors_T.size(); ++p) {
    d = factors_T[p].size(1);
    TensorContainer<cpu, 2, DType> factors_diff(factors_T[p].shape_);
    factors_diff = factors_T[p] - oldFactors_T[p];

    for (size_t i = 0; i < k; ++i) {
      if (nrm2(d, factors_diff[i].dptr_, 1)
          > eps * nrm2(d, oldFactors_T[p][i].dptr_, 1))
        return false;
    }
  }

  return true;
}

}
}



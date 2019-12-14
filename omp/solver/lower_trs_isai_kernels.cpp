/*******************************<GINKGO LICENSE>******************************
Copyright (c) 2017-2019, the Ginkgo authors
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************<GINKGO LICENSE>*******************************/

#include "core/solver/lower_trs_isai_kernels.hpp"


#include <memory>


#include <omp.h>


#include <ginkgo/core/base/array.hpp>
#include <ginkgo/core/base/exception_helpers.hpp>
#include <ginkgo/core/base/math.hpp>
#include <ginkgo/core/base/types.hpp>
#include <ginkgo/core/matrix/csr.hpp>
#include <ginkgo/core/matrix/dense.hpp>
#include <ginkgo/core/solver/lower_trs_isai.hpp>


namespace gko {
namespace kernels {
namespace omp {
/**
 * @brief The LOWER_TRS_ISAI solver namespace.
 *
 * @ingroup lower_trs_isai
 */
namespace lower_trs_isai {

template <typename ValueType, typename IndexType>
void isai_lower_col(
    gko::matrix::Csr<ValueType, IndexType> const *l,  // Input matrix
    gko::matrix::Csr<ValueType, IndexType> *linvt,    // ISAI transpose
    IndexType col  // Column of ISAI to be computed
)
{
    // std::string context = "isai_lower_col";

    // L factor
    auto const l_row_ptrs = l->get_const_row_ptrs();
    auto const l_col_idxs = l->get_const_col_idxs();
    auto const l_vals = l->get_const_values();

    // M_L precond (transpose)
    auto const col_ptrs = linvt->get_const_row_ptrs();
    auto const row_idxs = linvt->get_const_col_idxs();
    auto vals = linvt->get_values();

    auto colptr_sa = col_ptrs[col];
    auto colptr_en = col_ptrs[col + 1] - 1;

    // Number of nnz in current column
    int m = col_ptrs[col + 1] - col_ptrs[col];

    // Submatrix workspace used to compute column col of precond
    int ldsmat = m;
    gko::size_type smatsz = m * ldsmat;
    // smat is treated as column major
    std::vector<ValueType> smat(smatsz, 0.0);  // Init with zeros

    for (auto colptr = colptr_sa; colptr <= colptr_en; ++colptr) {
        auto row = row_idxs[colptr];

        auto rowptr_sa = l_row_ptrs[row];  // Ptr to first column in current row
        auto rowptr_en =
            l_row_ptrs[row + 1] - 1;  // Ptr to last column in current row

        // Row ptr to coeff in row of L
        auto rowptr = rowptr_sa;
        // Column ptr to coeff in column of M
        auto ptr = colptr_sa;

        // Coefficient indexes in sybmatrix
        IndexType ii = colptr - colptr_sa;  // Row index
        IndexType jj = 0;                   // Column index

        while ((rowptr <= rowptr_en) && (ptr <= colptr_en)) {
            auto j = l_col_idxs[rowptr];
            auto k = row_idxs[ptr];

            if (j == k) {
                // Col idx of M and row idx of L match: add element to
                // submatrix

                smat[ii + ldsmat * jj] = l_vals[rowptr];
                ++jj;
                ++ptr;
                ++rowptr;
            } else if (j > k) {
                // No matching element in L: add zero coefficient

                ++jj;
                ++ptr;
            } else {
                // j < k
                //
                // Current element in L does not match sparsity pattern of
                // column col in M
                ++rowptr;
            }
        }
    }

    // Solve triangular subsystem to compute current column of M
    // precond by solver L M_col = e_col

    // FIXME: Require linvt to be initialised with identity?

    // Set diagonal element to 1.0
    vals[colptr_sa] = static_cast<ValueType>(1.0);

    for (IndexType j = 1; j < m; ++j)
        vals[colptr_sa + j] = static_cast<ValueType>(0.0);

    // int nrhs = 1;
    // double alpha = 1.0;
    // char fside = 'L';
    // char fuplo = 'L';
    // char ftransa = 'N';
    // char fdiag = 'U';

    // dtrsm_(
    //       &fside, &fuplo, &ftransa, &fdiag,
    //       &m, &nrhs, &alpha,
    //       &smat[0], &ldsmat,
    //       &vals[colptr_sa], &m);

    for (IndexType j = 0; j < m; ++j) {
        vals[colptr_sa + j] /= smat[j * (1 + ldsmat)];
        for (IndexType i = j + 1; i < m; ++i) {
            vals[colptr_sa + i] -= vals[colptr_sa + j] * smat[i + j * ldsmat];
        }
    }
}

template <typename ValueType, typename IndexType>
void build_isai(std::shared_ptr<const OmpExecutor> exec,
                matrix::Csr<ValueType, IndexType> const *matrix,
                matrix::Csr<ValueType, IndexType> *isai)
{
    // std::string context = "build_isai";

    IndexType ncol = matrix->get_size()[1];

    using Mtx = gko::matrix::Csr<ValueType, IndexType>;
    auto linvt_linop = matrix->transpose();
    auto linvt = static_cast<Mtx *>(linvt_linop.get());

#pragma omp parallel
    {
        IndexType nworkers = 1;
#ifdef _OPENMP
        nworkers = omp_get_num_threads();
#endif


#pragma omp single
        {
            // std::cout << context << ", nworkers = " << nworkers << std::endl;

            // Number of of block columns corresponding to the number
            // of OMP tasks
            IndexType nbc = 10 * nworkers;
            // Number of columns per workers
            IndexType nb = (ncol - 1) / nbc + 1;

            // Compute lfactinv for each columns
            for (IndexType bc = 0; bc < nbc; ++bc) {
#pragma omp task firstprivate(bc)

                for (IndexType col = bc * nb;
                     col < std::min(ncol, (bc + 1) * nb); ++col) {
                    // shared(l) firstprivate(col)
                    isai_lower_col(matrix, linvt, col);
                }
            }
#pragma omp taskwait

        }  // omp single
    }      // omp parallel

    auto linv_linop = linvt->transpose();
    auto linv = static_cast<Mtx *>(linv_linop.get());
    isai->copy_from(linv);
}


GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_LOWER_TRS_ISAI_BUILD_KERNEL);


}  // namespace lower_trs_isai
}  // namespace omp
}  // namespace kernels
}  // namespace gko

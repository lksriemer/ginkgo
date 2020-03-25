/*******************************<GINKGO LICENSE>******************************
Copyright (c) 2017-2020, the Ginkgo authors
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

#include "core/preconditioner/isai_kernels.hpp"


#include <algorithm>
#include <memory>
#include <vector>


#include <ginkgo/core/base/array.hpp>
#include <ginkgo/core/base/exception_helpers.hpp>
#include <ginkgo/core/matrix/csr.hpp>


#include "core/matrix/csr_builder.hpp"


namespace gko {
namespace kernels {
namespace reference {
/**
 * @brief The Isai preconditioner namespace.
 *
 * @ingroup isai
 */
namespace isai {


template <typename ValueType, typename IndexType, typename Callable>
void generic_generate(std::shared_ptr<const DefaultExecutor> exec,
                      const matrix::Csr<ValueType, IndexType> *mtx,
                      matrix::Csr<ValueType, IndexType> *inverse_mtx,
                      Callable trs_solve)
{
    /*
    Consider: aiM := inverse_mtx; M := mtx
    I := Identity matrix
    e(i) := unit vector i (containing all zeros except for row i, which is one)
    S := Sparsity pattern of the desired aiM
    S(i) := Sparsity pattern of row i of aiM (Set of non-zero columns)
    D(i) := L[S(i), S(i)]
    aiM := approximate inverse of M

    Target: Solving (aiM * M = I)_{S} (aiM * M = I for the sparsity pattern S)
    aiM[i, :] * D(i) = e(i)^T
    <=> D(i)^T * vL[i, :]^T = e(i)   =^ Triangular system (Trs)
    Solve Trs, fill in aiM row by row (coalesced access)
    */
    const auto size = mtx->get_size();
    const auto m_row_ptrs = mtx->get_const_row_ptrs();
    const auto m_cols = mtx->get_const_col_idxs();
    const auto m_vals = mtx->get_const_values();
    auto i_row_ptrs = inverse_mtx->get_row_ptrs();
    auto i_cols = inverse_mtx->get_col_idxs();
    auto i_vals = inverse_mtx->get_values();

    // expect mtx and inverse_mtx to have the same number of elems
    const auto num_elems = mtx->get_num_stored_elements();
    // Copy sparsity pattern of original into the inverse of L
    for (IndexType col = 0; col < size[1] + 1; ++col) {
        i_row_ptrs[col] = m_row_ptrs[col];
    }
    for (IndexType i = 0; i < num_elems; ++i) {
        i_cols[i] = m_cols[i];
    }

    std::vector<ValueType> rhs;  // RHS for local trisystem
    // memory for dense trisystem in column major:
    std::vector<ValueType> trisystem;

    for (IndexType row = 0; row < size[0]; ++row) {
        const auto i_row_begin = i_row_ptrs[row];
        const auto i_row_end = i_row_ptrs[row + 1];
        const auto i_row_elems = i_row_end - i_row_begin;

        trisystem.clear();
        trisystem.reserve(i_row_elems * i_row_elems);
        for (IndexType i = 0; i < i_row_elems * i_row_elems; ++i) {
            trisystem.push_back(zero<ValueType>());
        }

        for (IndexType i = 0; i < i_row_elems; ++i) {
            const auto col = i_cols[i_row_begin + i];
            const auto m_row_end = m_row_ptrs[col + 1];
            auto m_row_ptr = m_row_ptrs[col];
            auto i_row_ptr = i_row_begin;
            // Values in `trisystem` will be stored in transposed order
            // to prevent the need for an explicit transpose.
            // Since `trisystem` stores in column major, this is equivalent
            // to storing it in row-major and reading in column major.
            auto idx = i * i_row_elems;
            while (m_row_ptr < m_row_end && i_row_ptr < i_row_end) {
                const auto sparsity_col = i_cols[i_row_ptr];
                const auto m_col = m_cols[m_row_ptr];
                if (sparsity_col == m_col) {
                    trisystem[idx] = m_vals[m_row_ptr];
                    ++m_row_ptr;
                    ++i_row_ptr;
                    ++idx;
                } else if (m_col < sparsity_col) {
                    ++m_row_ptr;
                } else {  // element not present -> leave value at 0
                    ++i_row_ptr;
                    ++idx;
                }
            }
        }

        rhs.clear();
        rhs.reserve(i_row_elems);

        trs_solve(i_row_elems, trisystem, rhs);

        // Drop RHS as a row to memory (since that is the computed inverse)
        for (IndexType i = 0; i < i_row_elems; ++i) {
            i_vals[i_row_begin + i] = rhs[i];
        }
    }
}


template <typename ValueType, typename IndexType>
void generate_l(std::shared_ptr<const DefaultExecutor> exec,
                const matrix::Csr<ValueType, IndexType> *l_csr,
                matrix::Csr<ValueType, IndexType> *inverse_l)
{
    auto trs_solve = [](IndexType size, std::vector<ValueType> &trisystem,
                        std::vector<ValueType> &rhs) {
        // RHS is the identity: zero everywhere except for the last entry
        // since that is the row we are searching the inverse for
        for (IndexType d_row = 0; d_row < size - 1; ++d_row) {
            rhs.push_back(zero<ValueType>());
        }
        rhs.push_back(one<ValueType>());

        // Note: `trisystem` is an upper triangular matrix here (stored in
        //       colum major)
        for (IndexType d_col = size - 1; d_col >= 0; --d_col) {
            const auto diag = trisystem[d_col * size + d_col];
            const auto bot = rhs[d_col] / diag;
            rhs[d_col] = bot;
            // do a backwards substitution
            for (IndexType d_row = d_col - 1; d_row >= 0; --d_row) {
                rhs[d_row] -= bot * trisystem[d_col * size + d_row];
            }
        }
    };

    generic_generate(exec, l_csr, inverse_l, trs_solve);
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_ISAI_GENERATE_L_KERNEL);


template <typename ValueType, typename IndexType>
void generate_u(std::shared_ptr<const DefaultExecutor> exec,
                const matrix::Csr<ValueType, IndexType> *u_csr,
                matrix::Csr<ValueType, IndexType> *inverse_u)
{
    auto trs_solve = [](IndexType size, std::vector<ValueType> &trisystem,
                        std::vector<ValueType> &rhs) {
        // RHS is the identity: zero everywhere except for the first entry
        // since that is the row we are searching the inverse for
        rhs.push_back(one<ValueType>());
        for (IndexType d_row = 1; d_row < size; ++d_row) {
            rhs.push_back(zero<ValueType>());
        }

        // Note: `trisystem` is a lower triangular matrix here (stored in
        //       colum major)
        for (IndexType d_col = 0; d_col < size; ++d_col) {
            const auto diag = trisystem[d_col * size + d_col];
            const auto top = rhs[d_col] / diag;
            rhs[d_col] = top;
            // do a forward substitution
            for (IndexType d_row = d_col + 1; d_row < size; ++d_row) {
                rhs[d_row] -= top * trisystem[d_col * size + d_row];
            }
        }
    };
    generic_generate(exec, u_csr, inverse_u, trs_solve);
}

GKO_INSTANTIATE_FOR_EACH_VALUE_AND_INDEX_TYPE(
    GKO_DECLARE_ISAI_GENERATE_U_KERNEL);


}  // namespace isai
}  // namespace reference
}  // namespace kernels
}  // namespace gko

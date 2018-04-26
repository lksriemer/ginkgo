/*******************************<GINKGO LICENSE>******************************
Copyright 2017-2018

Karlsruhe Institute of Technology
Universitat Jaume I
University of Tennessee

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************<GINKGO LICENSE>*******************************/

#include "core/solver/bicgstab.hpp"


#include "core/base/exception.hpp"
#include "core/base/exception_helpers.hpp"
#include "core/base/executor.hpp"
#include "core/base/math.hpp"
#include "core/base/utils.hpp"
#include "core/solver/bicgstab_kernels.hpp"


namespace gko {
namespace solver {
namespace {


template <typename ValueType>
struct TemplatedOperation {
    GKO_REGISTER_OPERATION(initialize, bicgstab::initialize<ValueType>);
    GKO_REGISTER_OPERATION(test_convergence,
                           bicgstab::test_convergence<ValueType>);
    GKO_REGISTER_OPERATION(test_convergence_2,
                           bicgstab::test_convergence_2<ValueType>);
    GKO_REGISTER_OPERATION(step_1, bicgstab::step_1<ValueType>);
    GKO_REGISTER_OPERATION(step_2, bicgstab::step_2<ValueType>);
    GKO_REGISTER_OPERATION(step_3, bicgstab::step_3<ValueType>);
};


}  // namespace


template <typename ValueType>
void Bicgstab<ValueType>::apply_impl(const LinOp *b, LinOp *x) const
{
    using std::swap;
    using Vector = matrix::Dense<ValueType>;

    auto exec = this->get_executor();

    auto one_op = initialize<Vector>({one<ValueType>()}, exec);
    auto neg_one_op = initialize<Vector>({-one<ValueType>()}, exec);

    auto dense_b = as<Vector>(b);
    auto dense_x = as<Vector>(x);
    auto r = Vector::create_with_config_of(dense_b);
    auto z = Vector::create_with_config_of(dense_b);
    auto y = Vector::create_with_config_of(dense_b);
    auto v = Vector::create_with_config_of(dense_b);
    auto s = Vector::create_with_config_of(dense_b);
    auto t = Vector::create_with_config_of(dense_b);
    auto p = Vector::create_with_config_of(dense_b);
    auto rr = Vector::create_with_config_of(dense_b);

    auto alpha = Vector::create(exec, dim{1, dense_b->get_size().num_cols});
    auto beta = Vector::create_with_config_of(alpha.get());
    auto gamma = Vector::create_with_config_of(alpha.get());
    auto prev_rho = Vector::create_with_config_of(alpha.get());
    auto rho = Vector::create_with_config_of(alpha.get());
    auto omega = Vector::create_with_config_of(alpha.get());
    auto tau = Vector::create_with_config_of(alpha.get());
    auto starting_tau = Vector::create_with_config_of(tau.get());

    Array<bool> converged(alpha->get_executor(), dense_b->get_size().num_cols);

    // TODO: replace this with automatic merged kernel generator
    exec->run(TemplatedOperation<ValueType>::make_initialize_operation(
        dense_b, r.get(), rr.get(), y.get(), s.get(), t.get(), z.get(), v.get(),
        p.get(), prev_rho.get(), rho.get(), alpha.get(), beta.get(),
        gamma.get(), omega.get(), &converged));
    // r = dense_b
    // prev_rho = rho = omega = alpha = beta = gamma = 1.0
    // rr = v = s = t = z = y = p = 0
    // converged = false

    system_matrix_->apply(neg_one_op.get(), dense_x, one_op.get(), r.get());
    rr->copy_from(r.get());
    r->compute_dot(r.get(), tau.get());
    starting_tau->copy_from(tau.get());
    system_matrix_->apply(r.get(), v.get());
    for (int iter = 0; iter < parameters_.max_iters; ++iter) {
        r->compute_dot(r.get(), tau.get());
        bool all_converged;
        exec->run(
            TemplatedOperation<ValueType>::make_test_convergence_operation(
                tau.get(), starting_tau.get(), parameters_.rel_residual_goal,
                &converged, &all_converged));
        if (all_converged) {
            break;
        }

        rr->compute_dot(r.get(), rho.get());

        exec->run(TemplatedOperation<ValueType>::make_step_1_operation(
            r.get(), p.get(), v.get(), rho.get(), prev_rho.get(), alpha.get(),
            omega.get(), converged));
        // tmp = rho / prev_rho * alpha / omega
        // p = r + tmp * (p - omega * v)

        preconditioner_->apply(p.get(), y.get());
        system_matrix_->apply(y.get(), v.get());
        rr->compute_dot(v.get(), beta.get());
        exec->run(TemplatedOperation<ValueType>::make_step_2_operation(
            r.get(), s.get(), v.get(), rho.get(), alpha.get(), beta.get(),
            converged));
        // alpha = rho / beta
        // s = r - alpha * v

        exec->run(
            TemplatedOperation<ValueType>::make_test_convergence_2_operation(
                s.get(), rel_residual_goal_, alpha.get(), y.get(), dense_x,
                &converged, &all_converged));

        if (all_converged) {
            break;
        }
        if (++iter == parameters_.max_iters) {
            dense_x->add_scaled(alpha.get(), y.get());
            break;
        }
        preconditioner_->apply(s.get(), z.get());
        system_matrix_->apply(z.get(), t.get());
        s->compute_dot(t.get(), gamma.get());
        t->compute_dot(t.get(), beta.get());
        exec->run(TemplatedOperation<ValueType>::make_step_3_operation(
            dense_x, r.get(), s.get(), t.get(), y.get(), z.get(), alpha.get(),
            beta.get(), gamma.get(), omega.get(), converged));
        // omega = gamma / beta
        // x = x + alpha * y + omega * z
        // r = s - omega * t
        swap(prev_rho, rho);
    }
}


template <typename ValueType>
void Bicgstab<ValueType>::apply_impl(const LinOp *alpha, const LinOp *b,
                                     const LinOp *beta, LinOp *x) const
{
    auto dense_x = as<matrix::Dense<ValueType>>(x);
    auto x_clone = dense_x->clone();
    this->apply(b, x_clone.get());
    dense_x->scale(beta);
    dense_x->add_scaled(alpha, x_clone.get());
}


#define GKO_DECLARE_BICGSTAB(_type) class Bicgstab<_type>
GKO_INSTANTIATE_FOR_EACH_VALUE_TYPE(GKO_DECLARE_BICGSTAB);
#undef GKO_DECLARE_BICGSTAB


}  // namespace solver
}  // namespace gko

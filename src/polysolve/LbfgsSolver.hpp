#pragma once

#include <polysolve/LinearSolver.hpp>
#include <polysolve/NLProblem.hpp>
#include <polysolve/MatrixUtils.hpp>
#include <polysolve/State.hpp>

#include <polysolve/Logger.hpp>

#include <cppoptlib/problem.h>
#include <cppoptlib/solver/isolver.h>
#include <cppoptlib/linesearch/armijo.h>
#include <cppoptlib/linesearch/morethuente.h>
#include <Eigen/Sparse>
#include <Eigen/LU>
#include <iostream>

namespace cppoptlib
{

    template <typename ProblemType>
    class LbfgsSolverL2 : public ISolver<ProblemType, 1>
    {
    public:
        using Superclass = ISolver<ProblemType, 1>;
        using typename Superclass::Scalar;
        using typename Superclass::THessian;
        using typename Superclass::TVector;
        using MatrixType = Eigen::Matrix<Scalar, Eigen::Dynamic, Eigen::Dynamic>;

        enum class LineSearch
        {
            Armijo,
            MoreThuente,
        };

        LineSearch line_search = LineSearch::Armijo;

        void setLineSearch(const std::string &name)
        {
            if (name == "armijo")
            {
                line_search = LineSearch::Armijo;
            }
            else if (name == "more_thuente")
            {
                line_search = LineSearch::MoreThuente;
            }
            else
            {
                throw std::invalid_argument("[SparseNewtonDescentSolver] Unknown line search.");
            }
            polysolve::logger().debug("\tline search {}", name);
        }

        void minimize(ProblemType &objFunc, TVector &x0)
        {
            polysolve::AssemblerUtils::instance().clear_cache();

            const size_t m = 10;
            const size_t DIM = x0.rows();
            MatrixType sVector = MatrixType::Zero(DIM, m);
            MatrixType yVector = MatrixType::Zero(DIM, m);
            Eigen::Matrix<Scalar, Eigen::Dynamic, 1> alpha = Eigen::Matrix<Scalar, Eigen::Dynamic, 1>::Zero(m);
            TVector grad(DIM), q(DIM), grad_old(DIM), s(DIM), y(DIM);
            objFunc.gradient(x0, grad);
            TVector x_old = x0;
            TVector x_old2 = x0;

            size_t iter = 0, globIter = 0;
            Scalar H0k = 1;
            this->m_current.reset();
            do
            {
                const Scalar relativeEpsilon = static_cast<Scalar>(0.0001) * std::max(static_cast<Scalar>(1.0), x0.norm());

                if (grad.norm() < relativeEpsilon)
                    break;

                // Algorithm 7.4 (L-BFGS two-loop recursion)
                q = grad;
                const int k = std::min(m, iter);

                // for i = k − 1, k − 2, . . . , k − m§
                for (int i = k - 1; i >= 0; i--)
                {
                    // alpha_i <- rho_i*s_i^T*q
                    const double rho = 1.0 / static_cast<TVector>(sVector.col(i)).dot(static_cast<TVector>(yVector.col(i)));
                    alpha(i) = rho * static_cast<TVector>(sVector.col(i)).dot(q);
                    // q <- q - alpha_i*y_i
                    q = q - alpha(i) * yVector.col(i);
                }
                // r <- H_k^0*q
                q = H0k * q;
                // for i k − m, k − m + 1, . . . , k − 1
                for (int i = 0; i < k; i++)
                {
                    // beta <- rho_i * y_i^T * r
                    const Scalar rho = 1.0 / static_cast<TVector>(sVector.col(i)).dot(static_cast<TVector>(yVector.col(i)));
                    const Scalar beta = rho * static_cast<TVector>(yVector.col(i)).dot(q);
                    // r <- r + s_i * ( alpha_i - beta)
                    q = q + sVector.col(i) * (alpha(i) - beta);
                }
                // stop with result "H_k*f_f'=q"

                // any issues with the descent direction ?
                Scalar descent = -grad.dot(q);
                Scalar alpha_init = 1.0 / grad.norm();
                if (descent > -0.0001 * relativeEpsilon)
                {
                    q = -1 * grad;
                    iter = 0;
                    alpha_init = 1.0;
                }

                // find steplength
                // const Scalar rate = MoreThuente<ProblemType, 1>::linesearch(x0, -q,  objFunc, alpha_init) ;
                Scalar rate;
                switch (line_search)
                {
                case LineSearch::Armijo:
                    rate = Armijo<ProblemType, 1>::linesearch(x0, -q, objFunc);
                    break;
                case LineSearch::MoreThuente:
                    rate = MoreThuente<ProblemType, 1>::linesearch(x0, -q, objFunc);
                    break;
                }
                // update guess
                x0 = x0 - rate * q;

                grad_old = grad;
                objFunc.gradient(x0, grad);

                s = x0 - x_old;
                y = grad - grad_old;

                // update the history
                if (iter < m)
                {
                    sVector.col(iter) = s;
                    yVector.col(iter) = y;
                }
                else
                {

                    sVector.leftCols(m - 1) = sVector.rightCols(m - 1).eval();
                    sVector.rightCols(1) = s;
                    yVector.leftCols(m - 1) = yVector.rightCols(m - 1).eval();
                    yVector.rightCols(1) = y;
                }
                // update the scaling factor
                H0k = y.dot(s) / static_cast<double>(y.dot(y));

                x_old = x0;
                polysolve::logger().debug("\titer: {}, f = {}, ‖g‖_2 = {}", globIter, objFunc.value(x0), grad.norm());

                iter++;
                globIter++;
                ++this->m_current.iterations;
                this->m_current.gradNorm = grad.norm(); // template lpNorm<Eigen::Infinity>();
                this->m_status = checkConvergence(this->m_stop, this->m_current);
            } while ((objFunc.callback(this->m_current, x0)) && (this->m_status == Status::Continue));
        }
    };

} // namespace cppoptlib

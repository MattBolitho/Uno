// Copyright (c) 2018-2024 Charlie Vanaret
// Licensed under the MIT license. See LICENSE file in the project directory for details.

#include <gtest/gtest.h>
#include "linear_algebra/COOSymmetricMatrix.hpp"
#include "solvers/linear/MUMPSSolver.hpp"

const size_t n = 5;
const size_t nnz = 7;
const std::array<double, n> reference{1., 2., 3., 4., 5.};
const double tolerance = 1e-8;

COOSymmetricMatrix<size_t, double> create_my_matrix() {
   COOSymmetricMatrix<size_t, double> matrix(n, nnz, false);
   matrix.insert(2., 0, 0);
   matrix.insert(3., 0, 1);
   matrix.insert(4., 1, 2);
   matrix.insert(6., 1, 4);
   matrix.insert(1., 2, 2);
   matrix.insert(5., 2, 3);
   matrix.insert(1., 4, 4);
   return matrix;
}

Vector<double> create_my_rhs() {
   Vector<double> rhs{8., 45., 31., 15., 17.};
   return rhs;
}

TEST(MUMPSSolver, SystemSize5) {
   const COOSymmetricMatrix<size_t, double> matrix = create_my_matrix();
   const Vector<double> rhs = create_my_rhs();
   Vector<double> result(n);
   result.fill(0.);

   MUMPSSolver solver(n, nnz);
   solver.do_symbolic_factorization(matrix);
   solver.do_numerical_factorization(matrix);
   solver.solve_indefinite_system(matrix, rhs, result);

   for (size_t index: Range(n)) {
      EXPECT_NEAR(result[index], reference[index], tolerance);
   }

}
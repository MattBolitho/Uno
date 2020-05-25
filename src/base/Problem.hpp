#ifndef PROBLEM_H
#define PROBLEM_H

#include <string>
#include <vector>
#include <map>
#include "Constraint.hpp"
#include "Matrix.hpp"

enum FunctionType {
    LINEAR = 0, /*!< Linear function */
    QUADRATIC, /*!< Quadratic function */
    NONLINEAR /*!< Nonlinear function */
};

/*! \class Problem
 * \brief Optimization problem
 *
 *  Description of an optimization problem
 */
class Problem {
public:
    Problem(std::string name, int number_variables, int number_constraints);
    virtual ~Problem();

    std::string name;
    int number_variables; /*!< Number of variables */
    int number_constraints; /*!< Number of constraints */
    
    /* objective */
    double objective_sign; /*!< Sign of the objective function (1: minimization, -1: maximization) */
    std::string objective_name;
    FunctionType objective_type; /*!< Type of the objective (LINEAR, QUADRATIC, NONLINEAR) */
    std::map<int, double> objective_variables;
    virtual double objective(std::vector<double>& x) = 0;
    virtual std::vector<double> objective_dense_gradient(std::vector<double>& x) = 0;
    virtual std::map<int, double> objective_sparse_gradient(std::vector<double>& x) = 0;

    /* variables */
    std::vector<std::string> variable_name;
    std::vector<bool> variable_discrete;
    std::vector<Range> variables_bounds;
    std::vector<ConstraintType> variable_status; /*!< Status of the variables (EQUALITY, BOUNDED_LOWER, BOUNDED_UPPER, BOUNDED_BOTH_SIDES) */

    /* constraints */
    std::vector<std::string> constraint_name;
    std::vector<std::map<int, double> > constraint_variables;
    std::vector<Range> constraint_bounds;
    std::vector<FunctionType> constraint_type; /*!< Types of the constraints (LINEAR, QUADRATIC, NONLINEAR) */
    std::vector<ConstraintType> constraint_status; /*!< Status of the constraints (EQUAL_BOUNDS, BOUNDED_LOWER, BOUNDED_UPPER, BOUNDED_BOTH_SIDES, UNBOUNDED) */
    virtual double evaluate_constraint(int j, std::vector<double>& x) = 0;
    virtual std::vector<double> evaluate_constraints(std::vector<double>& x) = 0;
    virtual std::vector<double> constraint_dense_gradient(int j, std::vector<double>& x) = 0;
    virtual void constraint_sparse_gradient(std::vector<double>& x, int j, std::map<int, double>& gradient) = 0;
    virtual std::vector<std::map<int, double> > constraints_sparse_jacobian(std::vector<double>& x) = 0;
    void determine_bounds_types(std::vector<Range>& variables_bounds, std::vector<ConstraintType>& status);
    std::map<int, int> equality_constraints; /*!< inequality constraints */
    std::map<int, int> inequality_constraints; /*!< inequality constraints */
    std::map<int, int> linear_constraints;

    std::vector<int> jacobian_sparsity;

    /* Hessian */
    int hessian_maximum_number_nonzeros; /*!< Number of nonzero elements in the Hessian */
    //std::vector<int> hessian_column_start; /*!< Column description of sparse Hessian */
    //std::vector<int> hessian_row_number; /*!< Row description of sparse Hessian */
    virtual CSCMatrix lagrangian_hessian(std::vector<double>& x, double objective_multiplier, std::vector<double>& multipliers) = 0;

    virtual std::vector<double> primal_initial_solution() = 0;
    virtual std::vector<double> dual_initial_solution() = 0;

    //double compute_constraint_residual(ConstraintPartition& constraint_partition, std::vector<double>& constraints, std::string norm_value);
    //double compute_constraint_residual(ConstraintPartition& constraint_partition, std::vector<double>& constraints, std::string norm_value);
    double compute_constraint_residual(std::vector<double>& constraints, std::string norm_value);
    double compute_constraint_residual(std::vector<double>& constraints, std::set<int> constraint_set, std::string norm_value);

    int number_eval_objective;
    int number_eval_constraints;
    int number_eval_jacobian;
    int number_eval_hessian;

protected:
    void determine_constraints_();
};

#endif // PROBLEM_H

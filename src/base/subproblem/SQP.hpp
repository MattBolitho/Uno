#ifndef SQP_H
#define SQP_H

#include "ActiveSetMethod.hpp"
#include "HessianEvaluation.hpp"

class SQP : public ActiveSetMethod {
public:
    SQP(Problem& problem, std::string QP_solver_name, std::string hessian_evaluation_method, bool use_trust_region, bool scale_residuals);

    SubproblemSolution compute_step(Problem& problem, Iterate& current_iterate, double trust_region_radius = INFINITY) override;
    SubproblemSolution restore_feasibility(Problem& problem, Iterate& current_iterate, SubproblemSolution& phase_II_solution, double trust_region_radius = INFINITY) override;
    
    /* use references to allow polymorphism */
    std::shared_ptr<HessianEvaluation> hessian_evaluation; /*!< Strategy to compute or approximate the Hessian */

protected:
    void evaluate_optimality_iterate_(Problem& problem, Iterate& current_iterate);
    void evaluate_feasibility_iterate_(Problem& problem, Iterate& current_iterate, ConstraintPartition& constraint_partition);
};

#endif // SQP_H

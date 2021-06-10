#include <cmath>
#include "Uno.hpp"
#include "l1Penalty.hpp"
#include "Logger.hpp"

/*
 * Infeasibility detection and SQP methods for nonlinear optimization 
 * http://epubs.siam.org/doi/pdf/10.1137/080738222
 */

l1Penalty::l1Penalty(ConstraintRelaxationStrategy& feasibility_strategy, Subproblem& subproblem) :
   GlobalizationStrategy(feasibility_strategy, subproblem), decrease_fraction_(1e-8) {
}

Iterate l1Penalty::initialize(Statistics& statistics, Problem& problem, std::vector<double>& x, Multipliers& multipliers) {
   statistics.add_column("penalty param.", Statistics::double_width, 4);
   /* initialize the subproblem */
   Iterate first_iterate = this->subproblem.evaluate_initial_point(problem, x, multipliers);
   this->subproblem.compute_residuals(problem, first_iterate, first_iterate.multipliers, 1.);
   // preallocate trial_iterate
   this->trial_primals_.resize(first_iterate.x.size());
   return first_iterate;
}

std::optional<Iterate> l1Penalty::check_acceptance(Statistics& statistics, Problem& problem, Iterate& current_iterate, Direction& direction,
      double step_length) {
   /* check if subproblem definition changed */
   if (this->subproblem.subproblem_definition_changed) {
      this->subproblem.subproblem_definition_changed = false;
      this->subproblem.compute_optimality_measures(problem, current_iterate);
   }

   /* generate the trial point */
   add_vectors(current_iterate.x, direction.x, step_length, this->trial_primals_);
   Iterate trial_iterate(this->trial_primals_, direction.multipliers);
   double step_norm = step_length * direction.norm;
   this->subproblem.compute_optimality_measures(problem, trial_iterate);

   bool accept = false;
   /* check zero step */
   if (step_norm == 0.) {
      accept = true;
   }
   else {
      /* compute current exact l1 penalty: rho f + ||c|| */
      double current_exact_l1_penalty =
            direction.objective_multiplier * current_iterate.progress.objective + current_iterate.progress.feasibility;
      double trial_exact_l1_penalty = direction.objective_multiplier * trial_iterate.progress.objective + trial_iterate.progress.feasibility;

      /* check the validity of the trial step */
      double predicted_reduction = direction.predicted_reduction(step_length);
      double actual_reduction = current_exact_l1_penalty - trial_exact_l1_penalty;

      DEBUG << "Current: η = " << current_iterate.progress.feasibility << ", ω = " << current_iterate.progress.objective << "\n";
      DEBUG << "Trial: η = " << trial_iterate.progress.feasibility << ", ω = " << trial_iterate.progress.objective << "\n";
      DEBUG << "Predicted reduction: " << predicted_reduction << ", actual: " << actual_reduction << "\n\n";

      // Armijo sufficient decrease condition
      if (actual_reduction >= this->decrease_fraction_ * predicted_reduction) {
         accept = true;
      }
   }

   if (accept) {
      statistics.add_statistic("penalty param.", direction.objective_multiplier);
      return std::optional<Iterate>{trial_iterate};
   }
   else {
      return std::nullopt;
   }
}
// Copyright (c) 2022 Charlie Vanaret
// Licensed under the MIT license. See LICENSE file in the project directory for details.

#include <cassert>
#include <functional>
#include "FeasibilityRestoration.hpp"
#include "ingredients/globalization_strategy/GlobalizationStrategyFactory.hpp"
#include "ingredients/subproblem/SubproblemFactory.hpp"

FeasibilityRestoration::FeasibilityRestoration(const Model& model, const Options& options) :
      ConstraintRelaxationStrategy(model, options),
      // create the (optimality phase) optimality problem (= original model)
      optimality_problem(model),
      // create the (restoration phase) feasibility problem (objective multiplier = 0)
      feasibility_problem(model, 0.),
      subproblem(SubproblemFactory::create(this->feasibility_problem.number_variables, this->feasibility_problem.number_constraints,
            this->feasibility_problem.get_maximum_number_hessian_nonzeros(), options)),
      // create the globalization strategies (one for each phase)
      restoration_phase_strategy(GlobalizationStrategyFactory::create(options.get_string("strategy"), options)),
      optimality_phase_strategy(GlobalizationStrategyFactory::create(options.get_string("strategy"), options)),
      statistics_restoration_phase_column_order(options.get_int("statistics_restoration_phase_column_order")) {
}

void FeasibilityRestoration::initialize(Statistics& statistics, Iterate& first_iterate) {
   statistics.add_column("phase", Statistics::int_width, this->statistics_restoration_phase_column_order);

   // initialize the subproblem
   this->subproblem->initialize(statistics, this->optimality_problem, first_iterate);

   // compute the progress measures of the initial point
   this->set_infeasibility_measure(first_iterate);
   this->set_scaled_optimality_measure(first_iterate);
   this->subproblem->set_unscaled_optimality_measure(this->optimality_problem, first_iterate);

   // compute the residuals of the initial point
   ConstraintRelaxationStrategy::evaluate_functions(this->optimality_problem, first_iterate);
   this->compute_primal_dual_residuals(this->optimality_problem, first_iterate);

   // initialize the globalization strategies
   this->restoration_phase_strategy->initialize(first_iterate);
   this->optimality_phase_strategy->initialize(first_iterate);
}

Direction FeasibilityRestoration::compute_feasible_direction(Statistics& statistics, Iterate& current_iterate) {
   DEBUG << "Current iterate\n" << current_iterate << '\n';
   if (this->current_phase == Phase::OPTIMALITY) {
      return this->solve_optimality_problem(statistics, current_iterate);
   }
   else {
      return this->solve_feasibility_problem(statistics, current_iterate);
   }
}

Direction FeasibilityRestoration::solve_optimality_problem(Statistics& statistics, Iterate& current_iterate) {
   // solve the subproblem
   DEBUG << "Solving the optimality subproblem\n";
   Direction direction = this->subproblem->solve(statistics, this->optimality_problem, current_iterate);
   direction.objective_multiplier = 1.;
   direction.norm = norm_inf(direction.primals, Range(this->optimality_problem.number_variables));
   DEBUG << direction << '\n';

   // infeasible subproblem: try to minimize the constraint violation by solving the feasibility subproblem
   if (direction.status == SubproblemStatus::INFEASIBLE) {
      direction = this->solve_feasibility_problem(statistics, current_iterate, direction.primals);
   }
   return direction;
}

// form and solve the feasibility problem
Direction FeasibilityRestoration::solve_feasibility_problem(Statistics& statistics, Iterate& current_iterate) {
   this->subproblem->initialize_feasibility_problem(current_iterate);
   this->subproblem->set_elastic_variable_values(this->feasibility_problem, current_iterate);
   if (this->current_phase == Phase::OPTIMALITY) {
      this->switch_to_feasibility_restoration(current_iterate);
   }

   DEBUG << "Solving the feasibility subproblem\n";
   Direction direction = this->subproblem->solve(statistics, this->feasibility_problem, current_iterate);
   direction.objective_multiplier = 0.;
   direction.norm = norm_inf(direction.primals, Range(this->optimality_problem.number_variables));
   DEBUG << direction << '\n';
   assert(direction.status == SubproblemStatus::OPTIMAL && "The feasibility subproblem was not solved to optimality");
   return direction;
}

// form and solve the feasibility problem (with an initial point)
Direction FeasibilityRestoration::solve_feasibility_problem(Statistics& statistics, Iterate& current_iterate, const std::vector<double>& initial_point) {
   this->subproblem->set_initial_point(initial_point);
   return this->solve_feasibility_problem(statistics, current_iterate);
}

Direction FeasibilityRestoration::compute_second_order_correction(Iterate& trial_iterate) {
   // evaluate the constraints for the second-order correction
   this->current_reformulated_problem().evaluate_constraints(trial_iterate, trial_iterate.reformulation_evaluations.constraints);
   Direction soc_direction = this->subproblem->compute_second_order_correction(this->current_reformulated_problem(), trial_iterate);
   soc_direction.objective_multiplier = 1.;
   soc_direction.norm = norm_inf(soc_direction.primals, Range(this->optimality_problem.number_variables));
   DEBUG << soc_direction << '\n';
   return soc_direction;
}

void FeasibilityRestoration::compute_progress_measures(Iterate& current_iterate, Iterate& trial_iterate, const Direction& direction) {
   // refresh the unscaled optimality measures for the current iterate
   if (this->subproblem->unscaled_optimality_measure_changed) {
      DEBUG << "The subproblem definition changed, the unscaled optimality measure is recomputed\n";
      this->restoration_phase_strategy->reset();
      this->optimality_phase_strategy->reset();
      this->subproblem->unscaled_optimality_measure_changed = false;
   }

   // possibly go from restoration phase to optimality phase
   if (this->current_phase == Phase::FEASIBILITY_RESTORATION &&
         ConstraintRelaxationStrategy::compute_linearized_constraint_violation(this->original_model, current_iterate, direction, 1.) == 0.) {
      // evaluate measure of infeasibility (in restoration phase definition, it corresponds to the "scaled optimality" quantity)
      this->set_scaled_optimality_measure(trial_iterate);
      // if the infeasibility improves upon the best known infeasibility of the globalization strategy
      if (this->optimality_phase_strategy->is_infeasibility_acceptable(trial_iterate.nonlinear_progress.scaled_optimality(1.))) {
         this->switch_to_optimality(current_iterate, trial_iterate);
      }
   }

   // evaluate the progress measures of the trial iterate
   this->set_infeasibility_measure(trial_iterate);
   this->set_scaled_optimality_measure(trial_iterate);
   this->subproblem->set_unscaled_optimality_measure(this->current_reformulated_problem(), current_iterate);
   this->subproblem->set_unscaled_optimality_measure(this->current_reformulated_problem(), trial_iterate);
}

void FeasibilityRestoration::switch_to_feasibility_restoration(Iterate& current_iterate) {
   DEBUG << "Switching from optimality to restoration phase\n";
   this->current_phase = Phase::FEASIBILITY_RESTORATION;
   this->optimality_phase_strategy->register_current_progress(current_iterate.nonlinear_progress);
   // refresh the progress measures of the current iterate
   this->set_infeasibility_measure(current_iterate);
   this->set_scaled_optimality_measure(current_iterate);
   this->subproblem->set_unscaled_optimality_measure(this->feasibility_problem, current_iterate);
   this->restoration_phase_strategy->reset();
   this->restoration_phase_strategy->register_current_progress(current_iterate.nonlinear_progress);
}

void FeasibilityRestoration::switch_to_optimality(Iterate& current_iterate, Iterate& trial_iterate) {
   DEBUG << "Switching from restoration to optimality phase\n";
   this->current_phase = Phase::OPTIMALITY;
   current_iterate.set_number_variables(this->optimality_problem.number_variables);
   trial_iterate.set_number_variables(this->optimality_problem.number_variables);
   // refresh the progress measures of current iterate
   this->set_scaled_optimality_measure(current_iterate);
   this->set_infeasibility_measure(current_iterate);
   this->subproblem->set_unscaled_optimality_measure(this->optimality_problem, current_iterate);
}

bool FeasibilityRestoration::is_iterate_acceptable(Statistics& statistics, Iterate& current_iterate, Iterate& trial_iterate, const Direction& direction,
      double step_length) {
   this->compute_progress_measures(current_iterate, trial_iterate, direction);

   bool accept = false;
   if (this->is_small_step(direction)) {
      DEBUG << "Small step acceptable\n";
      // in case the objective was not computed, evaluate it
      trial_iterate.evaluate_objective(this->original_model);
      accept = true;
   }
   else {
      // evaluate the predicted reduction
      PredictedReduction predicted_reduction = {
            this->generate_predicted_infeasibility_reduction_model(current_iterate, direction, step_length),
            this->generate_predicted_scaled_optimality_reduction_model(current_iterate, direction, step_length),
            this->subproblem->generate_predicted_unscaled_optimality_reduction_model(this->current_reformulated_problem(), current_iterate, direction,
                  step_length)
      };
      // invoke the globalization strategy for acceptance
      GlobalizationStrategy& current_phase_strategy = this->current_globalization_strategy();
      accept = current_phase_strategy.is_iterate_acceptable(current_iterate.nonlinear_progress, trial_iterate.nonlinear_progress,
            predicted_reduction, this->current_reformulated_problem().get_objective_multiplier());
   }
   if (accept) {
      statistics.add_statistic("phase", static_cast<int>(this->current_phase));
      ConstraintRelaxationStrategy::evaluate_functions(this->current_reformulated_problem(), trial_iterate);
      this->compute_primal_dual_residuals(this->current_reformulated_problem(), trial_iterate);
   }
   return accept;
}

const NonlinearProblem& FeasibilityRestoration::current_reformulated_problem() const {
   if (this->current_phase == Phase::OPTIMALITY) {
      return this->optimality_problem;
   }
   else {
      return this->feasibility_problem;
   }
}

GlobalizationStrategy& FeasibilityRestoration::current_globalization_strategy() const {
   return (this->current_phase == Phase::OPTIMALITY) ? *this->optimality_phase_strategy : *this->restoration_phase_strategy;
}

void FeasibilityRestoration::set_trust_region_radius(double trust_region_radius) {
   this->subproblem->set_trust_region_radius(trust_region_radius);
}

void FeasibilityRestoration::set_infeasibility_measure(Iterate& iterate) {
   if (this->current_phase == Phase::OPTIMALITY) {
      // constraint violation
      iterate.evaluate_constraints(this->original_model);
      iterate.nonlinear_progress.infeasibility = this->original_model.compute_constraint_violation(iterate.model_evaluations.constraints, L1_NORM);
   }
   else {
      // 0
      iterate.nonlinear_progress.infeasibility = 0.;
   }
}

double FeasibilityRestoration::generate_predicted_infeasibility_reduction_model(const Iterate& current_iterate, const Direction& direction,
      double step_length) const {
   if (this->current_phase == Phase::OPTIMALITY) {
      const double current_constraint_violation = this->original_model.compute_constraint_violation(current_iterate.model_evaluations.constraints,
            Norm::L1_NORM);
      const double linearized_constraint_violation = ConstraintRelaxationStrategy::compute_linearized_constraint_violation(this->original_model,
            current_iterate, direction, step_length);
      return current_constraint_violation - linearized_constraint_violation;
      //}, "‖c(x)‖₁ - ‖c(x) + ∇c(x)^T (αd)‖₁"};
   }
   else {
      return 0.;
      //}, "0"};
   }
}

void FeasibilityRestoration::set_scaled_optimality_measure(Iterate& iterate) {
   if (this->current_phase == Phase::OPTIMALITY) {
      // scaled objective
      iterate.evaluate_objective(this->original_model);
      const double objective = iterate.model_evaluations.objective;
      iterate.nonlinear_progress.scaled_optimality = [=](double objective_multiplier) {
         return objective_multiplier*objective;
      };
   }
   else {
      // constraint violation
      iterate.evaluate_constraints(this->original_model);
      const double constraint_violation = this->original_model.compute_constraint_violation(iterate.model_evaluations.constraints, L1_NORM);
      iterate.nonlinear_progress.scaled_optimality = [=](double /*objective_multiplier*/) {
         return constraint_violation;
      };
   }
}

std::function<double (double)> FeasibilityRestoration::generate_predicted_scaled_optimality_reduction_model(const Iterate& current_iterate,
      const Direction& direction, double step_length) const {
   if (this->current_phase == Phase::OPTIMALITY) {
      // precompute expensive quantities
      const double directional_derivative = dot(direction.primals, current_iterate.model_evaluations.objective_gradient);
      return [=](double objective_multiplier) {
         return step_length * (-objective_multiplier*directional_derivative);
      };
      //}, "-∇f(x)^T (αd)"};
   }
   else {
      const double current_constraint_violation = this->original_model.compute_constraint_violation(current_iterate.model_evaluations.constraints,
            Norm::L1_NORM);
      const double linearized_constraint_violation = ConstraintRelaxationStrategy::compute_linearized_constraint_violation(this->original_model,
            current_iterate, direction, step_length);
      return [=](double /*objective_multiplier*/) {
         return current_constraint_violation - linearized_constraint_violation;
      };
      //}, "‖c(x)‖₁ - ‖c(x) + ∇c(x)^T (αd)‖₁"};
   }
}

void FeasibilityRestoration::postprocess_accepted_iterate(Iterate& iterate) {
   this->subproblem->postprocess_accepted_iterate(this->current_reformulated_problem(), iterate);
}

size_t FeasibilityRestoration::get_hessian_evaluation_count() const {
   return this->subproblem->get_hessian_evaluation_count();
}

size_t FeasibilityRestoration::get_number_subproblems_solved() const {
   return this->subproblem->number_subproblems_solved;
}

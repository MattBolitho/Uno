#include <cassert>
#include "l1Relaxation.hpp"
#include "ingredients/strategy/GlobalizationStrategyFactory.hpp"
#include "ingredients/subproblem/SubproblemFactory.hpp"
#include "tools/Range.hpp"

/*
 * Infeasibility detection and SQP methods for nonlinear optimization
 * Richard H. Byrd, Frank E. Curtis and Jorge Nocedal
 * http://epubs.siam.org/doi/pdf/10.1137/080738222
 */

l1Relaxation::l1Relaxation(const Model& model, const Options& options) :
      ConstraintRelaxationStrategy(norm_from_string(options.at("residual_norm"))),
      // create the optimality problem
      optimality_problem(model),
      // create the relaxed problem by introducing elastic variables
      relaxed_problem(model, stod(options.at("l1_relaxation_initial_parameter")), stod(options.at("elastic_objective_coefficient")),
            (options.at("use_proximal_term") == "yes")),
      subproblem(SubproblemFactory::create(this->relaxed_problem, options)),
      globalization_strategy(GlobalizationStrategyFactory::create(options.at("strategy"), options)),
      penalty_parameter(stod(options.at("l1_relaxation_initial_parameter"))),
      parameters({options.at("l1_relaxation_fixed_parameter") == "yes",
                  stod(options.at("l1_relaxation_decrease_factor")),
                  stod(options.at("l1_relaxation_epsilon1")),
                  stod(options.at("l1_relaxation_epsilon2")),
                  stod(options.at("l1_relaxation_small_threshold"))}),
      constraint_multipliers(model.number_constraints),
      lower_bound_multipliers(model.number_variables),
      upper_bound_multipliers(model.number_variables) {
}

void l1Relaxation::initialize(Statistics& statistics, Iterate& first_iterate) {
   statistics.add_column("penalty param.", Statistics::double_width, 4);

   this->subproblem->set_elastic_variables(this->relaxed_problem, first_iterate);
   // initialize the subproblem
   this->subproblem->initialize(statistics, this->relaxed_problem, first_iterate);

   // compute the progress measures and the residuals of the initial point
   first_iterate.nonlinear_progress.infeasibility = this->compute_infeasibility_measure(first_iterate);
   first_iterate.nonlinear_progress.objective = this->subproblem->compute_optimality_measure(this->optimality_problem, first_iterate);

   this->compute_nonlinear_residuals(this->optimality_problem.model, first_iterate);

   // initialize the globalization strategy
   this->globalization_strategy->initialize(statistics, first_iterate);
}

void l1Relaxation::set_multipliers(const Iterate& current_iterate, std::vector<double>& current_constraint_multipliers) {
   // the values {1, -1} are derived from the KKT conditions of the l1 problem
   for (size_t j = 0; j < this->optimality_problem.number_constraints; j++) {
      if (current_iterate.original_evaluations.constraints[j] < this->optimality_problem.get_constraint_lower_bound(j)) { // lower bound infeasible
         current_constraint_multipliers[j] = 1.;
      }
      else if (this->optimality_problem.get_constraint_upper_bound(j) < current_iterate.original_evaluations.constraints[j]) { // upper bound infeasible
         current_constraint_multipliers[j] = -1.;
      }
   }
   // otherwise, leave the multiplier as it is
}

Direction l1Relaxation::compute_feasible_direction(Statistics& statistics, Iterate& current_iterate) {
   // set the elastic variables
   this->subproblem->set_elastic_variables(this->relaxed_problem, current_iterate);

   // set the proximal coefficient
   this->relaxed_problem.set_proximal_coefficient(std::sqrt(this->penalty_parameter));
   this->relaxed_problem.set_proximal_reference_point(current_iterate.x);

   // set the multipliers of the violated constraints
   // this->set_multipliers(current_iterate, current_iterate.multipliers.constraints);
   DEBUG << "Current iterate\n" << current_iterate << "\n";

   // use Byrd's steering rules to update the penalty parameter and compute descent directions
   Direction direction = this->solve_with_steering_rule(statistics, current_iterate);
   return direction;
}

Direction l1Relaxation::solve_subproblem(Statistics& statistics, Iterate& current_iterate, double current_penalty_parameter) {
   // update the objective model using the current penalty parameter
   DEBUG << "penalty parameter: " << current_penalty_parameter << "\n\n";
   this->relaxed_problem.set_objective_multiplier(current_penalty_parameter);

   // solve the subproblem
   Direction direction = this->subproblem->solve(statistics, this->relaxed_problem, current_iterate);
   direction.objective_multiplier = current_penalty_parameter;
   direction.norm = norm_inf(direction.x, Range(this->optimality_problem.number_variables));
   DEBUG << "\n" << direction << "\n";
   assert(direction.status == OPTIMAL && "The subproblem was not solved to optimality");
   // check feasibility (the subproblem is, by construction, always feasible)
   if (direction.constraint_partition.has_value()) {
      const ConstraintPartition& constraint_partition = direction.constraint_partition.value();
      assert(constraint_partition.infeasible.empty() && "solve_subproblem: infeasible constraints found, although direction is feasible");
   }
   return direction;
}

Direction l1Relaxation::solve_feasibility_problem(Statistics& statistics, Iterate& current_iterate,
      const std::optional<std::vector<double>>& /*optional_phase_2_solution*/) {
   assert(0. < this->penalty_parameter && "l1Relaxation: the penalty parameter is already 0");
   Direction direction = this->solve_subproblem(statistics, current_iterate, 0.);
   return direction;
}

Direction l1Relaxation::solve_with_steering_rule(Statistics& statistics, Iterate& current_iterate) {
   // stage a: compute the step within trust region
   Direction direction = this->solve_subproblem(statistics, current_iterate, this->penalty_parameter);

   // penalty update: if penalty parameter is already 0 or fixed by the user, no need to decrease it
   if (0. < this->penalty_parameter && !this->parameters.fixed_parameter) {
      // check infeasibility
      double linearized_residual = this->relaxed_problem.compute_linearized_constraint_violation(current_iterate.x, direction.x);
      DEBUG << "Linearized residual mk(dk): " << linearized_residual << "\n\n";

      // if the current direction is already feasible, terminate
      if (0. < linearized_residual) {
         const double current_penalty_parameter = this->penalty_parameter;

         // stage c: compute the lowest possible constraint violation (penalty parameter = 0)
         DEBUG << "Compute ideal solution by solving the feasibility problem:\n";
         Direction direction_lowest_violation = this->solve_subproblem(statistics, current_iterate, 0.);
         const double residual_lowest_violation = this->relaxed_problem
               .compute_linearized_constraint_violation(current_iterate.x, direction_lowest_violation.x);
         DEBUG << "Lowest linearized residual mk(dk): " << residual_lowest_violation << "\n";

         // stage f: update the penalty parameter
         this->decrease_parameter_aggressively(current_iterate, direction_lowest_violation);
         if (this->penalty_parameter == 0.) {
            direction = direction_lowest_violation;
            linearized_residual = residual_lowest_violation;
         }
         else {
            if (this->penalty_parameter < current_penalty_parameter) {
               DEBUG << "Resolving the subproblem (penalty parameter aggressively reduced to " << this->penalty_parameter << ")\n";
               direction = this->solve_subproblem(statistics, current_iterate, this->penalty_parameter);
               linearized_residual = this->relaxed_problem.compute_linearized_constraint_violation(current_iterate.x, direction.x);
            }

            // further decrease penalty parameter to satisfy 2 conditions
            DEBUG << "Further decrease the penalty parameter\n";
            bool condition1 = false, condition2 = false;
            while (!condition2) {
               if (!condition1) {
                  // stage d: reach a fraction of the ideal decrease
                  if (this->linearized_residual_sufficient_decrease(current_iterate, linearized_residual, residual_lowest_violation)) {
                     condition1 = true;
                     DEBUG << "Condition 1 is true\n";
                  }
               }
               // stage e: further decrease penalty parameter if necessary
               if (condition1 && this->objective_sufficient_decrease(current_iterate, direction, direction_lowest_violation)) {
                  condition2 = true;
                  DEBUG << "Condition 2 is true\n";
               }
               if (!condition2) {
                  this->penalty_parameter /= this->parameters.decrease_factor;
                  this->penalty_parameter = std::max(0., this->penalty_parameter - this->parameters.small_threshold);
                  if (this->penalty_parameter == 0.) {
                     direction = direction_lowest_violation;
                     linearized_residual = residual_lowest_violation;
                     condition2 = true;
                  }
                  else {
                     DEBUG << "Resolving the subproblem (penalty parameter reduced to " << this->penalty_parameter << ")\n";
                     direction = this->solve_subproblem(statistics, current_iterate, this->penalty_parameter);
                     linearized_residual = this->relaxed_problem.compute_linearized_constraint_violation(current_iterate.x, direction.x);
                     DEBUG << "Linearized residual mk(dk): " << linearized_residual << "\n\n";
                  }
               }
            }
         }

         if (this->penalty_parameter < current_penalty_parameter) {
            DEBUG << "\nPenalty parameter updated to " << this->penalty_parameter << "\n\n";
         }
      }
   }
   return direction;
}

bool l1Relaxation::linearized_residual_sufficient_decrease(const Iterate& current_iterate, double linearized_residual, double residual_lowest_violation) const {
   if (residual_lowest_violation == 0.) {
      return (linearized_residual == 0.);
   }
   const double linearized_residual_reduction = current_iterate.constraint_violation - linearized_residual;
   const double lowest_linearized_residual_reduction = current_iterate.constraint_violation - residual_lowest_violation;
   return (linearized_residual_reduction >= this->parameters.epsilon1 * lowest_linearized_residual_reduction);
}

void l1Relaxation::decrease_parameter_aggressively(Iterate& current_iterate, const Direction& direction_lowest_violation) {
   // compute the ideal error (with a zero penalty parameter)
   const double error_lowest_violation = l1Relaxation::compute_error(current_iterate, direction_lowest_violation.multipliers);
   DEBUG << "Ideal error: " << error_lowest_violation << "\n";

   const double scaled_error = error_lowest_violation / std::max(1., current_iterate.constraint_violation);
   const double scaled_error_square = scaled_error*scaled_error;
   this->penalty_parameter = std::min(this->penalty_parameter, scaled_error_square);
   this->penalty_parameter = std::max(0., this->penalty_parameter - this->parameters.small_threshold);
}

bool l1Relaxation::objective_sufficient_decrease(const Iterate& current_iterate, const Direction& direction,
      const Direction& direction_lowest_violation) const {
   const double decrease_objective = current_iterate.constraint_violation - direction.objective;
   const double lowest_decrease_objective = current_iterate.constraint_violation - direction_lowest_violation.objective;
   return (decrease_objective >= this->parameters.epsilon2 * lowest_decrease_objective);
}

bool l1Relaxation::is_acceptable(Statistics& statistics, Iterate& current_iterate, Iterate& trial_iterate, const Direction& direction,
      PredictedReductionModel& predicted_reduction_model, double step_length) {
   // check if subproblem definition changed
   if (this->subproblem->subproblem_definition_changed) {
      DEBUG << "The subproblem definition changed, the optimality measure is recomputed\n";
      current_iterate.nonlinear_progress.objective = this->subproblem->compute_optimality_measure(this->optimality_problem, current_iterate);
      this->globalization_strategy->reset();
      this->subproblem->subproblem_definition_changed = false;
   }

   // compute the measures of progress for the trial iterate
   trial_iterate.nonlinear_progress.infeasibility = this->compute_infeasibility_measure(trial_iterate);
   trial_iterate.nonlinear_progress.objective = this->subproblem->compute_optimality_measure(this->optimality_problem, trial_iterate);

   bool accept = false;
   if (ConstraintRelaxationStrategy::is_small_step(direction)) {
      accept = true;
   }
   else {
      // evaluate the predicted reduction
      const double predicted_reduction = l1Relaxation::compute_predicted_reduction(this->optimality_problem.model, current_iterate, direction,
            predicted_reduction_model, step_length);

      // invoke the globalization strategy for acceptance
      accept = this->globalization_strategy->is_acceptable(statistics, current_iterate.nonlinear_progress, trial_iterate.nonlinear_progress,
            this->penalty_parameter, predicted_reduction);
   }
   if (accept) {
      statistics.add_statistic("penalty param.", this->penalty_parameter);
      trial_iterate.evaluate_objective(this->optimality_problem.model);
      this->compute_nonlinear_residuals(this->optimality_problem.model, trial_iterate);
   }
   return accept;
}

double l1Relaxation::compute_predicted_reduction(const Model& model, Iterate& current_iterate,
      const Direction& direction, PredictedReductionModel& predicted_reduction_model, double step_length) {
   // compute the predicted reduction of the l1 relaxation as a postprocessing of the predicted reduction of the subproblem
   if (step_length == 1.) {
      return current_iterate.constraint_violation + predicted_reduction_model.evaluate(step_length);
   }
   else {
      // determine the linearized constraint violation term: c(x_k) + alpha*\nabla c(x_k)^T d
      const auto residual_function = [&](size_t j) {
         const double component_j = current_iterate.original_evaluations.constraints[j] + step_length * dot(direction.x,
               current_iterate.original_evaluations.constraint_jacobian[j]);
         return model.compute_constraint_violation(component_j, j);
      };
      const double linearized_constraint_violation = norm_1(residual_function, Range(model.number_constraints));
      return current_iterate.constraint_violation - linearized_constraint_violation + predicted_reduction_model.evaluate(step_length);
   }
}

// measure that combines KKT error and complementarity error
double l1Relaxation::compute_error(Iterate& current_iterate, const Multipliers& multiplier_displacements) {
   // assemble the trial multipliers
   for (size_t j = 0; j < this->optimality_problem.number_constraints; j++) {
      this->constraint_multipliers[j] = current_iterate.multipliers.constraints[j] + multiplier_displacements.constraints[j];
   }
   for (size_t i = 0; i < this->optimality_problem.number_variables; i++) {
      this->lower_bound_multipliers[i] = current_iterate.multipliers.lower_bounds[i] + multiplier_displacements.lower_bounds[i];
      this->upper_bound_multipliers[i] = current_iterate.multipliers.upper_bounds[i] + multiplier_displacements.upper_bounds[i];
   }

   // complementarity error
   current_iterate.evaluate_constraints(this->optimality_problem.model);
   double error = this->optimality_problem.model.compute_complementarity_error(current_iterate.x, current_iterate.original_evaluations.constraints,
         this->constraint_multipliers, this->lower_bound_multipliers, this->upper_bound_multipliers);
   // KKT error
   current_iterate.evaluate_lagrangian_gradient(this->optimality_problem.model, this->constraint_multipliers, this->lower_bound_multipliers,
         this->upper_bound_multipliers);
   error += norm_1(current_iterate.lagrangian_gradient);
   return error;
}

void l1Relaxation::set_variable_bounds(const Iterate& current_iterate, double trust_region_radius) {
   this->subproblem->set_variable_bounds(this->relaxed_problem, current_iterate, trust_region_radius);
}

Direction l1Relaxation::compute_second_order_correction(Iterate& trial_iterate) {
   return this->subproblem->compute_second_order_correction(this->relaxed_problem, trial_iterate);
}

PredictedReductionModel l1Relaxation::generate_predicted_reduction_model(const Direction& direction) const {
   // the predicted reduction should be that of the original problem. It will then be post-processed in compute_predicted_reduction
   return this->subproblem->generate_predicted_reduction_model(this->optimality_problem, direction);
}

double l1Relaxation::compute_infeasibility_measure(Iterate& iterate) {
   return this->optimality_problem.model.compute_constraint_violation(iterate.original_evaluations.constraints, L1_NORM);
}

void l1Relaxation::register_accepted_iterate(Iterate& iterate) {
   // TODO check problem
   this->subproblem->postprocess_accepted_iterate(this->relaxed_problem, iterate);
}

size_t l1Relaxation::get_hessian_evaluation_count() const {
   return this->subproblem->get_hessian_evaluation_count();
}

size_t l1Relaxation::get_number_subproblems_solved() const {
   return this->subproblem->number_subproblems_solved;
}

SecondOrderCorrection l1Relaxation::soc_strategy() const {
   return this->subproblem->soc_strategy;
}
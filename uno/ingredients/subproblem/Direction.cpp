#include "Direction.hpp"
#include "Logger.hpp"
#include "Vector.hpp"

Direction::Direction(size_t number_variables, size_t number_constraints):
   x(number_variables), multipliers(number_variables, number_constraints), constraint_partition(number_constraints) {

}

Direction::Direction(std::vector<double>& x, Multipliers& multipliers) : x(x), multipliers(multipliers), constraint_partition(multipliers
.constraints.size()) {
   this->active_set.bounds.at_lower_bound.reserve(x.size());
   this->active_set.bounds.at_lower_bound.reserve(x.size());
   this->active_set.constraints.at_lower_bound.reserve(multipliers.constraints.size());
   this->active_set.constraints.at_upper_bound.reserve(multipliers.constraints.size());
   this->constraint_partition.feasible.reserve(multipliers.constraints.size());
   this->constraint_partition.infeasible.reserve(multipliers.constraints.size());
}

std::ostream& operator<<(std::ostream& stream, const Direction& direction) {
   if (direction.status == OPTIMAL) {
      stream << "Status: optimal\n";
   }
   else if (direction.status == UNBOUNDED_PROBLEM) {
      stream << "Status: unbounded\n";
   }
   else if (direction.status == BOUND_INCONSISTENCY) {
      stream << "Status: bound inconsistency\n";
   }
   else if (direction.status == INFEASIBLE) {
      stream << "Status: infeasible subproblem\n";
   }
   else if (direction.status == INCORRECT_PARAMETER) {
      stream << "Status: incorrect parameter\n";
   }
   else if (direction.status == LP_INSUFFICIENT_SPACE) {
      stream << "Status: insufficient space for the LP\n";
   }
   else if (direction.status == HESSIAN_INSUFFICIENT_SPACE) {
      stream << "Status: insufficient space for the Hessian\n";
   }
   else if (direction.status == SPARSE_INSUFFICIENT_SPACE) {
      stream << "Status: insufficient space for the sparsity pattern\n";
   }
   else if (direction.status == MAX_RESTARTS_REACHED) {
      stream << "Status: maximum number of restarts reached\n";
   }
   else {
      stream << "Status " << direction.status << ": Beware peasant, something went wrong\n";
   }

   //stream << MAGENTA;
   stream << "d^* = ";
   print_vector(stream, direction.x);

   stream << "evaluate_objective = " << direction.objective << "\n";
   stream << "norm = " << direction.norm << "\n";

   stream << "bound constraints active at lower bound =";
   for (size_t index: direction.active_set.bounds.at_lower_bound) {
      stream << " x" << index;
   }
   stream << "\n";
   stream << "bound constraints active at upper bound =";
   for (size_t index: direction.active_set.bounds.at_upper_bound) {
      stream << " x" << index;
   }
   stream << "\n";

   stream << "constraints at lower bound =";
   for (size_t index: direction.active_set.constraints.at_lower_bound) {
      stream << " c" << index;
   }
   stream << "\n";
   stream << "constraints at upper bound =";
   for (size_t index: direction.active_set.constraints.at_upper_bound) {
      stream << " c" << index;
   }
   stream << "\n";

   stream << "general feasible =";
   for (int j: direction.constraint_partition.feasible) {
      stream << " c" << j;
   }
   stream << "\n";

   stream << "general infeasible =";
   for (int j: direction.constraint_partition.infeasible) {
      stream << " c" << j;
      if (direction.constraint_partition.constraint_feasibility[j] == INFEASIBLE_LOWER) {
         stream << " (lower)";
      }
      else if (direction.constraint_partition.constraint_feasibility[j] == INFEASIBLE_UPPER) {
         stream << " (upper)";
      }
   }
   stream << "\n";

   stream << "lower bound multipliers = ";
   print_vector(stream, direction.multipliers.lower_bounds);
   stream << "upper bound multipliers = ";
   print_vector(stream, direction.multipliers.upper_bounds);
   stream << "constraint multipliers = ";
   print_vector(stream, direction.multipliers.constraints);

   return stream;
}
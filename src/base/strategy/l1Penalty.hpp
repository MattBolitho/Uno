#ifndef PENALTYMERITFUNCTION_H
#define PENALTYMERITFUNCTION_H

#include <vector>
#include "GlobalizationStrategy.hpp"
#include "ConstraintRelaxationStrategy.hpp"
#include "Constraint.hpp"

/*! \class PenaltyStrategy
 * \brief Step acceptance strategy based on a penalty method
 *
 *  Strategy that accepts or declines a trial step
 */
class l1Penalty : public GlobalizationStrategy {
public:
    /*!
     *  Constructor that takes an optimization problem and a set of constants
     */
    l1Penalty(ConstraintRelaxationStrategy& feasibility_strategy, Subproblem& subproblem);

    Iterate initialize(Statistics& statistics, Problem& problem, std::vector<double>& x, Multipliers& multipliers) override;
    std::optional<Iterate> check_acceptance(Statistics& statistics, Problem& problem, Iterate& current_iterate, Direction& direction, double step_length) override;

private:
    double decrease_fraction_;
};

#endif // PENALTYMERITFUNCTION_H
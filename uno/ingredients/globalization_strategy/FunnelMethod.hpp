// Copyright (c) 2018-2023 Charlie Vanaret
// Licensed under the MIT license. See LICENSE file in the project directory for details.

#ifndef UNO_FUNNELSTRATEGY_H
#define UNO_FUNNELSTRATEGY_H

#include "GlobalizationStrategy.hpp"
#include "tools/Options.hpp"
#include "tools/Infinity.hpp"

/*! \class TwoPhaseConstants
 * \brief Constants for funnel strategy
 *
 *  Set of constants to control the funnel strategy
 */
struct FunnelMethodParameters {
   double kappa_initial_upper_bound;
   double kappa_initial_multiplication;
   double delta; /*!< Switching constant */
   double upper_bound;
   double infeasibility_fraction;
   double switching_infeasibility_exponent;
   double kappa_infeasibility_1;
   double kappa_infeasibility_2;
   double beta; /*!< Margin around funnel */
   double gamma; /*!< Margin around funnel (sloping margin) */
};

// enum class Phase {FEASIBILITY_RESTORATION = 1, OPTIMALITY = 2};

/*! \class FunnelMethod
 * \brief Step acceptance strategy based on a funnel
 *
 *  Strategy that accepts or declines a trial step
 */
class FunnelMethod : public GlobalizationStrategy {
public:
   explicit FunnelMethod(bool in_restoration_phase, const Options& options);

   void initialize(Statistics& statistics, const Iterate& initial_iterate, const Options& options) override;
   bool is_infeasibility_acceptable_to_funnel(double infeasibility_measure) const;
   [[nodiscard]] bool is_infeasibility_acceptable(const ProgressMeasures& current_progress, const ProgressMeasures& trial_progress) const override;
   [[nodiscard]] bool is_iterate_acceptable(Statistics& statistics, const ProgressMeasures& current_progress,
         const ProgressMeasures& trial_progress, const ProgressMeasures& predicted_reduction, double objective_multiplier) override;
   void update_funnel_width(double current_infeasibility_measure, double trial_infeasibility_measure);
   void update_funnel_width_restoration(double current_infeasibility_measure, double trial_infeasibility_measure);
   
   void reset() override;
   void register_current_progress(const ProgressMeasures& current_progress_measures) override;

   double compute_actual_reduction(double current_optimality_measure, double current_infeasibility_measure, double trial_optimality_measure);
   friend std::ostream& operator<<(std::ostream& stream, FunnelMethod& funnel);
   double get_funnel_width();

   double get_infeasibility_upper_bound() const;
   void set_infeasibility_upper_bound(double new_upper_bound) override;

protected:   
   // pointer to allow polymorphism
   double funnel_width;
//    bool current_iterate_acceptable_to_funnel;
   double initial_funnel_upper_bound{INF<double>};
   const FunnelMethodParameters parameters; /*!< Set of constants */
   const bool in_restoration_phase;
   [[nodiscard]] bool switching_condition(double predicted_reduction, double current_infeasibility, double switching_fraction) const;
};

#endif // UNO_FUNNELSTRATEGY_H

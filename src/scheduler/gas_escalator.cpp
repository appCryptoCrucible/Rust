#include "scheduler/gas_escalator.hpp"

GasParams GasEscalator::Next(const GasParams& current) {
  GasParams next;
  next.max_priority_fee_per_gas = static_cast<unsigned long long>(current.max_priority_fee_per_gas * bump_factor_);
  next.max_fee_per_gas = static_cast<unsigned long long>(current.max_fee_per_gas * bump_factor_);
  return next;
}



#pragma once
#include <chrono>

struct GasParams { unsigned long long max_fee_per_gas; unsigned long long max_priority_fee_per_gas; };

class GasEscalator {
public:
  GasEscalator(double bump_factor = 1.2, std::chrono::seconds interval = std::chrono::seconds(5), unsigned int max_bumps = 3)
    : bump_factor_(bump_factor), interval_(interval), max_bumps_(max_bumps) {}
  GasParams Next(const GasParams& current);
  std::chrono::seconds Interval() const { return interval_; }
  unsigned int MaxBumps() const { return max_bumps_; }
private:
  double bump_factor_;
  std::chrono::seconds interval_;
  unsigned int max_bumps_;
};



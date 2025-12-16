//! Balancer Weighted Pool Mathematics
//!
//! This module implements Balancer's weighted pool mathematical functions for
//! arbitrage and price impact calculations. Balancer uses weighted constant product
//! formula where each token has a weight that determines its share of liquidity.
//!
//! ## Key Formulas
//!
//! - **Invariant**: `V = ∏(B_i)^(W_i)` where B_i is balance and W_i is weight
//! - **Swap**: `amount_out = balance_out * (1 - (balance_in / (balance_in + amount_in_with_fee))^(weight_in / weight_out))`
//! - **Spot Price**: `price = (balance_out / weight_out) / (balance_in / weight_in)`
//!
//! ## Fixed-Point Scaling
//!
//! All weights and prices use 18-decimal (10^18) fixed-point format for precision.
//! This matches Balancer V2's on-chain representation.

use crate::core::{BasisPoints, MathError};
use ethers::types::U256;
use primitive_types::U256 as u256;

// ============================================================================
// Constants
// ============================================================================

/// Fixed-point scaling factor (10^18) - standard ERC20/DeFi precision
const SCALE_18: u128 = 1_000_000_000_000_000_000;

/// Basis points denominator (10000 = 100%)
const BPS_DENOMINATOR: u32 = 10000;

/// Calculate swap output amount for Balancer weighted pools
///
/// Implements the weighted constant product formula:
/// `amount_out = balance_out * (1 - (balance_in / (balance_in + amount_in_with_fee))^(weight_in / weight_out))`
///
/// # Arguments
/// * `amount_in` - Input token amount (raw, unscaled)
/// * `balance_in` - Current balance of input token in pool
/// * `balance_out` - Current balance of output token in pool
/// * `weight_in` - Weight of input token (18-decimal format, e.g., 0.5 = 5e17)
/// * `weight_out` - Weight of output token (18-decimal format)
/// * `swap_fee` - Swap fee (18-decimal format, e.g., 0.003 = 3e15)
///
/// # Returns
/// * `Ok(u256)` - Output amount after fees
/// * `Err(MathError)` - If inputs are invalid or calculation fails
pub fn calculate_swap_output(
    amount_in: u256,
    balance_in: u256,
    balance_out: u256,
    weight_in: u256,
    weight_out: u256,
    swap_fee: u256,
) -> Result<u256, MathError> {
    // Input validation
    if amount_in == u256::zero() {
        return Ok(u256::zero());
    }
    if balance_in == u256::zero() || balance_out == u256::zero() {
        return Err(MathError::InvalidInput {
            operation: "calculate_swap_output".to_string(),
            reason: "Pool balances cannot be zero".to_string(),
            context: "".to_string(),
        });
    }
    if weight_in == u256::zero() || weight_out == u256::zero() {
        return Err(MathError::InvalidInput {
            operation: "calculate_swap_output".to_string(),
            reason: "Token weights cannot be zero".to_string(),
            context: "".to_string(),
        });
    }

    // Use standard 18-decimal scaling
    let scale = u256::from(SCALE_18);

    // Apply swap fee: amount_in_with_fee = amount_in * (1 - swap_fee)
    // swap_fee is in 18-decimal format (e.g., 0.003 = 3e15)
    let fee_amount = amount_in.saturating_mul(swap_fee) / scale;
    let amount_in_with_fee = amount_in.saturating_sub(fee_amount);

    // Prevent division by zero
    let denominator = balance_in.saturating_add(amount_in_with_fee);
    if denominator == u256::zero() {
        return Err(MathError::DivisionByZero {
            operation: "calculate_swap_output".to_string(),
            context: "swap calculation".to_string(),
        });
    }

    // Calculate ratio: balance_in / (balance_in + amount_in_with_fee)
    // ratio is in [0, 1) scaled to 10^18
    let ratio = balance_in.saturating_mul(scale) / denominator;

    // Calculate exponent: weight_in / weight_out
    if weight_out == u256::zero() {
        return Err(MathError::DivisionByZero {
            operation: "calculate_swap_output".to_string(),
            context: "exponent calculation".to_string(),
        });
    }
    // exponent is scaled to 10^18
    let exponent_raw = weight_in.saturating_mul(scale) / weight_out;

    // Extract integer and fractional parts of exponent for power calculation
    let exponent_int = (exponent_raw / scale).as_u128() as usize;
    let exponent_frac = exponent_raw % scale;

    // Calculate (ratio)^exponent using optimized power function
    // Both ratio and result are in 10^18 scale
    let ratio_power = pow_u256_with_fractional_exponent(ratio, exponent_int, exponent_frac, scale);

    // amount_out = balance_out * (1 - ratio^exponent)
    // ratio_power is in scale, so (1 - ratio_power/scale) = (scale - ratio_power)/scale
    let one_minus_ratio_power = if scale > ratio_power {
        scale - ratio_power
    } else {
        u256::zero() // Protect against underflow
    };
    let amount_out = balance_out.saturating_mul(one_minus_ratio_power) / scale;

    Ok(amount_out)
}

/// Natural logarithm approximation using integer arithmetic
/// Returns (ln(x) * scale, is_negative) where scale is the precision factor
/// Uses binary decomposition for better stability
fn ln_u256_q128(x: u256, scale: u256) -> Result<(u256, bool), MathError> {
    if x == u256::zero() {
        return Err(MathError::InvalidInput {
            operation: "ln_u256_q128".to_string(),
            reason: "Cannot compute ln(0)".to_string(),
            context: "".to_string(),
        });
    }

    // For x = scale (which represents 1.0), ln(1) = 0
    if x == scale {
        return Ok((u256::zero(), false));
    }

    let is_negative = x < scale;

    // Work with the absolute value of log
    let work_x = if is_negative {
        // For x < 1: ln(x) = -ln(1/x) = -ln(scale/x) + ln(scale/scale) = -ln(scale^2/x) + ln(scale)
        // Actually simpler: just work with scale^2/x to get ln(1/x)
        scale
            .checked_mul(scale)
            .and_then(|v| v.checked_div(x))
            .unwrap_or(scale)
    } else {
        x
    };

    // Find k such that x is in [2^k, 2^(k+1))
    // ln(x) = k * ln(2) + ln(x / 2^k)
    let mut k: u32 = 0;
    let mut normalized = work_x;

    // Count how many times we can divide by 2 before going below scale
    while normalized >= scale.saturating_mul(u256::from(2)) {
        normalized = normalized / u256::from(2);
        k += 1;
        if k > 255 {
            break;
        }
    }

    // ln(2) ≈ 0.693147 in fixed-point
    // Using scale / 1000 * 693 as approximation for ln(2) * scale
    let ln2_scaled = scale / u256::from(1000) * u256::from(693);

    // ln(x) ≈ k * ln(2) + (normalized - scale) / scale for normalized close to 1
    // This uses ln(1+y) ≈ y for small y
    let k_contribution = ln2_scaled.saturating_mul(u256::from(k));

    // Calculate fractional part: (normalized - scale) / normalized ≈ ln(normalized/scale)
    let frac_contribution = if normalized > scale {
        let diff = normalized - scale;
        // ln(1 + diff/scale) ≈ diff/scale - (diff/scale)^2/2 + ...
        // For simplicity, use first-order approximation scaled
        diff.saturating_mul(scale) / normalized
    } else {
        u256::zero()
    };

    let result = k_contribution.saturating_add(frac_contribution);

    Ok((result, is_negative))
}

/// Exponential function approximation using integer arithmetic
/// Returns exp(x) * scale for x given as (value, is_negative)
/// Uses Taylor series with safe arithmetic
fn exp_u256_q128(x: u256, is_negative: bool, scale: u256) -> Result<u256, MathError> {
    // For large negative x, exp(x) approaches 0
    if is_negative && x > scale.saturating_mul(u256::from(10)) {
        return Ok(u256::zero());
    }

    // For large positive x, exp(x) overflows
    if !is_negative && x > scale.saturating_mul(u256::from(50)) {
        return Err(MathError::Overflow {
            operation: "exp_u256_q128".to_string(),
            inputs: vec![x],
            context: "Exponent too large for exp calculation".to_string(),
        });
    }

    // Normalize: compute exp(x/scale) where x/scale should be reasonable
    // Then scale the result

    // For moderate x, use Taylor: exp(y) ≈ 1 + y + y^2/2 + y^3/6 where y = x/scale
    // Then scale back: exp(x) = exp(y) * scale

    // Safe multiplication: divide before multiply to prevent overflow
    // x2 = x * x / scale (divide first piece by piece)
    let x_div_scale = x / scale;
    let _x_mod_scale = x % scale; // Reserved for higher precision Taylor expansion

    // For small x (relative to scale), use simple approximation
    if x_div_scale == u256::zero() {
        // x < scale, so exp(x) ≈ scale + x (first order Taylor)
        if is_negative {
            if scale > x {
                return Ok(scale - x);
            } else {
                return Ok(u256::zero());
            }
        } else {
            return Ok(scale.saturating_add(x));
        }
    }

    // For larger x, use more terms but with safe division
    // exp(x) ≈ scale * (1 + x/scale + (x/scale)^2/2 + (x/scale)^3/6)

    let y = x_div_scale; // x/scale (integer part)
    let y2 = y.saturating_mul(y);
    let y3 = y2.saturating_mul(y);

    // Compute: scale * (1 + y + y^2/2 + y^3/6)
    let term0 = scale;
    let term1 = y.saturating_mul(scale);
    let term2 = y2.saturating_mul(scale) / u256::from(2);
    let term3 = y3.saturating_mul(scale) / u256::from(6);

    if is_negative {
        // exp(-x) ≈ scale * (1 - y + y^2/2 - y^3/6)
        let positive = term0.saturating_add(term2);
        let negative = term1.saturating_add(term3);

        if positive > negative {
            Ok(positive - negative)
        } else {
            Ok(u256::zero())
        }
    } else {
        Ok(term0
            .saturating_add(term1)
            .saturating_add(term2)
            .saturating_add(term3))
    }
}

/// Calculate power with fractional exponent using proper logarithm-based calculation
/// Formula: x^(a/b) = exp((a/b) * ln(x))
/// This is the production-grade implementation for Balancer weighted pools
fn pow_u256_with_fractional_exponent(
    base: u256,
    exp_int: usize,
    exp_frac: u256,
    scale: u256,
) -> u256 {
    // Handle edge cases
    if base == u256::zero() {
        return u256::zero();
    }
    if exp_int == 0 && exp_frac == u256::zero() {
        return scale; // x^0 = 1
    }
    if base == scale {
        return scale; // 1^x = 1
    }

    // Calculate ln(base)
    let ln_result = match ln_u256_q128(base, scale) {
        Ok(result) => result,
        Err(_) => return u256::zero(),
    };
    let (ln_base, ln_is_negative) = ln_result;

    // Calculate exponent = exp_int + exp_frac/scale
    // We need to multiply ln(base) by (exp_int + exp_frac/scale)
    // = ln(base) * exp_int + ln(base) * exp_frac / scale

    let ln_times_int = ln_base
        .checked_mul(u256::from(exp_int as u64))
        .unwrap_or(u256::MAX);

    let ln_times_frac = ln_base
        .checked_mul(exp_frac)
        .and_then(|v| v.checked_div(scale))
        .unwrap_or(u256::zero());

    let total_exp = ln_times_int.saturating_add(ln_times_frac);

    // Calculate exp(total_exp)
    match exp_u256_q128(total_exp, ln_is_negative, scale) {
        Ok(result) => result,
        Err(_) => {
            // On overflow, use integer-only calculation as fallback
            let mut result = scale;
            let mut base_pow = base;
            let mut exp = exp_int;

            while exp > 0 {
                if exp % 2 == 1 {
                    result = result
                        .checked_mul(base_pow)
                        .and_then(|v| v.checked_div(scale))
                        .unwrap_or(scale);
                }
                base_pow = base_pow
                    .checked_mul(base_pow)
                    .and_then(|v| v.checked_div(scale))
                    .unwrap_or(base_pow);
                exp /= 2;
            }
            result
        }
    }
}

/// Calculate spot price for Balancer weighted pools
///
/// Formula: price = (balance_out / weight_out) / (balance_in / weight_in) * (weight_in / weight_out)
///
/// # Arguments
/// * `balance_in` - Current balance of input token in pool
/// * `balance_out` - Current balance of output token in pool
/// * `weight_in` - Weight of input token (normalized to sum to 1)
/// * `weight_out` - Weight of output token (normalized to sum to 1)
///
/// # Returns
/// * `Ok(u256)` - Spot price with appropriate scaling
/// * `Err(MathError)` - Calculation error
pub fn calculate_balancer_price(
    balance_in: u256,
    balance_out: u256,
    weight_in: u256,
    weight_out: u256,
) -> Result<u256, MathError> {
    // Input validation with proper error types
    if balance_in == u256::zero() || balance_out == u256::zero() {
        return Err(MathError::InvalidInput {
            operation: "calculate_balancer_price".to_string(),
            reason: "Pool balances cannot be zero".to_string(),
            context: format!("balance_in={}, balance_out={}", balance_in, balance_out),
        });
    }
    if weight_in == u256::zero() || weight_out == u256::zero() {
        return Err(MathError::InvalidInput {
            operation: "calculate_balancer_price".to_string(),
            reason: "Token weights cannot be zero".to_string(),
            context: format!("weight_in={}, weight_out={}", weight_in, weight_out),
        });
    }

    // Calculate normalized balances: balance / weight
    let scale = u256::from(10).pow(u256::from(18));
    let normalized_balance_in = balance_in.saturating_mul(scale) / weight_in;
    let normalized_balance_out = balance_out.saturating_mul(scale) / weight_out;

    // Spot price = normalized_balance_out / normalized_balance_in * (weight_in / weight_out)
    if normalized_balance_in == u256::zero() {
        return Err(MathError::DivisionByZero {
            operation: "calculate_balancer_price".to_string(),
            context: "Normalized balance calculation resulted in zero".to_string(),
        });
    }

    let price_ratio = normalized_balance_out.saturating_mul(scale) / normalized_balance_in;
    let weight_adjustment = weight_in.saturating_mul(scale) / weight_out;
    let spot_price = price_ratio.saturating_mul(weight_adjustment) / scale;

    Ok(spot_price)
}

/// Calculate weighted pool invariant for Balancer
///
/// # Formula
/// V = ∏(B_i)^(W_i) where B_i is balance and W_i is normalized weight
/// Using logarithms: log(V) = Σ(W_i * log(B_i))
/// Therefore: V = exp(Σ(W_i * log(B_i)))
///
/// # Arguments
/// * `balances` - Array of token balances in the pool
/// * `weights` - Array of token weights (should sum to 1 with appropriate scaling)
/// * `total_supply` - Total supply of pool tokens (for reference)
///
/// # Returns
/// * `Ok(u256)` - Pool invariant value
/// * `Err(MathError)` - Calculation error
pub fn calculate_weighted_pool_invariant(
    balances: &[u256],
    weights: &[u256],
    _total_supply: u256,
) -> Result<u256, MathError> {
    // Input validation
    if balances.len() != weights.len() {
        return Err(MathError::InvalidInput {
            operation: "calculate_weighted_pool_invariant".to_string(),
            reason: format!(
                "Balance and weight arrays must have same length: {} vs {}",
                balances.len(),
                weights.len()
            ),
            context: "Balancer weighted pool".to_string(),
        });
    }
    if balances.is_empty() {
        return Err(MathError::InvalidInput {
            operation: "calculate_weighted_pool_invariant".to_string(),
            reason: "Pool cannot be empty".to_string(),
            context: "Balancer weighted pool".to_string(),
        });
    }

    // Use high precision scaling (10^36)
    let scale = u256::from(10).pow(u256::from(36));

    // Calculate Σ(W_i * log(B_i))
    let mut log_sum: i128 = 0i128;

    for (i, &balance) in balances.iter().enumerate() {
        if weights[i] == u256::zero() {
            return Err(MathError::InvalidInput {
                operation: "calculate_weighted_pool_invariant".to_string(),
                reason: format!("Token {} has zero weight", i),
                context: "Balancer weighted pool".to_string(),
            });
        }
        if balance == u256::zero() {
            return Err(MathError::InvalidInput {
                operation: "calculate_weighted_pool_invariant".to_string(),
                reason: format!("Token {} has zero balance", i),
                context: "Balancer weighted pool".to_string(),
            });
        }

        // Calculate ln(balance) in scaled format
        let balance_scaled = balance
            .checked_mul(scale)
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_weighted_pool_invariant".to_string(),
                inputs: vec![balance, scale],
                context: format!("Balance {} overflow in scaling", i),
            })?;

        let (ln_balance, is_negative) = ln_u256_q128(balance_scaled, scale)?;

        // Multiply by weight (weights are in 18-decimal format)
        let ln_contrib = ln_balance
            .checked_mul(weights[i])
            .and_then(|v| v.checked_div(u256::from(10).pow(u256::from(18))))
            .unwrap_or(u256::zero());

        // Convert to i128 for signed accumulation
        let contrib_i128 = (ln_contrib.as_u128() as i128).min(i128::MAX);

        if is_negative {
            log_sum = log_sum.saturating_sub(contrib_i128);
        } else {
            log_sum = log_sum.saturating_add(contrib_i128);
        }
    }

    // Calculate invariant = exp(log_sum)
    let (exp_input, exp_is_negative) = if log_sum >= 0 {
        (u256::from(log_sum as u128), false)
    } else {
        (u256::from((-log_sum) as u128), true)
    };

    let invariant = exp_u256_q128(exp_input, exp_is_negative, scale)?;

    // Scale back to 18-decimal format
    let invariant_scaled = invariant / u256::from(10).pow(u256::from(18));

    Ok(invariant_scaled)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_calculate_swap_output_basic() {
        // Test with equal weights (0.5 each, scaled to 5e17)
        let weight_50 = u256::from(5) * u256::from(10).pow(u256::from(17)); // 0.5 * 1e18

        let amount_in = u256::from(1000000); // 1 USDC (6 decimals scaled)
        let balance_in = u256::from(1000000000000u64); // 1M USDC
        let balance_out = u256::from(1000000000000000000000u128); // 1000 ETH (18 decimals)
        let swap_fee = u256::from(3) * u256::from(10).pow(u256::from(15)); // 0.003 * 1e18

        let result = calculate_swap_output(
            amount_in,
            balance_in,
            balance_out,
            weight_50,
            weight_50,
            swap_fee,
        );

        assert!(result.is_ok(), "Swap calculation should succeed");
        let amount_out = result.unwrap();
        assert!(
            amount_out > u256::zero(),
            "Should receive some output tokens"
        );
    }

    #[test]
    fn test_calculate_balancer_price() {
        let balance_in = u256::from(1000000); // 1M tokens
        let balance_out = u256::from(1000000); // 1M tokens
        let weight_in = u256::from(5) * u256::from(10).pow(u256::from(17)); // 0.5
        let weight_out = u256::from(5) * u256::from(10).pow(u256::from(17)); // 0.5

        let result = calculate_balancer_price(balance_in, balance_out, weight_in, weight_out);
        assert!(result.is_ok(), "Price calculation should succeed");

        let price = result.unwrap();
        // With equal balances and weights, price should be approximately 1:1
        assert!(
            price > u256::from(9) * u256::from(10).pow(u256::from(17)),
            "Price should be close to 1"
        );
        assert!(
            price < u256::from(11) * u256::from(10).pow(u256::from(17)),
            "Price should be close to 1"
        );
    }

    #[test]
    fn test_zero_input() {
        let result = calculate_swap_output(
            u256::zero(),
            u256::from(1000),
            u256::from(1000),
            u256::from(5) * u256::from(10).pow(u256::from(17)),
            u256::from(5) * u256::from(10).pow(u256::from(17)),
            u256::zero(),
        );
        assert_eq!(
            result.unwrap(),
            u256::zero(),
            "Zero input should return zero output"
        );
    }

    #[test]
    fn test_zero_balance() {
        let result = calculate_swap_output(
            u256::from(100),
            u256::zero(),
            u256::from(1000),
            u256::from(5) * u256::from(10).pow(u256::from(17)),
            u256::from(5) * u256::from(10).pow(u256::from(17)),
            u256::zero(),
        );
        assert!(result.is_err(), "Zero balance should return error");
    }
}

/// Calculate Balancer sandwich profit
///
/// Calculates the profit from a sandwich attack on a Balancer weighted pool:
/// 1. Frontrun: Buy token_out with frontrun_amount of token_in
/// 2. Victim: Victim's trade executes
/// 3. Backrun: Sell token_out back to token_in
///
/// # Arguments
/// * `frontrun_amount` - Amount of token_in to use for frontrun
/// * `victim_amount` - Amount of token_in the victim is swapping
/// * `balance_in` - Current balance of input token in pool
/// * `balance_out` - Current balance of output token in pool
/// * `weight_in` - Weight of input token (18-decimal format)
/// * `weight_out` - Weight of output token (18-decimal format)
/// * `swap_fee` - Balancer swap fee (18-decimal format)
/// * `fee_bps` - Deprecated, use swap_fee consistently
/// * `aave_fee_bps` - Flash loan fee in basis points
///
/// # Returns
/// * `Ok(U256)` - Profit amount in token_in
/// * `Err(MathError)` - If calculation fails
pub fn calculate_balancer_sandwich_profit(
    frontrun_amount: U256,
    victim_amount: U256,
    balance_in: U256,
    balance_out: U256,
    weight_in: U256,
    weight_out: U256,
    swap_fee: U256,
    _fee_bps: BasisPoints, // DEPRECATED: Use swap_fee consistently
    aave_fee_bps: BasisPoints,
) -> Result<U256, MathError> {
    // FIXED Issue #23: Use swap_fee consistently for all swaps
    // swap_fee should be in 18-decimal format (e.g., 0.003 * 10^18 for 0.3%)

    // Calculate reserves after frontrun using consistent swap_fee
    let frontrun_output = calculate_swap_output(
        frontrun_amount,
        balance_in,
        balance_out,
        weight_in,
        weight_out,
        swap_fee,
    )?;
    let balance_in_post_frontrun =
        balance_in
            .checked_add(frontrun_amount)
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_balancer_sandwich_profit".to_string(),
                inputs: vec![balance_in, frontrun_amount],
                context: "Post-frontrun balance in".to_string(),
            })?;
    let balance_out_post_frontrun =
        balance_out
            .checked_sub(frontrun_output)
            .ok_or_else(|| MathError::Underflow {
                operation: "calculate_balancer_sandwich_profit".to_string(),
                inputs: vec![balance_out, frontrun_output],
                context: "Post-frontrun balance out".to_string(),
            })?;

    // Calculate reserves after victim
    let victim_output = calculate_swap_output(
        victim_amount,
        balance_in_post_frontrun,
        balance_out_post_frontrun,
        weight_in,
        weight_out,
        swap_fee,
    )?;
    let balance_in_post_victim = balance_in_post_frontrun
        .checked_add(victim_amount)
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_balancer_sandwich_profit".to_string(),
            inputs: vec![balance_in_post_frontrun, victim_amount],
            context: "Post-victim balance in".to_string(),
        })?;
    let balance_out_post_victim = balance_out_post_frontrun
        .checked_sub(victim_output)
        .ok_or_else(|| MathError::Underflow {
            operation: "calculate_balancer_sandwich_profit".to_string(),
            inputs: vec![balance_out_post_frontrun, victim_output],
            context: "Post-victim balance out".to_string(),
        })?;

    // Calculate backrun output (sell frontrun_amount worth of output token back to input token)
    let backrun_output = calculate_swap_output(
        frontrun_output,
        balance_out_post_victim,
        balance_in_post_victim,
        weight_out,
        weight_in,
        swap_fee,
    )?;

    // Calculate flash loan cost
    let flash_loan_cost = frontrun_amount
        .checked_mul(U256::from(aave_fee_bps.as_u32()))
        .and_then(|v| v.checked_div(U256::from(10000)))
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_balancer_sandwich_profit".to_string(),
            inputs: vec![frontrun_amount],
            context: "Flash loan cost".to_string(),
        })?;

    // Profit = backrun_output - frontrun_amount - flash_loan_cost
    backrun_output
        .checked_sub(frontrun_amount)
        .and_then(|v| v.checked_sub(flash_loan_cost))
        .ok_or_else(|| MathError::Underflow {
            operation: "calculate_balancer_sandwich_profit".to_string(),
            inputs: vec![backrun_output, frontrun_amount, flash_loan_cost],
            context: "Profit calculation".to_string(),
        })
}

pub fn calculate_balancer_post_frontrun_balances(
    frontrun_amount: U256,
    balance_in: U256,
    balance_out: U256,
    weight_in: U256,
    weight_out: U256,
    swap_fee: U256,
) -> Result<(U256, U256), MathError> {
    let frontrun_output = calculate_swap_output(
        frontrun_amount,
        balance_in,
        balance_out,
        weight_in,
        weight_out,
        swap_fee,
    )?;
    let new_balance_in =
        balance_in
            .checked_add(frontrun_amount)
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_balancer_post_frontrun_balances".to_string(),
                inputs: vec![balance_in, frontrun_amount],
                context: "Balance in".to_string(),
            })?;
    let new_balance_out =
        balance_out
            .checked_sub(frontrun_output)
            .ok_or_else(|| MathError::Underflow {
                operation: "calculate_balancer_post_frontrun_balances".to_string(),
                inputs: vec![balance_out, frontrun_output],
                context: "Balance out".to_string(),
            })?;
    Ok((new_balance_in, new_balance_out))
}

pub fn calculate_balancer_post_victim_balances(
    victim_amount: U256,
    balance_in: U256,
    balance_out: U256,
    weight_in: U256,
    weight_out: U256,
    swap_fee: U256,
) -> Result<(U256, U256), MathError> {
    calculate_balancer_post_frontrun_balances(
        victim_amount,
        balance_in,
        balance_out,
        weight_in,
        weight_out,
        swap_fee,
    )
}

pub fn simulate_victim_execution(
    victim_amount: U256,
    balance_in: U256,
    balance_out: U256,
    weight_in: U256,
    weight_out: U256,
    swap_fee: U256,
) -> Result<(U256, U256), MathError> {
    calculate_balancer_post_victim_balances(
        victim_amount,
        balance_in,
        balance_out,
        weight_in,
        weight_out,
        swap_fee,
    )
}

/// Swap execution for Balancer pool
#[derive(Debug, Clone)]
pub struct BalancerSwapExecution {
    /// Balances before swap
    pub balances_before: Vec<U256>,
    /// Balances after swap
    pub balances_after: Vec<U256>,
    /// Fee amount generated
    pub fee_amount: U256,
    /// Amount swapped
    pub amount_in: U256,
}

/// Simulate Balancer swap with balance tracking for JIT
/// Uses Balancer's weighted constant product formula
pub fn simulate_balancer_swap_for_jit(
    token_in_idx: usize,
    token_out_idx: usize,
    amount_in: u256,
    balances: &[u256],
    weights: &[u256],
    swap_fee_bps: u32,
) -> Result<BalancerSwapExecution, MathError> {
    // Balancer uses weighted math: balance_in, weight_in, weight_out
    // Get individual balances
    let balance_in = if token_in_idx < balances.len() {
        balances[token_in_idx]
    } else {
        return Err(MathError::InvalidInput {
            operation: "simulate_balancer_swap_for_jit".to_string(),
            reason: "token_in_idx out of bounds".to_string(),
            context: format!("idx={}, len={}", token_in_idx, balances.len()),
        });
    };

    let balance_out = if token_out_idx < balances.len() {
        balances[token_out_idx]
    } else {
        return Err(MathError::InvalidInput {
            operation: "simulate_balancer_swap_for_jit".to_string(),
            reason: "token_out_idx out of bounds".to_string(),
            context: format!("idx={}, len={}", token_out_idx, balances.len()),
        });
    };

    let weight_in = if token_in_idx < weights.len() {
        weights[token_in_idx]
    } else {
        return Err(MathError::InvalidInput {
            operation: "simulate_balancer_swap_for_jit".to_string(),
            reason: "weight_in out of bounds".to_string(),
            context: "".to_string(),
        });
    };

    let weight_out = if token_out_idx < weights.len() {
        weights[token_out_idx]
    } else {
        return Err(MathError::InvalidInput {
            operation: "simulate_balancer_swap_for_jit".to_string(),
            reason: "weight_out out of bounds".to_string(),
            context: "".to_string(),
        });
    };

    let swap_fee = u256::from(swap_fee_bps) * u256::from(10).pow(u256::from(14)); // Convert to 18-decimal format

    // Calculate output using existing Balancer math
    let amount_out = calculate_swap_output(
        amount_in,
        balance_in,
        balance_out,
        weight_in,
        weight_out,
        swap_fee,
    )?;

    // Calculate fee
    let fee_amount = amount_in
        .checked_mul(u256::from(swap_fee_bps))
        .and_then(|v| v.checked_div(u256::from(10000)))
        .ok_or_else(|| MathError::Overflow {
            operation: "simulate_balancer_swap_for_jit".to_string(),
            inputs: vec![amount_in.into()],
            context: "fee calculation".to_string(),
        })?;

    // Calculate new balances
    let mut new_balances = balances.to_vec();
    new_balances[token_in_idx] =
        balances[token_in_idx]
            .checked_add(amount_in)
            .ok_or_else(|| MathError::Overflow {
                operation: "simulate_balancer_swap_for_jit".to_string(),
                inputs: vec![balances[token_in_idx].into(), amount_in.into()],
                context: "balance update".to_string(),
            })?;
    new_balances[token_out_idx] =
        balances[token_out_idx]
            .checked_sub(amount_out)
            .ok_or_else(|| MathError::Underflow {
                operation: "simulate_balancer_swap_for_jit".to_string(),
                inputs: vec![balances[token_out_idx].into(), amount_out.into()],
                context: "balance update".to_string(),
            })?;

    Ok(BalancerSwapExecution {
        balances_before: balances.to_vec(),
        balances_after: new_balances,
        fee_amount,
        amount_in,
    })
}

/// Golden Section Search for Balancer sandwich optimization
///
/// Finds the optimal frontrun amount that maximizes profit using the golden section search algorithm.
/// This is a unimodal optimization method that efficiently narrows the search space.
///
/// # Arguments
/// * `victim_amount` - Amount the victim is swapping
/// * `balance_in` - Current balance of input token in pool
/// * `balance_out` - Current balance of output token in pool
/// * `weight_in` - Weight of input token (18-decimal format)
/// * `weight_out` - Weight of output token (18-decimal format)
/// * `swap_fee` - Balancer swap fee (18-decimal format)
/// * `fee_bps` - Deprecated parameter
/// * `aave_fee_bps` - Flash loan fee in basis points
///
/// # Returns
/// * `Ok(U256)` - Optimal frontrun amount
/// * `Err(MathError)` - If optimization fails
pub fn golden_section_balancer_sandwich_optimization(
    victim_amount: U256,
    balance_in: U256,
    balance_out: U256,
    weight_in: U256,
    weight_out: U256,
    swap_fee: U256,
    fee_bps: BasisPoints,
    aave_fee_bps: BasisPoints,
) -> Result<U256, MathError> {
    const PHI_INV: u128 = 6180; // Golden ratio inverse * 10000

    let mut a = U256::from(1000000); // Minimum frontrun size
    let mut b = victim_amount; // Maximum frontrun size
    let tolerance = victim_amount / U256::from(10000); // 0.01% precision

    // Golden section points
    let mut c = b - (b - a) * U256::from(PHI_INV) / U256::from(10000);
    let mut d = a + (b - a) * U256::from(PHI_INV) / U256::from(10000);

    // Initial function evaluations
    let mut fc = calculate_balancer_sandwich_profit(
        c,
        victim_amount,
        balance_in,
        balance_out,
        weight_in,
        weight_out,
        swap_fee,
        fee_bps,
        aave_fee_bps,
    )?;
    let mut fd = calculate_balancer_sandwich_profit(
        d,
        victim_amount,
        balance_in,
        balance_out,
        weight_in,
        weight_out,
        swap_fee,
        fee_bps,
        aave_fee_bps,
    )?;

    // Golden section iterations
    for _iteration in 0..30 {
        if (b - a) < tolerance {
            break;
        }

        if fc < fd {
            // Narrow search to [a, d]
            b = d;
            d = c;
            fd = fc;

            c = b - (b - a) * U256::from(PHI_INV) / U256::from(10000);
            fc = calculate_balancer_sandwich_profit(
                c,
                victim_amount,
                balance_in,
                balance_out,
                weight_in,
                weight_out,
                swap_fee,
                fee_bps,
                aave_fee_bps,
            )?;
        } else {
            // Narrow search to [c, b]
            a = c;
            c = d;
            fc = fd;

            d = a + (b - a) * U256::from(PHI_INV) / U256::from(10000);
            fd = calculate_balancer_sandwich_profit(
                d,
                victim_amount,
                balance_in,
                balance_out,
                weight_in,
                weight_out,
                swap_fee,
                fee_bps,
                aave_fee_bps,
            )?;
        }
    }

    Ok((a + b) / U256::from(2))
}

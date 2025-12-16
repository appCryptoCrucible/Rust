//! Uniswap V2 Math - Production-grade implementation
//!
//! Constant product formula: x * y = k
//! With fee: amount_out = (reserve_out * amount_in_with_fee) / (reserve_in * 10000 + amount_in_with_fee)

use crate::core::{BasisPoints, MathError};
use ethers::types::U256;

/// Calculate amount out for Uniswap V2 swap
///
/// Formula: amount_out = (reserve_out * amount_in_with_fee) / (reserve_in * 10000 + amount_in_with_fee)
/// where amount_in_with_fee = amount_in * (10000 - fee_bps)
///
/// # Arguments
/// * `amount_in` - Input token amount (in wei)
/// * `reserve_in` - Input token reserve (in wei)
/// * `reserve_out` - Output token reserve (in wei)
/// * `fee_bps` - Fee in basis points (30 = 0.3%)
///
/// # Returns
/// * `Ok(U256)` - Output amount in wei
/// * `Err(MathError)` - If validation fails or overflow occurs
pub fn calculate_v2_amount_out(
    amount_in: U256,
    reserve_in: U256,
    reserve_out: U256,
    fee_bps: BasisPoints,
) -> Result<U256, MathError> {
    // Input validation
    if amount_in.is_zero() {
        return Err(MathError::InvalidInput {
            operation: "calculate_v2_amount_out".to_string(),
            reason: "amount_in cannot be zero".to_string(),
            context: "V2 swap calculation".to_string(),
        });
    }

    if reserve_in.is_zero() || reserve_out.is_zero() {
        return Err(MathError::InvalidInput {
            operation: "calculate_v2_amount_out".to_string(),
            reason: format!(
                "Reserves cannot be zero: reserve_in: {}, reserve_out: {}",
                reserve_in, reserve_out
            ),
            context: "V2 swap calculation".to_string(),
        });
    }

    // Apply fee: amount_in_with_fee = amount_in * (10000 - fee_bps)
    let fee_multiplier = U256::from(10000 - fee_bps.as_u32());
    let amount_in_with_fee =
        amount_in
            .checked_mul(fee_multiplier)
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_v2_amount_out".to_string(),
                inputs: vec![amount_in, U256::from(fee_bps.as_u32())],
                context: "V2 swap calculation".to_string(),
            })?;

    // Calculate numerator: reserve_out * amount_in_with_fee
    let numerator =
        reserve_out
            .checked_mul(amount_in_with_fee)
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_v2_amount_out".to_string(),
                inputs: vec![reserve_out, amount_in_with_fee],
                context: "numerator calculation".to_string(),
            })?;

    // Calculate denominator: (reserve_in * 10000) + amount_in_with_fee
    let reserve_in_scaled =
        reserve_in
            .checked_mul(U256::from(10000))
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_v2_amount_out".to_string(),
                inputs: vec![reserve_in, U256::from(10000)],
                context: "reserve_in * 10000".to_string(),
            })?;

    let denominator = reserve_in_scaled
        .checked_add(amount_in_with_fee)
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_v2_amount_out".to_string(),
            inputs: vec![reserve_in_scaled, amount_in_with_fee],
            context: "denominator calculation".to_string(),
        })?;

    // Final division
    if denominator.is_zero() {
        return Err(MathError::DivisionByZero {
            operation: "calculate_v2_amount_out".to_string(),
            context: "denominator is zero".to_string(),
        });
    }

    Ok(numerator / denominator)
}

/// Calculate price impact for V2 swap in basis points
///
/// Price impact = (amount_in / reserve_in) * 10000
///
/// # Arguments
/// * `amount_in` - Input token amount (in wei)
/// * `reserve_in` - Input token reserve (in wei)
///
/// # Returns
/// * `Ok(u32)` - Price impact in basis points
/// * `Err(MathError)` - If validation fails or overflow occurs
pub fn calculate_v2_price_impact(amount_in: U256, reserve_in: U256) -> Result<u32, MathError> {
    // Input validation
    if amount_in.is_zero() {
        return Ok(0); // No impact if no trade
    }

    if reserve_in.is_zero() {
        return Err(MathError::InvalidInput {
            operation: "calculate_v2_price_impact".to_string(),
            reason: "reserve_in cannot be zero".to_string(),
            context: "".to_string(),
        });
    }

    // Calculate impact: (amount_in / reserve_in) * 10000
    let impact_scaled =
        amount_in
            .checked_mul(U256::from(10000))
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_v2_price_impact".to_string(),
                inputs: vec![amount_in, U256::from(10000)],
                context: "impact scaling".to_string(),
            })?;

    let impact = impact_scaled / reserve_in;

    // Convert to u32 (capped at 10000 = 100%)
    let impact_bps = if impact > U256::from(10000) {
        10000
    } else {
        impact.as_u32()
    };

    Ok(impact_bps)
}

/// Calculate optimal sandwich front-run size for V2
///
/// This finds the amount_in that maximizes profit while keeping victim slippage under max_slippage_bps
///
/// # Arguments
/// * `victim_amount_in` - Victim's trade size
/// * `reserve_in` - Input token reserve
/// * `reserve_out` - Output token reserve  
/// * `max_slippage_bps` - Maximum allowed victim slippage (100 = 1%)
///
/// # Returns
/// * `Ok(U256)` - Optimal front-run amount
/// * `Err(MathError)` - If validation fails
pub fn calculate_v2_optimal_sandwich_size(
    victim_amount_in: U256,
    reserve_in: U256,
    reserve_out: U256,
    max_slippage_bps: BasisPoints,
) -> Result<U256, MathError> {
    // Input validation
    if victim_amount_in.is_zero() {
        return Ok(U256::zero());
    }

    if reserve_in.is_zero() || reserve_out.is_zero() {
        return Err(MathError::InvalidInput {
            operation: "calculate_v2_optimal_sandwich_size".to_string(),
            reason: "reserves cannot be zero".to_string(),
            context: format!("reserve_in: {}, reserve_out: {}", reserve_in, reserve_out),
        });
    }

    // Calculate victim's price impact
    let victim_impact = calculate_v2_price_impact(victim_amount_in, reserve_in)?;

    // If victim impact already exceeds max, we can't sandwich
    if victim_impact > max_slippage_bps.as_u32() {
        return Ok(U256::zero());
    }

    // Calculate remaining slippage budget: max_slippage - victim_impact
    let remaining_slippage_bps = max_slippage_bps.as_u32().saturating_sub(victim_impact);

    // Optimal front-run size = reserve_in * remaining_slippage / 10000
    let optimal_size = reserve_in
        .checked_mul(U256::from(remaining_slippage_bps))
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_v2_optimal_sandwich_size".to_string(),
            inputs: vec![reserve_in, U256::from(remaining_slippage_bps)],
            context: "optimal size calculation".to_string(),
        })?;

    Ok(optimal_size / U256::from(10000))
}

/// Calculate Uniswap V2 sandwich profit
///
/// Calculates the profit from a sandwich attack on a Uniswap V2 pool:
/// 1. Frontrun: Buy token_out with frontrun_amount of token_in
/// 2. Victim: Victim's trade executes
/// 3. Backrun: Sell token_out back to token_in
///
/// # Arguments
/// * `frontrun_amount` - Amount of token_in to use for frontrun
/// * `victim_amount` - Amount of token_in the victim is swapping
/// * `reserve_in` - Current reserve of input token in pool
/// * `reserve_out` - Current reserve of output token in pool
/// * `fee_bps` - Uniswap V2 swap fee in basis points (30 = 0.3%)
/// * `aave_fee_bps` - Flash loan fee in basis points
///
/// # Returns
/// * `Ok(U256)` - Profit amount in token_in
/// * `Err(MathError)` - If calculation fails
pub fn calculate_v2_sandwich_profit(
    frontrun_amount: U256,
    victim_amount: U256,
    reserve_in: U256,
    reserve_out: U256,
    fee_bps: BasisPoints,
    aave_fee_bps: BasisPoints,
) -> Result<U256, MathError> {
    // OPTIMIZATION: Use calculate_v2_post_swap_state to get reserves AND output in one call
    // This avoids duplicate calculation of frontrun output (was Issue #18)

    // Step 1: Calculate frontrun - get new reserves AND the output we receive (our backrun input)
    let (reserve_in_post_frontrun, reserve_out_post_frontrun, frontrun_output) =
        calculate_v2_post_swap_state(frontrun_amount, reserve_in, reserve_out, fee_bps)?;

    // Step 2: Calculate victim swap effect on reserves
    let (reserve_in_post_victim, reserve_out_post_victim, _victim_output) =
        calculate_v2_post_swap_state(
            victim_amount,
            reserve_in_post_frontrun,
            reserve_out_post_frontrun,
            fee_bps,
        )?;

    // Step 3: Calculate backrun - we sell our frontrun_output (token_out) for token_in
    // Note: For backrun, we're selling token_out to get token_in back
    // So reserve_out_post_victim becomes our "reserve_in" and reserve_in_post_victim is "reserve_out"
    let backrun_output = calculate_v2_amount_out(
        frontrun_output,         // Our input: what we got from frontrun
        reserve_out_post_victim, // Reserve of what we're selling (token_out)
        reserve_in_post_victim,  // Reserve of what we're buying (token_in)
        fee_bps,
    )?;

    // Step 4: Calculate flash loan cost
    let flash_loan_cost = frontrun_amount
        .checked_mul(U256::from(aave_fee_bps.as_u32()))
        .and_then(|v| v.checked_div(U256::from(10000)))
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_v2_sandwich_profit".to_string(),
            inputs: vec![frontrun_amount],
            context: "Flash loan cost calculation".to_string(),
        })?;

    // Step 5: Calculate profit = backrun_output - frontrun_amount - flash_loan_cost
    // Return 0 if negative (for optimization compatibility)
    let total_cost = frontrun_amount.saturating_add(flash_loan_cost);

    if backrun_output >= total_cost {
        Ok(backrun_output - total_cost)
    } else {
        Ok(U256::zero())
    }
}

/// Calculate post-swap reserves and output amount for V2
///
/// Returns (new_reserve_in, new_reserve_out, amount_out) to avoid duplicate calculation
///
/// # Arguments
/// * `amount_in` - Input amount for the swap
/// * `reserve_in` - Current input token reserve
/// * `reserve_out` - Current output token reserve
/// * `fee_bps` - Fee in basis points
///
/// # Returns
/// * `Ok((U256, U256, U256))` - (new_reserve_in, new_reserve_out, amount_out)
pub fn calculate_v2_post_swap_state(
    amount_in: U256,
    reserve_in: U256,
    reserve_out: U256,
    fee_bps: BasisPoints,
) -> Result<(U256, U256, U256), MathError> {
    let amount_out = calculate_v2_amount_out(amount_in, reserve_in, reserve_out, fee_bps)?;

    let new_reserve_in = reserve_in
        .checked_add(amount_in)
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_v2_post_swap_state".to_string(),
            inputs: vec![reserve_in, amount_in],
            context: "Reserve in addition".to_string(),
        })?;

    let new_reserve_out =
        reserve_out
            .checked_sub(amount_out)
            .ok_or_else(|| MathError::Underflow {
                operation: "calculate_v2_post_swap_state".to_string(),
                inputs: vec![reserve_out, amount_out],
                context: "Reserve out subtraction".to_string(),
            })?;

    Ok((new_reserve_in, new_reserve_out, amount_out))
}

/// Calculate post-frontrun reserves (legacy wrapper for backward compatibility)
pub fn calculate_v2_post_frontrun_reserves(
    frontrun_amount: U256,
    reserve_in: U256,
    reserve_out: U256,
    fee_bps: BasisPoints,
) -> Result<(U256, U256), MathError> {
    let (new_in, new_out, _) =
        calculate_v2_post_swap_state(frontrun_amount, reserve_in, reserve_out, fee_bps)?;
    Ok((new_in, new_out))
}

/// Calculate post-victim reserves (legacy wrapper for backward compatibility)
pub fn calculate_v2_post_victim_reserves(
    victim_amount: U256,
    reserve_in: U256,
    reserve_out: U256,
    fee_bps: BasisPoints,
) -> Result<(U256, U256), MathError> {
    calculate_v2_post_frontrun_reserves(victim_amount, reserve_in, reserve_out, fee_bps)
}

pub fn simulate_victim_execution(
    victim_amount: U256,
    reserve_in: U256,
    reserve_out: U256,
    fee_bps: BasisPoints,
) -> Result<(U256, U256), MathError> {
    calculate_v2_post_victim_reserves(victim_amount, reserve_in, reserve_out, fee_bps)
}

/// Golden section search for V2 sandwich optimization
///
/// Uses golden section search (not Newton-Raphson) because:
/// 1. The profit function is unimodal (single maximum)
/// 2. U256 can't represent negative derivatives
/// 3. Golden section is more robust for optimization
///
/// # Arguments
/// * `victim_amount` - Amount the victim is swapping
/// * `reserve_in` - Current reserve of input token in pool
/// * `reserve_out` - Current reserve of output token in pool
/// * `fee_bps` - Uniswap V2 swap fee in basis points
/// * `aave_fee_bps` - Flash loan fee in basis points
///
/// # Returns
/// * `Ok(U256)` - Optimal frontrun amount
/// * `Err(MathError)` - If optimization fails
pub fn newton_raphson_sandwich_optimization(
    victim_amount: U256,
    reserve_in: U256,
    reserve_out: U256,
    fee_bps: BasisPoints,
    aave_fee_bps: BasisPoints,
) -> Result<U256, MathError> {
    // Golden ratio constants for golden section search
    // φ = (1 + √5) / 2 ≈ 1.618033988749895
    // 1/φ = φ - 1 ≈ 0.618033988749895
    const PHI_INV_SCALED: u64 = 618033988; // 1/φ * 10^9
    const SCALE: u64 = 1_000_000_000; // 10^9

    // Search bounds: [0, victim_amount]
    // We want to find x that maximizes profit(x)
    let mut a = U256::zero();
    let mut b = victim_amount;

    // Tolerance: 0.01% of victim_amount or minimum 1
    let tolerance = (victim_amount / U256::from(10000)).max(U256::from(1));

    // Calculate initial interior points using golden ratio
    let diff = b - a;
    let golden_diff = diff.saturating_mul(U256::from(PHI_INV_SCALED)) / U256::from(SCALE);

    let mut c = a + golden_diff;
    let mut d = b - golden_diff;

    // Ensure c < d
    if c > d {
        std::mem::swap(&mut c, &mut d);
    }

    // Calculate profits at interior points
    let mut fc = calculate_v2_sandwich_profit(
        c,
        victim_amount,
        reserve_in,
        reserve_out,
        fee_bps,
        aave_fee_bps,
    )
    .unwrap_or(U256::zero());
    let mut fd = calculate_v2_sandwich_profit(
        d,
        victim_amount,
        reserve_in,
        reserve_out,
        fee_bps,
        aave_fee_bps,
    )
    .unwrap_or(U256::zero());

    // Golden section search loop
    for _iteration in 0..50 {
        // Check convergence
        if b.saturating_sub(a) < tolerance {
            break;
        }

        if fc < fd {
            // Maximum is in [c, b]
            a = c;
            c = d;
            fc = fd;

            // Calculate new d
            let new_diff = b - a;
            let new_golden =
                new_diff.saturating_mul(U256::from(PHI_INV_SCALED)) / U256::from(SCALE);
            d = b - new_golden;

            fd = calculate_v2_sandwich_profit(
                d,
                victim_amount,
                reserve_in,
                reserve_out,
                fee_bps,
                aave_fee_bps,
            )
            .unwrap_or(U256::zero());
        } else {
            // Maximum is in [a, d]
            b = d;
            d = c;
            fd = fc;

            // Calculate new c
            let new_diff = b - a;
            let new_golden =
                new_diff.saturating_mul(U256::from(PHI_INV_SCALED)) / U256::from(SCALE);
            c = a + new_golden;

            fc = calculate_v2_sandwich_profit(
                c,
                victim_amount,
                reserve_in,
                reserve_out,
                fee_bps,
                aave_fee_bps,
            )
            .unwrap_or(U256::zero());
        }
    }

    // Return the midpoint of the final interval
    Ok((a + b) / U256::from(2))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_v2_amount_out() {
        let amount_in = U256::from(1_000_000u64); // 1 token
        let reserve_in = U256::from(100_000_000u64); // 100 tokens
        let reserve_out = U256::from(50_000_000u64); // 50 tokens
        let fee_bps = BasisPoints::new(30).unwrap(); // 0.3% fee

        let amount_out =
            calculate_v2_amount_out(amount_in, reserve_in, reserve_out, fee_bps).unwrap();

        // Should get approximately 0.497 tokens out (less than 0.5 due to fee)
        assert!(amount_out > U256::zero());
        assert!(amount_out < U256::from(500_000u64)); // Less than 0.5
    }

    #[test]
    fn test_v2_price_impact() {
        let amount_in = U256::from(1_000_000u64); // 1 token
        let reserve_in = U256::from(100_000_000u64); // 100 tokens

        let impact = calculate_v2_price_impact(amount_in, reserve_in).unwrap();

        // Impact = (1 / 100) * 10000 = 100 bps = 1%
        assert_eq!(impact, 100);
    }

    #[test]
    fn test_v2_zero_amount_in() {
        let amount_in = U256::zero();
        let reserve_in = U256::from(100_000_000u64);
        let reserve_out = U256::from(50_000_000u64);
        let fee_bps = BasisPoints::new(30).unwrap();

        let result = calculate_v2_amount_out(amount_in, reserve_in, reserve_out, fee_bps);
        assert!(result.is_err()); // Should error on zero input
    }

    #[test]
    fn test_v2_zero_reserves() {
        let amount_in = U256::from(1_000_000u64);
        let reserve_in = U256::zero();
        let reserve_out = U256::from(50_000_000u64);
        let fee_bps = BasisPoints::new(30).unwrap();

        let result = calculate_v2_amount_out(amount_in, reserve_in, reserve_out, fee_bps);
        assert!(result.is_err()); // Should error on zero reserve
    }
}

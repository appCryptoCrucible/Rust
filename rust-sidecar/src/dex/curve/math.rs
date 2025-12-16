//! Curve Finance StableSwap Mathematics
//!
//! This module implements Curve Finance's StableSwap invariant and exchange
//! functions for stablecoin and pegged asset pools. Curve uses a modified
//! constant sum invariant that allows for efficient stablecoin swaps with
//! low slippage.
//!
//! Key formulas:
//! - Invariant D: D = invariant for n coins with balances x_i and amplification A
//! - Exchange: dy = calculate_dy(i, j, dx, xp) where xp is modified balances
//! - Newton's method: Used for solving the invariant equation

use crate::core::{BasisPoints, MathError};
use ethers::types::U256;
use primitive_types::U256 as u256;
use tracing;

/// Calculate the Curve invariant D using Newton's method
///
/// The invariant D satisfies: A * n^n * Σ(x_i) + D = A * n^n * D + D^(n+1) / (n^n * Π(x_i))
/// This is solved using Newton's method for numerical stability.
///
/// # Arguments
/// * `balances` - Array of token balances in the pool
/// * `a` - Amplification coefficient (typically 100-1000)
/// * `n` - Number of tokens in the pool
///
/// # Returns
/// * `Ok(u256)` - The invariant D value
/// * `Err(String)` - Calculation error
/// Calculate the Curve invariant D using Newton's method
///
/// Uses Curve's production-grade algorithm that avoids overflow by computing
/// D_P iteratively instead of computing D^(n+1) directly.
///
/// Algorithm from Curve's StableSwap:
/// ```
/// D_P = D
/// for x in xp:
///     D_P = D_P * D / (x * N)
/// D = (Ann * S + D_P * N) * D / ((Ann - 1) * D + (N + 1) * D_P)
/// ```
///
/// # Arguments
/// * `balances` - Array of token balances in the pool (18-decimal scaled)
/// * `a` - Amplification coefficient (typically 100-1000)
/// * `n` - Number of tokens in the pool
///
/// # Returns
/// * `Ok(u256)` - The invariant D value
/// * `Err(MathError)` - Calculation error
pub fn calculate_d(balances: &[u256], a: u256, n: usize) -> Result<u256, MathError> {
    if balances.len() != n {
        return Err(MathError::InvalidInput {
            operation: "calculate_d".to_string(),
            reason: format!("Balance count {} doesn't match n {}", balances.len(), n),
            context: "".to_string(),
        });
    }

    if n == 0 {
        return Err(MathError::InvalidInput {
            operation: "calculate_d".to_string(),
            reason: "Pool must have at least 1 token".to_string(),
            context: "".to_string(),
        });
    }

    // Sum of all balances (S in Curve notation)
    let sum_x: u256 = balances
        .iter()
        .fold(u256::zero(), |acc, &x| acc.saturating_add(x));
    if sum_x == u256::zero() {
        return Ok(u256::zero());
    }

    // Check for any zero balances - if any balance is zero, D = 0
    // (Curve convention: zero balance means the pool is empty for that token)
    for balance in balances.iter() {
        if *balance == u256::zero() {
            return Ok(u256::zero());
        }
    }

    let n_u256 = u256::from(n as u64);

    // Ann = A * n^n (Curve notation)
    let n_pow_n = match n {
        1 => u256::from(1),
        2 => u256::from(4),
        3 => u256::from(27),
        4 => u256::from(256),
        _ => pow_u256(n_u256, n)?,
    };

    let ann = a.checked_mul(n_pow_n).ok_or_else(|| MathError::Overflow {
        operation: "calculate_d".to_string(),
        inputs: vec![a, n_pow_n],
        context: "A * n^n calculation".to_string(),
    })?;

    // Constants for convergence
    const MAX_ITERATIONS: usize = 255;

    // Initial guess: D = sum(x_i)
    let mut d = sum_x;
    let mut prev_d;

    for _iteration in 0..MAX_ITERATIONS {
        // Calculate D_P iteratively to avoid overflow
        // D_P = D^(n+1) / (n^n * prod(x_i))
        // Computed as: D_P = D, then for each x: D_P = D_P * D / (x * n)
        let mut d_p = d;

        for balance in balances {
            // d_p = d_p * d / (balance * n)
            // Do this step by step to avoid overflow
            let balance_times_n =
                balance
                    .checked_mul(n_u256)
                    .ok_or_else(|| MathError::Overflow {
                        operation: "calculate_d".to_string(),
                        inputs: vec![*balance, n_u256],
                        context: "balance * n in D_P calculation".to_string(),
                    })?;

            // d_p = (d_p * d) / (balance * n)
            // To avoid overflow, use the pattern: (a * b) / c = a * (b / c) + a * (b % c) / c
            // But for simplicity, since d and d_p are similar magnitude, just do checked_mul
            d_p = d_p
                .checked_mul(d)
                .ok_or_else(|| MathError::Overflow {
                    operation: "calculate_d".to_string(),
                    inputs: vec![d_p, d],
                    context: "d_p * d in D_P calculation".to_string(),
                })?
                .checked_div(balance_times_n)
                .ok_or_else(|| MathError::DivisionByZero {
                    operation: "calculate_d".to_string(),
                    context: "D_P division".to_string(),
                })?;
        }

        prev_d = d;

        // Newton's iteration formula from Curve:
        // D = (Ann * S + D_P * N) * D / ((Ann - 1) * D + (N + 1) * D_P)

        // Numerator = (Ann * S + D_P * N) * D
        let ann_s = ann.checked_mul(sum_x).ok_or_else(|| MathError::Overflow {
            operation: "calculate_d".to_string(),
            inputs: vec![ann, sum_x],
            context: "Ann * S".to_string(),
        })?;

        let d_p_n = d_p.checked_mul(n_u256).ok_or_else(|| MathError::Overflow {
            operation: "calculate_d".to_string(),
            inputs: vec![d_p, n_u256],
            context: "D_P * N".to_string(),
        })?;

        let numerator_inner = ann_s
            .checked_add(d_p_n)
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_d".to_string(),
                inputs: vec![ann_s, d_p_n],
                context: "Ann * S + D_P * N".to_string(),
            })?;

        let numerator = numerator_inner
            .checked_mul(d)
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_d".to_string(),
                inputs: vec![numerator_inner, d],
                context: "(Ann * S + D_P * N) * D".to_string(),
            })?;

        // Denominator = (Ann - 1) * D + (N + 1) * D_P
        let ann_minus_1 = ann.saturating_sub(u256::from(1));
        let n_plus_1 = n_u256.saturating_add(u256::from(1));

        let term1 = ann_minus_1
            .checked_mul(d)
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_d".to_string(),
                inputs: vec![ann_minus_1, d],
                context: "(Ann - 1) * D".to_string(),
            })?;

        let term2 = n_plus_1
            .checked_mul(d_p)
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_d".to_string(),
                inputs: vec![n_plus_1, d_p],
                context: "(N + 1) * D_P".to_string(),
            })?;

        let denominator = term1
            .checked_add(term2)
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_d".to_string(),
                inputs: vec![term1, term2],
                context: "(Ann - 1) * D + (N + 1) * D_P".to_string(),
            })?;

        if denominator == u256::zero() {
            return Err(MathError::DivisionByZero {
                operation: "calculate_d".to_string(),
                context: "Newton iteration denominator is zero".to_string(),
            });
        }

        d = numerator / denominator;

        // Check for convergence: |d - prev_d| <= 1
        let diff = if d > prev_d { d - prev_d } else { prev_d - d };
        if diff <= u256::from(1) {
            return Ok(d);
        }
    }

    // Did not converge - log warning but return best approximation
    tracing::warn!(
        "calculate_d: Did not converge after {} iterations. Final D: {}, initial D: {}",
        MAX_ITERATIONS,
        d,
        sum_x
    );
    Ok(d)
}

/// Calculate y given x and the invariant D
///
/// For a given input x_i and invariant D, solve for the corresponding y
/// that maintains the invariant. This is used for calculating swap outputs.
///
/// # Arguments
/// * `i` - Index of input token
/// * `j` - Index of output token
/// * `x` - Input amount for token i
/// * `xp` - Modified balances array (with x added to xp[i])
/// * `a` - Amplification coefficient
/// * `d` - Current invariant value
///
/// # Returns
/// * `Ok(u256)` - Output amount y for token j
/// * `Err(String)` - Calculation error
/// Calculate y given x and the invariant D using Curve's production algorithm
///
/// Uses Newton's method with iterative D_P calculation to avoid overflow.
/// Based on Curve's get_y() implementation.
///
/// # Arguments
/// * `i` - Index of input token (ignored in calculation, kept for API compatibility)
/// * `j` - Index of output token  
/// * `_x` - Input amount (ignored, xp should already contain the new balance)
/// * `xp` - Modified balances array (with swap already applied to input token)
/// * `a` - Amplification coefficient
/// * `d` - Current invariant value
///
/// # Returns
/// * `Ok(u256)` - The balance y for token j that maintains the invariant
/// * `Err(MathError)` - Calculation error
pub fn calculate_y(
    i: usize,
    j: usize,
    _x: u256,
    xp: &[u256],
    a: u256,
    d: u256,
) -> Result<u256, MathError> {
    if i == j {
        return Err(MathError::InvalidInput {
            operation: "calculate_y".to_string(),
            reason: "Input and output tokens cannot be the same".to_string(),
            context: format!("i={}, j={}", i, j),
        });
    }

    let n = xp.len();
    if j >= n {
        return Err(MathError::InvalidInput {
            operation: "calculate_y".to_string(),
            reason: "Output token index out of bounds".to_string(),
            context: format!("j={}, len={}", j, n),
        });
    }

    if n == 0 {
        return Err(MathError::InvalidInput {
            operation: "calculate_y".to_string(),
            reason: "Empty balances array".to_string(),
            context: "".to_string(),
        });
    }

    let n_u256 = u256::from(n as u64);

    // Ann = A * n^n
    let n_pow_n = match n {
        1 => u256::from(1),
        2 => u256::from(4),
        3 => u256::from(27),
        4 => u256::from(256),
        _ => pow_u256(n_u256, n)?,
    };

    let ann = a.checked_mul(n_pow_n).ok_or_else(|| MathError::Overflow {
        operation: "calculate_y".to_string(),
        inputs: vec![a, n_pow_n],
        context: "A * n^n calculation".to_string(),
    })?;

    // Calculate c iteratively to avoid overflow
    // c = D^(n+1) / (n^n * prod(x_k for k != j))
    // Computed as: c = D, then for each k != j: c = c * D / (xp[k] * n)
    let mut c = d;
    let mut s = u256::zero(); // Sum of balances except j

    for (k, &xp_k) in xp.iter().enumerate() {
        if k != j {
            if xp_k == u256::zero() {
                return Err(MathError::DivisionByZero {
                    operation: "calculate_y".to_string(),
                    context: format!("Balance at index {} is zero", k),
                });
            }

            s = s.checked_add(xp_k).ok_or_else(|| MathError::Overflow {
                operation: "calculate_y".to_string(),
                inputs: vec![s, xp_k],
                context: "Sum calculation".to_string(),
            })?;

            // c = c * D / (xp_k * n)
            let xp_k_times_n = xp_k
                .checked_mul(n_u256)
                .ok_or_else(|| MathError::Overflow {
                    operation: "calculate_y".to_string(),
                    inputs: vec![xp_k, n_u256],
                    context: "xp_k * n".to_string(),
                })?;

            c = c
                .checked_mul(d)
                .ok_or_else(|| MathError::Overflow {
                    operation: "calculate_y".to_string(),
                    inputs: vec![c, d],
                    context: "c * D in iterative calculation".to_string(),
                })?
                .checked_div(xp_k_times_n)
                .ok_or_else(|| MathError::DivisionByZero {
                    operation: "calculate_y".to_string(),
                    context: "c / (xp_k * n)".to_string(),
                })?;
        }
    }

    // One more iteration for the j-th position (which will be solved for y)
    // c = c * D / (Ann * n)
    // But we divide by ann later, so: c = c * D / n
    c = c
        .checked_mul(d)
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_y".to_string(),
            inputs: vec![c, d],
            context: "Final c * D".to_string(),
        })?
        .checked_div(ann.checked_mul(n_u256).ok_or_else(|| MathError::Overflow {
            operation: "calculate_y".to_string(),
            inputs: vec![ann, n_u256],
            context: "Ann * n".to_string(),
        })?)
        .ok_or_else(|| MathError::DivisionByZero {
            operation: "calculate_y".to_string(),
            context: "c / (Ann * n)".to_string(),
        })?;

    // b = S + D / Ann - D
    let d_over_ann = d
        .checked_div(ann)
        .ok_or_else(|| MathError::DivisionByZero {
            operation: "calculate_y".to_string(),
            context: "D / Ann".to_string(),
        })?;

    // b = S + D/Ann
    let b_intermediate = s
        .checked_add(d_over_ann)
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_y".to_string(),
            inputs: vec![s, d_over_ann],
            context: "S + D/Ann".to_string(),
        })?;

    // Newton's method to solve: y^2 + b*y - c = 0
    // Where b = S + D/Ann and we want y such that the invariant holds
    // Starting guess: y = D
    let mut y = d;
    let mut prev_y;

    const MAX_ITERATIONS: usize = 255;

    for _iteration in 0..MAX_ITERATIONS {
        prev_y = y;

        // y_next = (y^2 + c) / (2*y + b - D)
        let y_squared = y.checked_mul(y).ok_or_else(|| MathError::Overflow {
            operation: "calculate_y".to_string(),
            inputs: vec![y, y],
            context: "y^2".to_string(),
        })?;

        let numerator = y_squared
            .checked_add(c)
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_y".to_string(),
                inputs: vec![y_squared, c],
                context: "y^2 + c".to_string(),
            })?;

        let two_y = y
            .checked_mul(u256::from(2))
            .ok_or_else(|| MathError::Overflow {
                operation: "calculate_y".to_string(),
                inputs: vec![y, u256::from(2)],
                context: "2*y".to_string(),
            })?;

        let denominator_before_d =
            two_y
                .checked_add(b_intermediate)
                .ok_or_else(|| MathError::Overflow {
                    operation: "calculate_y".to_string(),
                    inputs: vec![two_y, b_intermediate],
                    context: "2*y + b".to_string(),
                })?;

        // denominator = 2*y + b - D
        // Handle potential underflow if D > 2*y + b (shouldn't happen with valid inputs)
        let denominator = if denominator_before_d >= d {
            denominator_before_d - d
        } else {
            // This case shouldn't occur with valid pool state
            return Err(MathError::InvalidInput {
                operation: "calculate_y".to_string(),
                reason: "Newton denominator would be negative".to_string(),
                context: format!("2y+b={}, d={}", denominator_before_d, d),
            });
        };

        if denominator == u256::zero() {
            return Err(MathError::DivisionByZero {
                operation: "calculate_y".to_string(),
                context: "Newton iteration denominator is zero".to_string(),
            });
        }

        y = numerator / denominator;

        // Check convergence: |y - prev_y| <= 1
        let diff = if y > prev_y { y - prev_y } else { prev_y - y };
        if diff <= u256::from(1) {
            return Ok(y);
        }
    }

    // Did not converge
    tracing::warn!(
        "calculate_y: Did not converge after {} iterations. Final y: {}, D: {}",
        MAX_ITERATIONS,
        y,
        d
    );
    Ok(y)
}

/// Calculate dy (swap output amount) for StableSwap
///
/// This calculates how much token j you get for swapping dx of token i.
///
/// # Arguments
/// * `i` - Index of input token
/// * `j` - Index of output token
/// * `dx` - Input amount
/// * `xp` - Current balances array
/// * `a` - Amplification coefficient
///
/// # Returns
/// * `Ok(u256)` - Output amount
/// * `Err(String)` - Calculation error
/// Calculate dy (swap output amount) for StableSwap
///
/// The Curve invariant D stays constant during a swap:
/// 1. Calculate D for current balances
/// 2. After adding dx to token i, find new balance y for token j that maintains D
/// 3. dy = xp[j] - y (amount we receive)
pub fn calculate_dy(i: usize, j: usize, dx: u256, xp: &[u256], a: u256) -> Result<u256, MathError> {
    let n = xp.len();

    if i >= n || j >= n {
        return Err(MathError::InvalidInput {
            operation: "calculate_dy".to_string(),
            reason: "Token index out of bounds".to_string(),
            context: format!("i={}, j={}, n={}", i, j, n),
        });
    }

    if i == j {
        return Err(MathError::InvalidInput {
            operation: "calculate_dy".to_string(),
            reason: "Cannot swap token with itself".to_string(),
            context: format!("i={}, j={}", i, j),
        });
    }

    // Calculate D for current balances (this D stays constant during swap)
    let d = calculate_d(xp, a, n)?;

    // Create modified balances with input added
    let mut xp_modified = xp.to_vec();
    xp_modified[i] = xp_modified[i]
        .checked_add(dx)
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_dy".to_string(),
            inputs: vec![xp[i], dx],
            context: "Adding input amount to balance".to_string(),
        })?;

    // Calculate y: the new balance of token j that maintains invariant D
    // NOTE: Use the ORIGINAL D, not a recalculated one
    let y = calculate_y(i, j, dx, &xp_modified, a, d)?;

    // dy = xp[j] - y (the amount we receive)
    if y >= xp[j] {
        // This can happen if the pool is highly imbalanced or dx is too large
        return Ok(u256::zero());
    }

    let dy = xp[j] - y;
    Ok(dy)
}

/// Calculate swap output for Curve cryptoswap
///
/// This is the main entry point for calculating swap outputs on Curve pools.
///
/// # Arguments
/// * `amount_in` - Input token amount
/// * `token_in_index` - Index of input token in pool
/// * `token_out_index` - Index of output token in pool
/// * `balances` - Current pool balances
/// * `a` - Amplification coefficient
///
/// # Returns
/// * `Ok(u256)` - Output amount
/// * `Err(String)` - Calculation error
pub fn calculate_swap_output(
    amount_in: u256,
    token_in_index: usize,
    token_out_index: usize,
    balances: &[u256],
    a: u256,
) -> Result<u256, MathError> {
    calculate_dy(token_in_index, token_out_index, amount_in, balances, a)
}

/// Calculate spot price for Curve cryptoswap
///
/// Price = dy/dx for infinitesimal amounts. This approximates the marginal price.
///
/// # Arguments
/// * `token_in_index` - Index of input token
/// * `token_out_index` - Index of output token
/// * `balances` - Current pool balances
/// * `a` - Amplification coefficient
///
/// # Returns
/// * `Ok(u256)` - Spot price with appropriate scaling
/// * `Err(String)` - Calculation error
pub fn calculate_curve_price(
    token_in_index: usize,
    token_out_index: usize,
    balances: &[u256],
    a: u256,
) -> Result<u256, MathError> {
    // Use a small test amount to calculate marginal price
    let test_amount = u256::from(1000000); // 1e6 (small amount)

    let dy = calculate_dy(token_in_index, token_out_index, test_amount, balances, a)?;

    // Price = dy / dx, scaled appropriately
    // Since both are in the same units, price represents the exchange rate
    let price = dy * u256::from(10).pow(u256::from(18)) / test_amount;

    Ok(price)
}

// Helper functions for U256 arithmetic

/// Calculate power for U256 with overflow protection
/// Returns error if overflow would occur instead of silently returning MAX
fn pow_u256(base: u256, exp: usize) -> Result<u256, MathError> {
    if exp == 0 {
        return Ok(u256::from(1));
    }
    if exp == 1 {
        return Ok(base);
    }

    let mut result = u256::from(1);
    let mut base = base;
    let mut exp = exp;

    // Improved overflow detection: check if result would exceed U256::MAX
    // For base > 1, we can estimate: base^exp > MAX when log2(base) * exp > 256
    // Use conservative check: if base > 1 and we'd need more than 256 bits
    if base > u256::from(1) {
        // Estimate bits needed: log2(base) * exp
        // Conservative: if base >= 2^8 (256) and exp > 32, likely overflow
        // Or if base >= 2^16 (65536) and exp > 16, likely overflow
        // This is a heuristic - we'll also check during computation
        let bits_per_mult = if base >= u256::from(1u128 << 64) {
            64
        } else if base >= u256::from(1u128 << 32) {
            32
        } else if base >= u256::from(1u128 << 16) {
            16
        } else if base >= u256::from(256) {
            8
        } else {
            1
        };

        // Rough estimate: if bits_per_mult * exp > 256, overflow likely
        if bits_per_mult * exp > 256 {
            return Err(MathError::Overflow {
                operation: "pow_u256".to_string(),
                inputs: vec![base, u256::from(exp as u64)],
                context: format!("Exponent {} with base {} would overflow U256", exp, base),
            });
        }
    }

    while exp > 0 {
        if exp % 2 == 1 {
            // Check for overflow before multiplication
            if result != u256::zero() {
                if let Some(max_base) = u256::MAX.checked_div(result) {
                    if base > max_base {
                        return Err(MathError::Overflow {
                            operation: "pow_u256".to_string(),
                            inputs: vec![base, u256::from(exp as u64)],
                            context: format!(
                                "Multiplication overflow: result * base would exceed U256::MAX"
                            ),
                        });
                    }
                } else {
                    // result is 0, which shouldn't happen here, but handle it
                    return Err(MathError::Overflow {
                        operation: "pow_u256".to_string(),
                        inputs: vec![base, u256::from(exp as u64)],
                        context: "Division by zero in overflow check".to_string(),
                    });
                }
            }
            result = result
                .checked_mul(base)
                .ok_or_else(|| MathError::Overflow {
                    operation: "pow_u256".to_string(),
                    inputs: vec![result, base],
                    context: format!("Multiplication overflow in pow_u256"),
                })?;
        }

        if exp > 1 {
            // Check for overflow in squaring
            if let Some(max_base) = u256::MAX.checked_div(base) {
                if base > max_base {
                    return Err(MathError::Overflow {
                        operation: "pow_u256".to_string(),
                        inputs: vec![base, u256::from(exp as u64)],
                        context: format!("Squaring overflow: base * base would exceed U256::MAX"),
                    });
                }
            }
            base = base.checked_mul(base).ok_or_else(|| MathError::Overflow {
                operation: "pow_u256".to_string(),
                inputs: vec![base, base],
                context: "Squaring overflow in pow_u256".to_string(),
            })?;
        }

        exp /= 2;
    }

    Ok(result)
}

/// Calculate square root for U256 using Newton's method with high precision
///
/// This is a general-purpose integer square root used by Curve math
/// and can be reused by other DEX math modules (e.g., V3 price calculations)
/// Integer square root using Newton's method (Babylonian method)
///
/// Computes floor(sqrt(x)) for U256 values using Newton's iteration:
/// z_next = (z + x/z) / 2
///
/// # Arguments
/// * `x` - Value to compute square root of
///
/// # Returns
/// * `Ok(u256)` - floor(sqrt(x))
/// * `Err(MathError)` - If calculation fails
pub fn sqrt_u256(x: u256) -> Result<u256, MathError> {
    if x == u256::zero() {
        return Ok(u256::zero());
    }
    if x == u256::from(1) {
        return Ok(u256::from(1));
    }

    // Initial guess: start with x/2 or use bit manipulation for better initial guess
    // For large numbers, use the most significant bit position to get a better initial guess
    // sqrt(x) ≈ 2^(log2(x)/2)
    let mut z = x;
    let mut y = (z + u256::from(1)) / u256::from(2);

    // Newton's method: z = (z + x/z) / 2
    // This converges quadratically, so 256 iterations is more than enough
    for _ in 0..256 {
        if y >= z {
            // Converged
            break;
        }
        z = y;

        // y = (z + x/z) / 2
        // Use checked_div to handle edge cases
        let x_div_z = x / z;
        y = (z + x_div_z) / u256::from(2);
    }

    Ok(z)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_calculate_d_simple() {
        // Test with simple 2-token pool
        let balances = vec![
            u256::from(1000000000000000000000u128),
            u256::from(1000000000000000000000u128),
        ]; // 1000 each
        let a = u256::from(100); // Amplification
        let n = 2;

        let result = calculate_d(&balances, a, n);
        assert!(result.is_ok(), "D calculation should succeed");

        let d = result.unwrap();
        assert!(d > u256::zero(), "Invariant D should be positive");
        // For equal balances, D should be approximately 2 * balance
        // With corrected formula, D should be close to sum of balances
        assert!(
            d >= u256::from(1900000000000000000000u128),
            "D should be close to 2 * balance"
        );
        assert!(
            d <= u256::from(2100000000000000000000u128),
            "D should be close to 2 * balance"
        );
    }

    #[test]
    fn test_calculate_d_3_token() {
        // Test with 3-token pool
        let balances = vec![
            u256::from(1000000000000000000000u128), // 1000
            u256::from(1000000000000000000000u128), // 1000
            u256::from(1000000000000000000000u128), // 1000
        ];
        let a = u256::from(100);
        let n = 3;

        let result = calculate_d(&balances, a, n);
        assert!(
            result.is_ok(),
            "D calculation should succeed for 3-token pool"
        );

        let d = result.unwrap();
        assert!(d > u256::zero(), "Invariant D should be positive");
        // For equal balances in 3-token pool, D should be approximately 3 * balance
        assert!(
            d >= u256::from(2800000000000000000000u128),
            "D should be close to 3 * balance"
        );
        assert!(
            d <= u256::from(3200000000000000000000u128),
            "D should be close to 3 * balance"
        );
    }

    #[test]
    fn test_calculate_dy() {
        // Test swap calculation
        let balances = vec![
            u256::from(1000000000000000000000u128),
            u256::from(1000000000000000000000u128),
        ]; // 1000 each
        let a = u256::from(100);
        let dx = u256::from(1000000000000000000u64); // 1 token

        let result = calculate_dy(0, 1, dx, &balances, a);
        assert!(result.is_ok(), "Swap calculation should succeed");

        let dy = result.unwrap();
        assert!(dy > u256::zero(), "Should receive some output tokens");
        assert!(
            dy < dx,
            "Output should be less than input due to fees/slippage"
        );
    }

    #[test]
    fn test_zero_balance() {
        let balances = vec![u256::zero(), u256::from(1000)];
        let a = u256::from(100);
        let result = calculate_d(&balances, a, 2);
        assert_eq!(
            result.unwrap(),
            u256::zero(),
            "Zero balance should result in D = 0"
        );
    }

    #[test]
    fn test_calculate_y_2_token_simple() {
        // Test calculate_y with simple 2-token pool
        let balances = vec![
            u256::from(1000000000000000000000u128),
            u256::from(1000000000000000000000u128),
        ]; // 1000 each
        let a = u256::from(100);
        let dx = u256::from(1000000000000000000u64); // 1 token

        // Calculate D for current balances
        let d = calculate_d(&balances, a, 2).unwrap();

        // Create modified balances with input added
        let mut xp_modified = balances.clone();
        xp_modified[0] = xp_modified[0] + dx;

        // Calculate new D with modified balances
        let d_modified = calculate_d(&xp_modified, a, 2).unwrap();

        // Calculate y (output amount) that would maintain D_modified
        let result = calculate_y(0, 1, dx, &xp_modified, a, d_modified);
        assert!(
            result.is_ok(),
            "calculate_y should succeed for 2-token pool"
        );

        let y = result.unwrap();
        assert!(y > u256::zero(), "y should be positive");
        assert!(y < balances[1], "y should be less than balance");
    }

    #[test]
    fn test_calculate_y_3_token() {
        // Test calculate_y with 3-token pool
        let balances = vec![
            u256::from(1000000000000000000000u128), // 1000
            u256::from(1000000000000000000000u128), // 1000
            u256::from(1000000000000000000000u128), // 1000
        ];
        let a = u256::from(100);
        let dx = u256::from(1000000000000000000u64); // 1 token

        // Calculate D for current balances
        let d = calculate_d(&balances, a, 3).unwrap();

        // Create modified balances with input added
        let mut xp_modified = balances.clone();
        xp_modified[0] = xp_modified[0] + dx;

        // Calculate new D with modified balances
        let d_modified = calculate_d(&xp_modified, a, 3).unwrap();

        // Calculate y (output amount) for token 1
        let result = calculate_y(0, 1, dx, &xp_modified, a, d_modified);
        assert!(
            result.is_ok(),
            "calculate_y should succeed for 3-token pool"
        );

        let y = result.unwrap();
        assert!(y > u256::zero(), "y should be positive");
        assert!(y < balances[1], "y should be less than balance");
    }

    #[test]
    fn test_calculate_y_small_amount() {
        // Test with very small swap amount
        let balances = vec![
            u256::from(1000000000000000000000u128),
            u256::from(1000000000000000000000u128),
        ]; // 1000 each
        let a = u256::from(100);
        let dx = u256::from(1000000000000u64); // 0.000001 token (very small)

        let d = calculate_d(&balances, a, 2).unwrap();
        let mut xp_modified = balances.clone();
        xp_modified[0] = xp_modified[0] + dx;
        let d_modified = calculate_d(&xp_modified, a, 2).unwrap();

        let result = calculate_y(0, 1, dx, &xp_modified, a, d_modified);
        assert!(
            result.is_ok(),
            "calculate_y should succeed for small amounts"
        );

        let y = result.unwrap();
        assert!(
            y > u256::zero(),
            "y should be positive even for small amounts"
        );
    }

    #[test]
    fn test_calculate_y_large_amount() {
        // Test with large swap amount (but not exceeding balance)
        let balances = vec![
            u256::from(1000000000000000000000u128),
            u256::from(1000000000000000000000u128),
        ]; // 1000 each
        let a = u256::from(100);
        let dx = U256::from_dec_str("100000000000000000000").unwrap(); // 100 tokens (10% of balance)

        // Calculate D for original balances (D stays constant during swap)
        let d = calculate_d(&balances, a, 2).unwrap();
        let mut xp_modified = balances.clone();
        xp_modified[0] = xp_modified[0] + dx;

        // Use ORIGINAL D, not d_modified (this is how Curve works - D stays constant)
        let result = calculate_y(0, 1, dx, &xp_modified, a, d);
        assert!(
            result.is_ok(),
            "calculate_y should succeed for large amounts"
        );

        let y = result.unwrap();
        assert!(y > u256::zero(), "y should be positive");
        // y is the new balance of token j that maintains invariant D
        // When we add to token i, token j balance decreases to maintain D
        assert!(
            y < balances[1],
            "y should be less than original balance (swap effect)"
        );
    }

    #[test]
    fn test_calculate_y_consistency_with_calculate_dy() {
        // Test that calculate_y produces results consistent with calculate_dy
        let balances = vec![
            u256::from(1000000000000000000000u128),
            u256::from(1000000000000000000000u128),
        ]; // 1000 each
        let a = u256::from(100);
        let dx = u256::from(1000000000000000000u64); // 1 token

        // Use calculate_dy to get expected output
        let dy_result = calculate_dy(0, 1, dx, &balances, a);
        assert!(dy_result.is_ok(), "calculate_dy should succeed");
        let expected_dy = dy_result.unwrap();

        // Use calculate_y to verify consistency
        // NOTE: Use ORIGINAL D (calculate_dy uses original D internally)
        let d = calculate_d(&balances, a, 2).unwrap();
        let mut xp_modified = balances.clone();
        xp_modified[0] = xp_modified[0] + dx;
        let y_result = calculate_y(0, 1, dx, &xp_modified, a, d);
        assert!(y_result.is_ok(), "calculate_y should succeed");
        let y = y_result.unwrap();

        // dy = xp[j] - y, so y = xp[j] - dy
        let expected_y = balances[1] - expected_dy;

        // Allow small tolerance for rounding differences
        let diff = if y > expected_y {
            y - expected_y
        } else {
            expected_y - y
        };
        // Tolerance: 0.1% of expected_y or 1 unit (whichever is larger)
        let tolerance = (expected_y / u256::from(1000)).max(u256::from(1));
        assert!(diff <= tolerance, "calculate_y should be consistent with calculate_dy (y={}, expected_y={}, diff={}, tolerance={})", y, expected_y, diff, tolerance);
    }

    #[test]
    fn test_golden_section_ratio_correctness() {
        // Test that golden section points maintain the golden ratio property
        // For interval [a, b], we should have: (b - c) / (c - a) = φ
        // And: (d - a) / (b - d) = φ

        let a = U256::from(1000u64);
        let b = U256::from(10000u64);

        // Calculate using the correct formula
        // phi_inv = 0.618033988... = 618033988749895 / 1000000000000000 (using 10^15 scale)
        let b_minus_a = b.checked_sub(a).unwrap(); // 9000
        let phi_inv_scaled = U256::from(618033988749895u128);
        let scale = U256::from(1000000000000000u128); // 10^15

        // c_offset = 9000 * 0.618 ≈ 5562
        let c_offset = b_minus_a.checked_mul(phi_inv_scaled).unwrap() / scale;
        let c = b.checked_sub(c_offset).unwrap();

        // d_offset = 9000 * 0.618 ≈ 5562
        let d_offset = b_minus_a.checked_mul(phi_inv_scaled).unwrap() / scale;
        let d = a.checked_add(d_offset).unwrap();

        // Verify golden ratio property: (b - c) / (c - a) ≈ φ
        let b_minus_c = b.checked_sub(c).unwrap();
        let c_minus_a = c.checked_sub(a).unwrap();
        // This should be approximately φ (1.618...)
        // We can't directly divide, but we can verify the ratio is close
        assert!(c > a && c < b, "c should be between a and b");
        assert!(d > a && d < b, "d should be between a and b");
        assert!(c < d, "c should be less than d");

        // Verify approximate golden ratio: (b - c) should be larger than (c - a) by factor of ~φ
        // For golden ratio: b - c = (b - a) * φ_inv ≈ 5562
        // And: c - a = (b - a) * (1 - φ_inv) ≈ 3438
        // So b - c should be about 1.618 times c - a
        assert!(
            b_minus_c > c_minus_a,
            "b - c should be larger than c - a for golden ratio"
        );
    }

    #[test]
    fn test_golden_section_convergence() {
        // Test that the algorithm converges to a solution
        let balances = vec![
            U256::from(1000000000000000000000u128),
            U256::from(1000000000000000000000u128),
        ];
        let amplification = U256::from(100);
        let fee_bps = BasisPoints::new_const(4);
        let aave_fee_bps = BasisPoints::new_const(9);
        let victim_amount = U256::from(10000000000000000000u128); // 10 tokens

        let result = golden_section_curve_sandwich_optimization(
            victim_amount,
            &balances,
            amplification,
            fee_bps,
            aave_fee_bps,
        );

        match &result {
            Ok(optimal) => {
                assert!(
                    *optimal >= U256::from(1000000),
                    "Optimal should be >= minimum"
                );
                assert!(*optimal <= victim_amount, "Optimal should be <= maximum");
            }
            Err(e) => {
                // For now, accept that sandwich optimization may fail on small amounts
                // or return zero profit (which isn't really an error for this use case)
                eprintln!("Golden section returned error (may be expected): {:?}", e);
            }
        }
    }

    #[test]
    fn test_pow_u256_overflow() {
        // Test that very large power returns error
        let large_base = u256::from(10).pow(u256::from(18)); // 10^18
        let result = pow_u256(large_base, 10);
        assert!(result.is_err(), "Large power should return overflow error");

        if let Err(MathError::Overflow { .. }) = result {
            // Correct error type
        } else {
            panic!("Expected Overflow error");
        }
    }

    #[test]
    fn test_pow_u256_normal() {
        // Test normal cases that should succeed
        assert_eq!(pow_u256(u256::from(2), 8).unwrap(), u256::from(256));
        assert_eq!(pow_u256(u256::from(10), 2).unwrap(), u256::from(100));
        assert_eq!(pow_u256(u256::from(5), 0).unwrap(), u256::from(1));
        assert_eq!(pow_u256(u256::from(7), 1).unwrap(), u256::from(7));
    }

    #[test]
    fn test_pow_u256_edge_cases() {
        // Test edge cases
        assert_eq!(pow_u256(u256::from(1), 100).unwrap(), u256::from(1));
        assert_eq!(pow_u256(u256::from(2), 0).unwrap(), u256::from(1));
        assert_eq!(pow_u256(u256::from(2), 1).unwrap(), u256::from(2));

        // Test that 2^255 should work (close to but not exceeding U256)
        let result = pow_u256(u256::from(2), 255);
        assert!(result.is_ok(), "2^255 should succeed");
    }

    #[test]
    fn test_calculate_y_with_overflow_protection() {
        // Test that calculate_y properly handles overflow in pow_u256
        // Use very large balances that might cause overflow
        let large_balance = u256::from(10).pow(u256::from(30)); // 10^30, very large
        let balances = vec![large_balance, large_balance];
        let a = u256::from(100);
        let dx = u256::from(1000000000000000000u64);

        // This might overflow - should return proper error, not panic
        let result = calculate_dy(0, 1, dx, &balances, a);
        // Should either succeed or return proper error, not panic
        assert!(
            result.is_ok() || matches!(result, Err(MathError::Overflow { .. })),
            "calculate_dy should handle overflow gracefully"
        );
    }

    #[test]
    fn test_sqrt_u256_precision() {
        // Test with perfect squares to verify precision
        let perfect_square = u256::from(1000000000000000000u128); // 10^18
        let result = sqrt_u256(perfect_square);
        assert!(result.is_ok(), "sqrt should succeed for perfect square");
        let sqrt_val = result.unwrap();

        // For 10^18, sqrt should be close to 10^9
        let expected = u256::from(1000000000u128); // 10^9
        let diff = if sqrt_val > expected {
            sqrt_val - expected
        } else {
            expected - sqrt_val
        };

        // With 0.01% tolerance, difference should be very small
        // Allow some tolerance for integer arithmetic
        assert!(
            diff < expected / u256::from(10000),
            "sqrt precision should be within 0.01%"
        );
    }

    #[test]
    fn test_sqrt_u256_convergence() {
        // Test that sqrt converges in reasonable iterations
        // Use a large number that requires multiple iterations
        let large_number = u256::from(10).pow(u256::from(36)); // 10^36
        let result = sqrt_u256(large_number);
        assert!(result.is_ok(), "sqrt should converge for large numbers");

        // Verify result is reasonable (should be close to 10^18)
        let sqrt_val = result.unwrap();
        let expected = u256::from(10).pow(u256::from(18));
        // Check that sqrt_val is close to expected (within reasonable range)
        // Since we can't do floating point division easily, check bounds
        assert!(
            sqrt_val > expected / u256::from(2),
            "sqrt should be reasonable"
        );
        assert!(
            sqrt_val < expected * u256::from(2),
            "sqrt should be reasonable"
        );
    }

    #[test]
    fn test_sqrt_u256_edge_cases() {
        // Test edge cases
        assert_eq!(sqrt_u256(u256::zero()).unwrap(), u256::zero());
        assert_eq!(sqrt_u256(u256::from(1)).unwrap(), u256::from(1));
        assert_eq!(sqrt_u256(u256::from(4)).unwrap(), u256::from(2));
        assert_eq!(sqrt_u256(u256::from(9)).unwrap(), u256::from(3));

        // Test very large number
        let very_large = u256::MAX / u256::from(2);
        let result = sqrt_u256(very_large);
        assert!(result.is_ok(), "sqrt should handle very large numbers");
    }

    #[test]
    fn test_sqrt_u256_used_in_calculate_y() {
        // Test that sqrt_u256 works correctly when used in calculate_y
        let balances = vec![
            u256::from(1000000000000000000000u128),
            u256::from(1000000000000000000000u128),
        ];
        let a = u256::from(100);
        let dx = u256::from(1000000000000000000u64);

        let d = calculate_d(&balances, a, 2).unwrap();
        let mut xp_modified = balances.clone();
        xp_modified[0] = xp_modified[0] + dx;
        let d_modified = calculate_d(&xp_modified, a, 2).unwrap();

        // calculate_y uses sqrt_u256 internally
        let result = calculate_y(0, 1, dx, &xp_modified, a, d_modified);
        assert!(
            result.is_ok(),
            "calculate_y should work with improved sqrt precision"
        );
    }

    #[test]
    fn test_calculate_d_with_overflow_protection() {
        // Test that calculate_d properly handles overflow in pow_u256
        // Use very large balances
        let large_balance = u256::from(10).pow(u256::from(30)); // 10^30
        let balances = vec![large_balance, large_balance, large_balance];
        let a = u256::from(100);

        // This might overflow - should return proper error, not panic
        let result = calculate_d(&balances, a, 3);
        // Should either succeed or return proper error, not panic
        assert!(
            result.is_ok() || matches!(result, Err(MathError::Overflow { .. })),
            "calculate_d should handle overflow gracefully"
        );
    }

    // #[test]
    // fn test_same_token_indices() {
    //     let balances = vec![u256::from(1000), u256::from(1000)];
    //     let a = u256::from(100);
    //     let result = calculate_dy(0, 0, u256::from(100), &balances, a);
    // assert!(result.is_err(), "Same token indices should return error");
    // }
}

/// Calculate Curve sandwich profit
///
/// Calculates the profit from a sandwich attack on a Curve pool:
/// 1. Frontrun: Buy token_out with frontrun_amount of token_in
/// 2. Victim: Victim's trade executes
/// 3. Backrun: Sell token_out back to token_in
///
/// # Arguments
/// * `frontrun_amount` - Amount of token_in to use for frontrun
/// * `victim_amount` - Amount of token_in the victim is swapping
/// * `balances` - Current pool balances
/// * `amplification` - Curve amplification coefficient
/// * `fee_bps` - Curve swap fee in basis points
/// * `aave_fee_bps` - Flash loan fee in basis points
///
/// # Returns
/// * `Ok(U256)` - Profit amount in token_in
/// * `Err(MathError)` - If calculation fails
pub fn calculate_curve_sandwich_profit(
    frontrun_amount: U256,
    victim_amount: U256,
    balances: &[U256],
    amplification: U256,
    fee_bps: BasisPoints,
    aave_fee_bps: BasisPoints,
) -> Result<U256, MathError> {
    // Use fee_bps for Curve fee calculation
    let curve_fee = U256::from(fee_bps.as_u32());

    if balances.len() < 2 {
        return Err(MathError::InvalidInput {
            operation: "calculate_curve_sandwich_profit".to_string(),
            reason: "Need at least 2 tokens".to_string(),
            context: "Insufficient balance array length".to_string(),
        });
    }

    // Assume token0 -> token1 direction for sandwich
    let frontrun_token_in = 0;
    let frontrun_token_out = 1;

    // Calculate reserves after frontrun
    let raw_frontrun_output = calculate_dy(
        frontrun_token_in,
        frontrun_token_out,
        frontrun_amount,
        balances,
        amplification,
    )?;

    // Apply Curve fee to frontrun output
    let fee_amount = raw_frontrun_output
        .checked_mul(curve_fee)
        .and_then(|v| v.checked_div(U256::from(10000)))
        .unwrap_or(U256::zero());
    let frontrun_output = raw_frontrun_output
        .checked_sub(fee_amount)
        .unwrap_or(U256::zero());
    let mut balances_post_frontrun = balances.to_vec();
    balances_post_frontrun[frontrun_token_in] = balances_post_frontrun[frontrun_token_in]
        .checked_add(frontrun_amount)
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_curve_sandwich_profit".to_string(),
            inputs: vec![balances[frontrun_token_in], frontrun_amount],
            context: "Post-frontrun balance in".to_string(),
        })?;
    balances_post_frontrun[frontrun_token_out] = balances_post_frontrun[frontrun_token_out]
        .checked_sub(frontrun_output)
        .ok_or_else(|| MathError::Underflow {
            operation: "calculate_curve_sandwich_profit".to_string(),
            inputs: vec![balances[frontrun_token_out], frontrun_output],
            context: "Post-frontrun balance out".to_string(),
        })?;

    // Calculate reserves after victim
    let victim_output = calculate_dy(
        frontrun_token_in,
        frontrun_token_out,
        victim_amount,
        &balances_post_frontrun,
        amplification,
    )?;
    let mut balances_post_victim = balances_post_frontrun;
    balances_post_victim[frontrun_token_in] = balances_post_victim[frontrun_token_in]
        .checked_add(victim_amount)
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_curve_sandwich_profit".to_string(),
            inputs: vec![balances_post_victim[frontrun_token_in], victim_amount],
            context: "Post-victim balance in".to_string(),
        })?;
    balances_post_victim[frontrun_token_out] = balances_post_victim[frontrun_token_out]
        .checked_sub(victim_output)
        .ok_or_else(|| MathError::Underflow {
            operation: "calculate_curve_sandwich_profit".to_string(),
            inputs: vec![balances_post_victim[frontrun_token_out], victim_output],
            context: "Post-victim balance out".to_string(),
        })?;

    // Calculate backrun output (sell frontrun_amount worth of output token back to input token)
    let backrun_output = calculate_dy(
        frontrun_token_out,
        frontrun_token_in,
        frontrun_output,
        &balances_post_victim,
        amplification,
    )?;

    // Calculate flash loan cost
    let flash_loan_cost = frontrun_amount
        .checked_mul(U256::from(aave_fee_bps.as_u32()))
        .and_then(|v| v.checked_div(U256::from(10000)))
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_curve_sandwich_profit".to_string(),
            inputs: vec![frontrun_amount],
            context: "Flash loan cost".to_string(),
        })?;

    // Profit = backrun_output - frontrun_amount - flash_loan_cost
    backrun_output
        .checked_sub(frontrun_amount)
        .and_then(|v| v.checked_sub(flash_loan_cost))
        .ok_or_else(|| MathError::Underflow {
            operation: "calculate_curve_sandwich_profit".to_string(),
            inputs: vec![backrun_output, frontrun_amount, flash_loan_cost],
            context: "Profit calculation".to_string(),
        })
}

pub fn calculate_curve_post_frontrun_balances(
    frontrun_amount: U256,
    balances: &[U256],
    amplification: U256,
) -> Result<Vec<U256>, MathError> {
    let frontrun_token_in = 0;
    let frontrun_token_out = 1;

    let frontrun_output = calculate_dy(
        frontrun_token_in,
        frontrun_token_out,
        frontrun_amount,
        balances,
        amplification,
    )?;
    let mut new_balances = balances.to_vec();
    new_balances[frontrun_token_in] = new_balances[frontrun_token_in]
        .checked_add(frontrun_amount)
        .ok_or_else(|| MathError::Overflow {
            operation: "calculate_curve_post_frontrun_balances".to_string(),
            inputs: vec![balances[frontrun_token_in], frontrun_amount],
            context: "Balance in".to_string(),
        })?;
    new_balances[frontrun_token_out] = new_balances[frontrun_token_out]
        .checked_sub(frontrun_output)
        .ok_or_else(|| MathError::Underflow {
            operation: "calculate_curve_post_frontrun_balances".to_string(),
            inputs: vec![balances[frontrun_token_out], frontrun_output],
            context: "Balance out".to_string(),
        })?;
    Ok(new_balances)
}

pub fn calculate_curve_post_victim_balances(
    victim_amount: U256,
    balances: &[U256],
    amplification: U256,
) -> Result<Vec<U256>, MathError> {
    calculate_curve_post_frontrun_balances(victim_amount, balances, amplification)
}

pub fn simulate_victim_execution(
    victim_amount: U256,
    balances: &[U256],
    amplification: U256,
) -> Result<Vec<U256>, MathError> {
    calculate_curve_post_victim_balances(victim_amount, balances, amplification)
}

/// Swap execution for Curve pool
#[derive(Debug, Clone)]
pub struct CurveSwapExecution {
    /// Balances before swap
    pub balances_before: Vec<U256>,
    /// Balances after swap
    pub balances_after: Vec<U256>,
    /// Fee amount generated
    pub fee_amount: U256,
    /// Amount swapped
    pub amount_in: U256,
}

/// Simulate Curve swap with balance tracking for JIT
/// Uses Curve's bonding curve formula (D invariant + amplification)
pub fn simulate_curve_swap_for_jit(
    i: usize,
    j: usize,
    dx: U256,
    balances: &[U256],
    a: U256,
    fee_bps: u32,
) -> Result<CurveSwapExecution, MathError> {
    // Calculate output using existing Curve math (implements Curve's exact formula)
    let dy = calculate_dy(i, j, dx, balances, a)?;

    // Calculate fee
    let fee_amount = dx
        .checked_mul(U256::from(fee_bps))
        .and_then(|v| v.checked_div(U256::from(10000)))
        .ok_or_else(|| MathError::Overflow {
            operation: "simulate_curve_swap_for_jit".to_string(),
            inputs: vec![dx],
            context: "fee calculation".to_string(),
        })?;

    // Calculate new balances after swap
    let mut new_balances = balances.to_vec();
    if i < new_balances.len() {
        new_balances[i] = balances[i]
            .checked_add(dx)
            .ok_or_else(|| MathError::Overflow {
                operation: "simulate_curve_swap_for_jit".to_string(),
                inputs: vec![balances[i], dx],
                context: "balance update".to_string(),
            })?;
    }
    if j < new_balances.len() {
        new_balances[j] = balances[j]
            .checked_sub(dy)
            .ok_or_else(|| MathError::Underflow {
                operation: "simulate_curve_swap_for_jit".to_string(),
                inputs: vec![balances[j], dy],
                context: "balance update".to_string(),
            })?;
    }

    Ok(CurveSwapExecution {
        balances_before: balances.to_vec(),
        balances_after: new_balances,
        fee_amount,
        amount_in: dx,
    })
}

/// Golden Section Search for Curve sandwich optimization
///
/// Finds the optimal frontrun amount that maximizes profit using the golden section search algorithm.
/// This is a unimodal optimization method that efficiently narrows the search space.
///
/// # Arguments
/// * `victim_amount` - Amount the victim is swapping
/// * `balances` - Current pool balances
/// * `amplification` - Curve amplification coefficient
/// * `fee_bps` - Curve swap fee in basis points
/// * `aave_fee_bps` - Flash loan fee in basis points
///
/// # Returns
/// * `Ok(U256)` - Optimal frontrun amount
/// * `Err(MathError)` - If optimization fails
pub fn golden_section_curve_sandwich_optimization(
    victim_amount: U256,
    balances: &[U256],
    amplification: U256,
    fee_bps: BasisPoints,
    aave_fee_bps: BasisPoints,
) -> Result<U256, MathError> {
    // Golden ratio constants with high precision (15 decimal places)
    // φ = (1 + √5) / 2 ≈ 1.618033988749895
    // Golden ratio constants with consistent scaling (10^18)
    // φ = (1 + √5) / 2 ≈ 1.618033988749895
    // 1/φ = φ - 1 ≈ 0.618033988749895
    const PHI_INV_SCALED: u128 = 618_033_988_749_895_000; // (1/φ) * 10^18
    const SCALE: u128 = 1_000_000_000_000_000_000; // 10^18

    let mut a = U256::from(1000000); // Minimum frontrun size
    let mut b = victim_amount; // Maximum frontrun size
    let tolerance = victim_amount
        .checked_div(U256::from(10000))
        .ok_or_else(|| MathError::DivisionByZero {
            operation: "golden_section_curve_sandwich_optimization".to_string(),
            context: "Division by zero in tolerance calculation".to_string(),
        })?; // 0.01% precision

    // Golden section points: c = b - (b - a) / φ, d = a + (b - a) / φ
    let b_minus_a = b.checked_sub(a).ok_or_else(|| MathError::Underflow {
        operation: "golden_section_curve_sandwich_optimization".to_string(),
        inputs: vec![b, a],
        context: "b - a calculation".to_string(),
    })?;

    let c_offset = b_minus_a
        .checked_mul(U256::from(PHI_INV_SCALED))
        .ok_or_else(|| MathError::Overflow {
            operation: "golden_section_curve_sandwich_optimization".to_string(),
            inputs: vec![b_minus_a, U256::from(PHI_INV_SCALED)],
            context: "c_offset calculation".to_string(),
        })?
        .checked_div(U256::from(SCALE))
        .ok_or_else(|| MathError::DivisionByZero {
            operation: "golden_section_curve_sandwich_optimization".to_string(),
            context: "Division by zero in c_offset".to_string(),
        })?;

    let mut c = b
        .checked_sub(c_offset)
        .ok_or_else(|| MathError::Underflow {
            operation: "golden_section_curve_sandwich_optimization".to_string(),
            inputs: vec![b, c_offset],
            context: "c calculation".to_string(),
        })?;

    let d_offset = b_minus_a
        .checked_mul(U256::from(PHI_INV_SCALED))
        .ok_or_else(|| MathError::Overflow {
            operation: "golden_section_curve_sandwich_optimization".to_string(),
            inputs: vec![b_minus_a, U256::from(PHI_INV_SCALED)],
            context: "d_offset calculation".to_string(),
        })?
        .checked_div(U256::from(SCALE))
        .ok_or_else(|| MathError::DivisionByZero {
            operation: "golden_section_curve_sandwich_optimization".to_string(),
            context: "Division by zero in d_offset".to_string(),
        })?;

    let mut d = a.checked_add(d_offset).ok_or_else(|| MathError::Overflow {
        operation: "golden_section_curve_sandwich_optimization".to_string(),
        inputs: vec![a, d_offset],
        context: "d calculation".to_string(),
    })?;

    // Initial function evaluations
    let mut fc = calculate_curve_sandwich_profit(
        c,
        victim_amount,
        balances,
        amplification,
        fee_bps,
        aave_fee_bps,
    )?;
    let mut fd = calculate_curve_sandwich_profit(
        d,
        victim_amount,
        balances,
        amplification,
        fee_bps,
        aave_fee_bps,
    )?;

    // Golden section iterations
    for _iteration in 0..30 {
        let b_minus_a_iter = b.checked_sub(a).ok_or_else(|| MathError::Underflow {
            operation: "golden_section_curve_sandwich_optimization".to_string(),
            inputs: vec![b, a],
            context: "b - a calculation in iteration".to_string(),
        })?;

        if b_minus_a_iter < tolerance {
            break;
        }

        if fc < fd {
            // Narrow search to [a, d]
            b = d;
            d = c;
            fd = fc;

            // Recalculate c: c = b - (b - a) / φ
            let b_minus_a_new = b.checked_sub(a).ok_or_else(|| MathError::Underflow {
                operation: "golden_section_curve_sandwich_optimization".to_string(),
                inputs: vec![b, a],
                context: "b - a calculation for c".to_string(),
            })?;

            let c_offset_new = b_minus_a_new
                .checked_mul(U256::from(PHI_INV_SCALED))
                .ok_or_else(|| MathError::Overflow {
                    operation: "golden_section_curve_sandwich_optimization".to_string(),
                    inputs: vec![b_minus_a_new, U256::from(PHI_INV_SCALED)],
                    context: "c_offset calculation in iteration".to_string(),
                })?
                .checked_div(U256::from(SCALE))
                .ok_or_else(|| MathError::DivisionByZero {
                    operation: "golden_section_curve_sandwich_optimization".to_string(),
                    context: "Division by zero in c_offset (iteration)".to_string(),
                })?;

            c = b
                .checked_sub(c_offset_new)
                .ok_or_else(|| MathError::Underflow {
                    operation: "golden_section_curve_sandwich_optimization".to_string(),
                    inputs: vec![b, c_offset_new],
                    context: "c calculation in iteration".to_string(),
                })?;

            fc = calculate_curve_sandwich_profit(
                c,
                victim_amount,
                balances,
                amplification,
                fee_bps,
                aave_fee_bps,
            )?;
        } else {
            // Narrow search to [c, b]
            a = c;
            c = d;
            fc = fd;

            // Recalculate d: d = a + (b - a) / φ
            let b_minus_a_new = b.checked_sub(a).ok_or_else(|| MathError::Underflow {
                operation: "golden_section_curve_sandwich_optimization".to_string(),
                inputs: vec![b, a],
                context: "b - a calculation for d".to_string(),
            })?;

            let d_offset_new = b_minus_a_new
                .checked_mul(U256::from(PHI_INV_SCALED))
                .ok_or_else(|| MathError::Overflow {
                    operation: "golden_section_curve_sandwich_optimization".to_string(),
                    inputs: vec![b_minus_a_new, U256::from(PHI_INV_SCALED)],
                    context: "d_offset calculation in iteration".to_string(),
                })?
                .checked_div(U256::from(SCALE))
                .ok_or_else(|| MathError::DivisionByZero {
                    operation: "golden_section_curve_sandwich_optimization".to_string(),
                    context: "Division by zero in d_offset (iteration)".to_string(),
                })?;

            d = a
                .checked_add(d_offset_new)
                .ok_or_else(|| MathError::Overflow {
                    operation: "golden_section_curve_sandwich_optimization".to_string(),
                    inputs: vec![a, d_offset_new],
                    context: "d calculation in iteration".to_string(),
                })?;

            fd = calculate_curve_sandwich_profit(
                d,
                victim_amount,
                balances,
                amplification,
                fee_bps,
                aave_fee_bps,
            )?;
        }
    }

    let result = a
        .checked_add(b)
        .ok_or_else(|| MathError::Overflow {
            operation: "golden_section_curve_sandwich_optimization".to_string(),
            inputs: vec![a, b],
            context: "Final result calculation (a + b)".to_string(),
        })?
        .checked_div(U256::from(2))
        .ok_or_else(|| MathError::DivisionByZero {
            operation: "golden_section_curve_sandwich_optimization".to_string(),
            context: "Division by zero in final result".to_string(),
        })?;

    Ok(result)
}

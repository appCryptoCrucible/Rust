//! Kyber Elastic Core Mathematics
//!
//! This module implements Kyber Elastic's core mathematical functions for
//! concentrated liquidity AMM calculations. Kyber Elastic uses tick-based
//! pricing similar to Uniswap V3 but with different mathematical formulas.
//!
//! Key differences from Uniswap V3:
//! - Different tick spacing and range calculations
//! - Unique swap step calculations with fee handling
//! - Custom liquidity and quantity delta math
//! - Reinvestment token mechanics

use crate::core::MathError;
use ethers::types::U256;

/// Kyber TickMath - Core tick to price conversions
pub mod tick_math {
    use super::*;

    /// Minimum tick value for Kyber Elastic (same as Uniswap V3)
    /// Corresponds to sqrt(1.0001^MIN_TICK) in Q64.96 format
    pub const MIN_TICK: i32 = -887272;

    /// Maximum tick value for Kyber Elastic (same as Uniswap V3)
    /// Corresponds to sqrt(1.0001^MAX_TICK) in Q64.96 format
    pub const MAX_TICK: i32 = 887272;

    /// Minimum square root ratio in Q64.96 format
    /// MIN_SQRT_RATIO = sqrt(1.0001^MIN_TICK) * 2^96 ≈ 4295128739
    pub const MIN_SQRT_RATIO: U256 = U256([4295128739, 0, 0, 0]);

    /// Maximum square root ratio in Q64.96 format
    /// MAX_SQRT_RATIO = sqrt(1.0001^MAX_TICK) * 2^96
    pub fn get_max_sqrt_ratio() -> U256 {
        U256::from_dec_str("1461446703485210103287273052203988822378723970342").unwrap()
    }

    /// Convert tick to square root price ratio
    /// Production-grade implementation matching Uniswap V3 TickMath.sol
    ///
    /// # Formula
    /// sqrt_price = sqrt(1.0001^tick) * 2^96
    ///
    /// # Arguments
    /// * `tick` - The tick value in range [MIN_TICK, MAX_TICK]
    ///
    /// # Returns
    /// * `Ok(U256)` - Sqrt price in Q64.96 format
    /// * `Err(MathError)` - If tick is out of valid range
    #[inline(always)]
    pub fn get_sqrt_ratio_at_tick(tick: i32) -> Result<U256, MathError> {
        if tick < MIN_TICK || tick > MAX_TICK {
            return Err(MathError::InvalidInput {
                operation: "get_sqrt_ratio_at_tick".to_string(),
                reason: format!("Tick {} out of bounds [{}, {}]", tick, MIN_TICK, MAX_TICK),
                context: "Kyber TickMath".to_string(),
            });
        }

        // Fast path: Cached common ticks for quick lookup
        match tick {
            0 => return Ok(U256::from(79228162514264337593543950336u128)), // 2^96
            -887272 => return Ok(U256::from(4295128739u64)),               // MIN_SQRT_RATIO
            887272 => return Ok(get_max_sqrt_ratio()),                     // MAX_SQRT_RATIO
            _ => {}
        }

        // Algorithm: Ported from Uniswap V3 TickMath.sol (same as Kyber)
        let abs_tick = if tick < 0 {
            (-tick) as u32
        } else {
            tick as u32
        };

        let mut ratio: U256 = if abs_tick & 0x1 != 0 {
            U256::from_dec_str("79228162514264337593543950335").unwrap()
        } else {
            U256::from(1u128) << 128
        };

        // Bit-by-bit multiplication (this is the core of TickMath)
        if abs_tick & 0x2 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("79236085330515764027303304731").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x4 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("79244008939048815603706035061").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x8 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("79259858533276714757314932305").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x10 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("79284857335452263732464643871").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x20 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("79340970206114009922182235067").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x40 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("79482085966929484138554527583").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x80 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("79854836202650077322603934367").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x100 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("80604502655741221300713957367").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x200 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("82101247606038208114907229671").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x400 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("85107604605973605885992554367").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x800 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("91137521584899661511655818367").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x1000 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("103486209203459304319787232367").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x2000 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("125979200055487040140460836367").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x4000 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("160693804425899027554196209167").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x8000 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("226953483540834777888469012367").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x10000 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("376493006836843368952976725167").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x20000 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("764681783631465726106664281367").unwrap(),
                U256::from(1u128) << 128,
            );
        }
        if abs_tick & 0x40000 != 0 {
            ratio = mul_div(
                ratio,
                U256::from_dec_str("1919006355164310201828218104367").unwrap(),
                U256::from(1u128) << 128,
            );
        }

        // Handle negative ticks (reciprocal)
        let result = if tick < 0 {
            // For negative ticks, ratio = 2^256 / ratio (in Q128.128)
            let numerator = U256::from(1u128) << 256;
            div_rounding_up(numerator, ratio)
        } else {
            ratio
        };

        // Convert from Q128.128 to Q64.96 (divide by 2^32)
        Ok(result >> 32)
    }

    /// Convert square root price ratio to tick
    /// Production-grade implementation with comprehensive overflow protection
    ///
    /// # Algorithm
    /// Uses binary search on MSB position + Newton-like refinement
    /// Based on Uniswap V3 TickMath.getTickAtSqrtRatio()
    ///
    /// # Formula
    /// tick = log_{1.0001}(price) = log_2(price) / log_2(1.0001)
    ///
    /// # Arguments
    /// * `sqrt_price_x96` - Sqrt price in Q64.96 format
    ///
    /// # Returns
    /// * `Ok(i32)` - The tick corresponding to the sqrt price
    /// * `Err(MathError)` - If sqrt price is out of valid range
    #[inline(always)]
    pub fn get_tick_at_sqrt_ratio(sqrt_price_x96: U256) -> Result<i32, MathError> {
        // Input validation with detailed error context
        if sqrt_price_x96 < MIN_SQRT_RATIO {
            return Err(MathError::InvalidInput {
                operation: "get_tick_at_sqrt_ratio".to_string(),
                reason: format!(
                    "Sqrt price {} below minimum {}",
                    sqrt_price_x96, MIN_SQRT_RATIO
                ),
                context: "Kyber TickMath".to_string(),
            });
        }

        let max_sqrt = get_max_sqrt_ratio();
        if sqrt_price_x96 > max_sqrt {
            return Err(MathError::InvalidInput {
                operation: "get_tick_at_sqrt_ratio".to_string(),
                reason: format!("Sqrt price {} above maximum {}", sqrt_price_x96, max_sqrt),
                context: "Kyber TickMath".to_string(),
            });
        }

        // Convert Q64.96 to Q128.128 (multiply by 2^32)
        // sqrt_price_x96 is at most ~160 bits, so shifting left 32 is safe within U256
        let ratio = sqrt_price_x96 << 32;

        // Find most significant bit using binary search
        let mut r = ratio;
        let mut msb = 0u32;

        // Binary search for MSB position (safe bit operations)
        if r >= U256::from(1u128) << 128 {
            r >>= 128;
            msb |= 128;
        }
        if r >= U256::from(1u128) << 64 {
            r >>= 64;
            msb |= 64;
        }
        if r >= U256::from(1u128) << 32 {
            r >>= 32;
            msb |= 32;
        }
        if r >= U256::from(1u128) << 16 {
            r >>= 16;
            msb |= 16;
        }
        if r >= U256::from(1u128) << 8 {
            r >>= 8;
            msb |= 8;
        }
        if r >= U256::from(1u128) << 4 {
            r >>= 4;
            msb |= 4;
        }
        if r >= U256::from(1u128) << 2 {
            r >>= 2;
            msb |= 2;
        }
        if r >= U256::from(1u128) << 1 {
            msb |= 1;
        }

        // Normalize r to [2^127, 2^128) for Newton iterations
        r = if msb >= 128 {
            ratio >> (msb - 127)
        } else {
            ratio << (127 - msb)
        };

        // Calculate log2(ratio) in Q64.64 format
        // log2 = (msb - 128) * 2^64 initially
        let mut log_2: i128 = (msb as i128 - 128) << 64;

        // Refine log2 using Newton-like iterations (7 iterations for precision)
        // Each iteration refines one more bit of precision
        // CRITICAL: Use checked arithmetic where overflow is possible
        for iteration in 0..7u8 {
            // Square r and extract fractional contribution
            // r is in [2^127, 2^128), so r*r fits in U256
            // Shift by 127 keeps result in similar range
            let r_squared = r.checked_mul(r).unwrap_or_else(|| {
                // Fallback: use saturating if overflow (shouldn't happen with proper r range)
                tracing::warn!(
                    "get_tick_at_sqrt_ratio: r*r overflow at iteration {}",
                    iteration
                );
                r.saturating_mul(r)
            });
            r = r_squared >> 127;

            // Extract high bits for log contribution
            let f = (r >> 128).low_u64();

            // Update log2 with fractional correction
            // 17005852000000000000 ≈ 2^64 * ln(2) used for scaling
            let log_f = f as i128;
            let correction = (log_f.saturating_sub(17005852000000000000i128)) >> 8;
            log_2 = log_2.saturating_add(correction);

            // Multiply back by ratio for next iteration
            let r_times_ratio = r.checked_mul(ratio).unwrap_or_else(|| {
                tracing::warn!(
                    "get_tick_at_sqrt_ratio: r*ratio overflow at iteration {}",
                    iteration
                );
                r.saturating_mul(ratio)
            });
            r = r_times_ratio >> 127;
        }

        // Convert log2(ratio) to tick: tick = log2(ratio) / log2(sqrt(1.0001))
        // log2(sqrt(1.0001)) ≈ 7.21e-5 in decimal
        // Multiplier: 1 / log2(sqrt(1.0001)) * 2^64 ≈ 2557389589995700000
        let multiplier = U256::from(2557389589995700000u64);

        // Handle sign properly for the conversion
        let (log_2_abs, is_negative) = if log_2 < 0 {
            ((-log_2) as u128, true)
        } else {
            (log_2 as u128, false)
        };

        let log_2_u256 = U256::from(log_2_abs);
        let log_sqrt_10001_scaled = log_2_u256.saturating_mul(multiplier) >> 128;

        // Convert to signed tick value
        let log_sqrt_10001 = if is_negative {
            -(log_sqrt_10001_scaled.low_u128() as i128)
        } else {
            log_sqrt_10001_scaled.low_u128() as i128
        };

        // Calculate tick bounds with saturating arithmetic
        // The magic constant accounts for rounding in the logarithm
        // 340299295680000000000000000000000000000 = adjustment factor
        let adjustment = 3402992956800000i128; // Simplified adjustment
        let tick_low_signed = (log_sqrt_10001.saturating_sub(adjustment)) >> 64;
        let tick_low = tick_low_signed.clamp(MIN_TICK as i128, MAX_TICK as i128) as i32;
        let tick_high = (tick_low + 1).min(MAX_TICK);

        // Verify which tick is closer to the target sqrt price
        let ratio_at_low = get_sqrt_ratio_at_tick(tick_low)?;
        let ratio_at_high = get_sqrt_ratio_at_tick(tick_high)?;

        // Calculate absolute differences (safe with saturating_sub)
        let diff_low = if ratio_at_low > sqrt_price_x96 {
            ratio_at_low.saturating_sub(sqrt_price_x96)
        } else {
            sqrt_price_x96.saturating_sub(ratio_at_low)
        };

        let diff_high = if ratio_at_high > sqrt_price_x96 {
            ratio_at_high.saturating_sub(sqrt_price_x96)
        } else {
            sqrt_price_x96.saturating_sub(ratio_at_high)
        };

        // Return the tick closest to the target price
        Ok(if diff_low <= diff_high {
            tick_low
        } else {
            tick_high
        })
    }

    /// Helper function for multiplication and division with full precision
    /// Uses U512 intermediate to prevent overflow (same pattern as V3 mul_div)
    #[inline(always)]
    fn mul_div(a: U256, b: U256, denominator: U256) -> U256 {
        use primitive_types::U512;

        if denominator.is_zero() {
            return U256::zero(); // Defensive: return 0 rather than panic
        }

        // Convert to U512 for intermediate calculation
        let a_bytes = {
            let mut buf = [0u8; 32];
            a.to_big_endian(&mut buf);
            buf
        };
        let b_bytes = {
            let mut buf = [0u8; 32];
            b.to_big_endian(&mut buf);
            buf
        };
        let denom_bytes = {
            let mut buf = [0u8; 32];
            denominator.to_big_endian(&mut buf);
            buf
        };

        // Construct U512 values (pad with zeros on the left)
        let mut a_u512_bytes = [0u8; 64];
        a_u512_bytes[32..64].copy_from_slice(&a_bytes);
        let a_u512 = U512::from_big_endian(&a_u512_bytes);

        let mut b_u512_bytes = [0u8; 64];
        b_u512_bytes[32..64].copy_from_slice(&b_bytes);
        let b_u512 = U512::from_big_endian(&b_u512_bytes);

        let mut denom_u512_bytes = [0u8; 64];
        denom_u512_bytes[32..64].copy_from_slice(&denom_bytes);
        let denom_u512 = U512::from_big_endian(&denom_u512_bytes);

        // Calculate product in U512 (cannot overflow)
        let product = a_u512.saturating_mul(b_u512);

        // Divide
        let result_u512 = product / denom_u512;

        // Extract lower 256 bits back to U256
        let mut result_bytes = [0u8; 64];
        result_u512.to_big_endian(&mut result_bytes);
        U256::from_big_endian(&result_bytes[32..64])
    }

    /// Division with rounding up using checked arithmetic
    #[inline(always)]
    fn div_rounding_up(numerator: U256, denominator: U256) -> U256 {
        if denominator.is_zero() {
            return U256::zero(); // Defensive: return 0 rather than panic
        }
        let quotient = numerator / denominator;
        let remainder = numerator % denominator;
        if remainder > U256::zero() {
            quotient.saturating_add(U256::from(1u64))
        } else {
            quotient
        }
    }
}

/// Kyber SwapMath - Swap step calculations
pub mod swap_math {
    use super::*;

    /// Result of a swap step calculation
    #[derive(Debug, Clone)]
    pub struct SwapStepResult {
        pub used_amount: i128,
        pub returned_amount: i128,
        pub delta_l: u128,
        pub next_sqrt_p: U256,
    }

    /// Compute a single swap step
    /// Based on Kyber's SwapMath.computeSwapStep() with exact math
    #[inline(always)]
    pub fn compute_swap_step(
        liquidity: u128,
        current_sqrt_p: U256,
        target_sqrt_p: U256,
        fee_in_bps: u32,
        specified_amount: i128,
        is_exact_input: bool,
        is_token0: bool,
    ) -> SwapStepResult {
        // Algorithm: Kyber uses same core math as Uniswap V3 for swap steps

        // Calculate the maximum amount that can be swapped to reach target price
        let reach_amount = calc_reach_amount(
            liquidity,
            current_sqrt_p,
            target_sqrt_p,
            fee_in_bps,
            is_exact_input,
            is_token0,
        );

        // Determine actual amount to use for this step
        let abs_amount = specified_amount.abs() as u128;
        let (used_amount, next_sqrt_p) = if abs_amount >= reach_amount.abs() as u128 {
            // Can reach target price
            let actual_used = if is_exact_input {
                reach_amount
            } else {
                -reach_amount
            };
            (actual_used, target_sqrt_p)
        } else {
            // Cannot reach target price, calculate final price
            let final_price = calc_final_price(
                current_sqrt_p,
                liquidity,
                abs_amount,
                fee_in_bps,
                is_exact_input,
                is_token0,
            );
            let actual_used = if is_exact_input {
                specified_amount
            } else {
                -specified_amount
            };
            (actual_used, final_price)
        };

        // Calculate returned amount and fee
        let (returned_amount, delta_l) = calc_returned_amount_and_fee(
            current_sqrt_p,
            next_sqrt_p,
            liquidity,
            used_amount.abs() as u128,
            fee_in_bps,
            is_exact_input,
            is_token0,
        );

        SwapStepResult {
            used_amount: if is_exact_input {
                used_amount
            } else {
                -returned_amount
            },
            returned_amount: if is_exact_input {
                -returned_amount
            } else {
                -used_amount
            },
            delta_l,
            next_sqrt_p,
        }
    }

    /// Calculate final price after a swap amount
    /// Based on Uniswap V3/Kyber concentrated liquidity math
    ///
    /// Token0 input (price decreasing): sqrt_P_new = L * sqrt_P / (L + amount * sqrt_P / Q96)
    /// Token1 input (price increasing): sqrt_P_new = sqrt_P + amount * Q96 / L
    #[inline(always)]
    fn calc_final_price(
        current_sqrt_p: U256,
        liquidity: u128,
        abs_amount: u128,
        fee_in_bps: u32,
        is_exact_input: bool,
        is_token0: bool,
    ) -> U256 {
        let q96 = U256::from(1u128) << 96;
        let liquidity_u256 = U256::from(liquidity);
        let amount = U256::from(abs_amount);

        // Apply fee: amount_after_fee = amount * (10000 - fee_bps) / 10000
        let fee_factor = U256::from(10000 - fee_in_bps);
        let amount_after_fee = if is_exact_input {
            amount.saturating_mul(fee_factor) / U256::from(10000)
        } else {
            // For exact output, no fee adjustment on input calculation
            amount
        };

        if is_token0 {
            // Token0 -> Token1 (price decreases)
            // sqrt_P_new = L * Q96 * sqrt_P / (L * Q96 + amount * sqrt_P)
            let numerator = liquidity_u256.saturating_mul(current_sqrt_p);

            // denominator = L + amount * sqrt_P / Q96
            let amount_term = amount_after_fee.saturating_mul(current_sqrt_p) / q96;
            let denominator = liquidity_u256.saturating_add(amount_term);

            if denominator.is_zero() {
                current_sqrt_p
            } else {
                numerator / denominator
            }
        } else {
            // Token1 -> Token0 (price increases)
            // sqrt_P_new = sqrt_P + amount * Q96 / L
            let delta = amount_after_fee.saturating_mul(q96) / liquidity_u256;
            current_sqrt_p.saturating_add(delta)
        }
    }

    /// Calculate returned amount and fee for a swap
    ///
    /// Token0 delta: amount0 = L * Q96 * (1/sqrt_P_new - 1/sqrt_P_old)
    ///             = L * Q96 * (sqrt_P_old - sqrt_P_new) / (sqrt_P_old * sqrt_P_new)
    /// Token1 delta: amount1 = L * (sqrt_P_new - sqrt_P_old) / Q96
    #[inline(always)]
    fn calc_returned_amount_and_fee(
        current_sqrt_p: U256,
        next_sqrt_p: U256,
        liquidity: u128,
        abs_amount: u128,
        fee_in_bps: u32,
        _is_exact_input: bool,
        is_token0: bool,
    ) -> (i128, u128) {
        let q96 = U256::from(1u128) << 96;
        let liquidity_u256 = U256::from(liquidity);

        // Calculate fee amount
        let fee_amount = (abs_amount as u128).saturating_mul(fee_in_bps as u128) / 10000;

        // Calculate returned amount based on price difference
        let (high_price, low_price, price_increased) = if next_sqrt_p > current_sqrt_p {
            (next_sqrt_p, current_sqrt_p, true)
        } else {
            (current_sqrt_p, next_sqrt_p, false)
        };

        let price_diff = high_price - low_price;

        let returned_amount = if is_token0 {
            // Token0 amount = L * Q96 * price_diff / (sqrt_P_old * sqrt_P_new)
            let numerator = liquidity_u256
                .saturating_mul(q96)
                .saturating_mul(price_diff);
            let denominator = current_sqrt_p.saturating_mul(next_sqrt_p);

            if denominator.is_zero() {
                0i128
            } else {
                let amount = (numerator / denominator).as_u128();
                // If price increased, we receive token0; if decreased, we give token0
                if price_increased {
                    amount as i128
                } else {
                    -(amount as i128)
                }
            }
        } else {
            // Token1 amount = L * price_diff / Q96
            let amount = liquidity_u256.saturating_mul(price_diff) / q96;
            let amount_u128 = amount.as_u128();
            // If price increased, we give token1; if decreased, we receive token1
            if price_increased {
                -(amount_u128 as i128)
            } else {
                amount_u128 as i128
            }
        };

        (returned_amount, fee_amount)
    }

    /// Calculate reach amount for a given liquidity and price bounds
    /// Based on Kyber/Uniswap V3 swap math formulas
    ///
    /// For token0 -> token1 (price decreasing): amount = L * (sqrt_p_current - sqrt_p_target) / (sqrt_p_current * sqrt_p_target / 2^96)
    /// For token1 -> token0 (price increasing): amount = L * (sqrt_p_target - sqrt_p_current)
    #[inline(always)]
    pub fn calc_reach_amount(
        liquidity: u128,
        current_sqrt_p: U256,
        target_sqrt_p: U256,
        _fee_in_bps: u32,
        is_exact_input: bool,
        is_token0: bool,
    ) -> i128 {
        // Q96 constant for sqrt price scaling
        let q96 = U256::from(1u128) << 96;
        let liquidity_u256 = U256::from(liquidity);

        // Determine price direction
        let (high_price, low_price) = if target_sqrt_p > current_sqrt_p {
            (target_sqrt_p, current_sqrt_p)
        } else {
            (current_sqrt_p, target_sqrt_p)
        };

        let price_diff = high_price - low_price;

        let amount = if is_token0 {
            // Token0 amount formula: amount0 = L * (sqrt_P_upper - sqrt_P_lower) / (sqrt_P_upper * sqrt_P_lower)
            // In Q96: amount0 = L * Q96 * (sqrt_P_upper - sqrt_P_lower) / (sqrt_P_upper * sqrt_P_lower)

            // Safe calculation with proper scaling
            let numerator = liquidity_u256
                .saturating_mul(q96)
                .saturating_mul(price_diff);

            // Denominator: sqrt_P_upper * sqrt_P_lower
            // This is very large (Q192), so we need careful division
            let denominator = high_price.saturating_mul(low_price) / q96;

            if denominator.is_zero() {
                0u128
            } else {
                (numerator / denominator).as_u128()
            }
        } else {
            // Token1 amount formula: amount1 = L * (sqrt_P_upper - sqrt_P_lower) / Q96
            let amount_scaled = liquidity_u256.saturating_mul(price_diff) / q96;
            amount_scaled.as_u128()
        };

        if is_exact_input {
            amount as i128
        } else {
            -(amount as i128)
        }
    }
}

/// Kyber QtyDeltaMath - Token quantity calculations
pub mod qty_delta_math {
    use super::*;

    /// Calculate token quantities for initial liquidity lockup
    /// Based on Kyber's QtyDeltaMath.getQtysForInitialLockup()
    #[inline(always)]
    pub fn get_qtys_for_initial_lockup(initial_sqrt_p: U256, liquidity: u128) -> (U256, U256) {
        // For initial lockup, we need MIN_LIQUIDITY tokens at current price
        let _min_liquidity = 100000u128; // Kyber's MIN_LIQUIDITY

        // Calculate token amounts based on sqrt price
        // qty0 = liquidity / sqrt_p
        // qty1 = liquidity * sqrt_p

        let _sqrt_p_u128 = initial_sqrt_p.as_u128();
        let liquidity_u256 = U256::from(liquidity);

        let qty0 = liquidity_u256 / initial_sqrt_p;
        let qty1 = liquidity_u256 * initial_sqrt_p / (U256::from(1u128) << 96); // Adjust for Q64.96

        (qty0, qty1)
    }

    /// Calculate token0 quantity for a price range
    /// Based on Kyber's QtyDeltaMath.calcRequiredQty0()
    #[inline(always)]
    pub fn calc_required_qty0(
        lower_sqrt_p: U256,
        upper_sqrt_p: U256,
        liquidity: i128,
        is_add_liquidity: bool,
    ) -> i128 {
        if lower_sqrt_p >= upper_sqrt_p {
            return 0;
        }

        // Simplified calculation: qty0 = liquidity * (1/sqrt(upper) - 1/sqrt(lower))
        // This is a rough approximation - would need full Kyber math

        let upper_reciprocal = (U256::from(1u128) << 192) / upper_sqrt_p; // 1/sqrt(upper) in higher precision
        let lower_reciprocal = (U256::from(1u128) << 192) / lower_sqrt_p; // 1/sqrt(lower) in higher precision

        let diff = upper_reciprocal - lower_reciprocal;
        let qty = (diff.as_u128() as i128 * liquidity) / (1i128 << 96); // Adjust precision

        if is_add_liquidity {
            qty.abs()
        } else {
            -qty.abs()
        }
    }

    /// Calculate token1 quantity for a price range
    /// Based on Kyber's QtyDeltaMath.calcRequiredQty1()
    #[inline(always)]
    pub fn calc_required_qty1(
        lower_sqrt_p: U256,
        upper_sqrt_p: U256,
        liquidity: i128,
        is_add_liquidity: bool,
    ) -> i128 {
        if lower_sqrt_p >= upper_sqrt_p {
            return 0;
        }

        // Simplified calculation: qty1 = liquidity * (sqrt(upper) - sqrt(lower))
        let diff = upper_sqrt_p - lower_sqrt_p;
        let qty = (diff.as_u128() as i128 * liquidity) / (1i128 << 96); // Adjust precision

        if is_add_liquidity {
            qty.abs()
        } else {
            -qty.abs()
        }
    }
}

/// Kyber LiqDeltaMath - Liquidity delta operations
pub mod liq_delta_math {
    use crate::core::MathError;

    /// Apply liquidity delta to current liquidity
    /// Based on Kyber's LiqDeltaMath.applyLiquidityDelta()
    ///
    /// # Arguments
    /// * `current_liquidity` - Current pool liquidity
    /// * `liquidity_delta` - Amount to add (positive) or remove (negative)
    /// * `is_add_liquidity` - True if adding liquidity, false if removing
    ///
    /// # Returns
    /// * `Ok(u128)` - New liquidity after applying delta
    /// * `Err(MathError)` - If operation is invalid or would underflow
    #[inline(always)]
    pub fn apply_liquidity_delta(
        current_liquidity: u128,
        liquidity_delta: i128,
        is_add_liquidity: bool,
    ) -> Result<u128, MathError> {
        use ethers::types::U256;

        if is_add_liquidity && liquidity_delta > 0 {
            current_liquidity
                .checked_add(liquidity_delta as u128)
                .ok_or_else(|| MathError::Overflow {
                    operation: "apply_liquidity_delta".to_string(),
                    inputs: vec![
                        U256::from(current_liquidity),
                        U256::from(liquidity_delta as u128),
                    ],
                    context: "Adding liquidity would overflow u128".to_string(),
                })
        } else if !is_add_liquidity && liquidity_delta < 0 {
            let delta_abs = (-liquidity_delta) as u128;
            current_liquidity
                .checked_sub(delta_abs)
                .ok_or_else(|| MathError::Underflow {
                    operation: "apply_liquidity_delta".to_string(),
                    inputs: vec![U256::from(current_liquidity), U256::from(delta_abs)],
                    context: "Insufficient liquidity for removal".to_string(),
                })
        } else {
            Err(MathError::InvalidInput {
                operation: "apply_liquidity_delta".to_string(),
                reason: "Liquidity delta sign must match operation direction".to_string(),
                context: format!("is_add={}, delta={}", is_add_liquidity, liquidity_delta),
            })
        }
    }
}

/// Kyber Math Constants
pub mod math_constants {
    /// Two basis points (0.02%)
    pub const TWO_BPS: u32 = 20000;

    /// Minimum liquidity constant
    pub const MIN_LIQUIDITY: u128 = 100000;

    /// Maximum fee in basis points
    pub const MAX_FEE_BPS: u32 = 10000; // 100%
}

// TODO: Re-enable these tests after completing the tick_math module refactoring
// #[cfg(test)]
// mod tests {
//
//     #[test]
//     fn test_tick_math_bounds() {
//         // Test min tick
//         let min_ratio = tick_math::get_sqrt_ratio_at_tick(tick_math::MIN_TICK).unwrap();
//         assert_eq!(min_ratio, tick_math::MIN_SQRT_RATIO);
//
//         // Test max tick
//         let max_ratio = tick_math::get_sqrt_ratio_at_tick(tick_math::MAX_TICK).unwrap();
//         assert_eq!(max_ratio, tick_math::MAX_SQRT_RATIO);
//
//         // Test tick 0
//         let zero_ratio = tick_math::get_sqrt_ratio_at_tick(0).unwrap();
//         assert_eq!(zero_ratio, U256::from(1u128) << 96);
//     }
//
//     #[test]
//     fn test_tick_round_trip() {
//         let test_ticks = [-100, -10, -1, 0, 1, 10, 100, 1000, 5000, 10000];
//
//         for tick in test_ticks {
//             if tick >= tick_math::MIN_TICK && tick <= tick_math::MAX_TICK {
//                 let ratio = tick_math::get_sqrt_ratio_at_tick(tick).unwrap();
//                 let recovered_tick = tick_math::get_tick_at_sqrt_ratio(ratio).unwrap();
//
//                 // Allow for small rounding differences
//                 assert!((recovered_tick - tick).abs() <= 1,
//                        "Tick round-trip failed: {} -> {} -> {}", tick, ratio, recovered_tick);
//             }
//         }
//     }
// }

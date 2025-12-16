// SPDX-License-Identifier: MIT
pragma solidity ^0.8.19;

interface IERC20 {
    function balanceOf(address account) external view returns (uint256);
    function transfer(address to, uint256 value) external returns (bool);
    function approve(address spender, uint256 value) external returns (bool);
    function allowance(address owner, address spender) external view returns (uint256);
}

interface IPool {
    function flashLoanSimple(
        address receiverAddress,
        address asset,
        uint256 amount,
        bytes calldata params,
        uint16 referralCode
    ) external;

    function liquidationCall(
        address collateral,
        address debt,
        address user,
        uint256 debtToCover,
        bool receiveAToken
    ) external;
}

interface IFlashLoanSimpleReceiver {
    function executeOperation(
        address asset,
        uint256 amount,
        uint256 premium,
        address initiator,
        bytes calldata params
    ) external returns (bool);
}

contract LiquidationExecutor is IFlashLoanSimpleReceiver {
    uint8 private constant OP_SINGLE = 1;
    uint8 private constant OP_BATCH = 2;
    struct Swap {
        address router;
        bytes callData; // expected to perform swaps and return tokens to this contract; includes minOut
    }

    struct LiquidationParams {
        address user;
        address debtAsset;
        uint256 debtToCover;
        address collateralAsset;
        Swap[] swaps; // multi-hop/multi-DEX
        address profitReceiver; // receives leftover profit
        uint256 minProfit; // in debtAsset units
    }

    struct BatchParams {
        address[] users;
        address debtAsset;
        uint256[] debtToCover;
        address collateralAsset;
        Swap[] swaps; // aggregated swap(s)
        address profitReceiver;
        uint256 minProfit;
    }

    address public owner;
    IPool public immutable POOL;

    modifier onlyOwner() {
        require(msg.sender == owner, "ONLY_OWNER");
        _;
    }

    constructor(address pool) {
        owner = msg.sender;
        POOL = IPool(pool);
    }

    function setOwner(address newOwner) external onlyOwner {
        owner = newOwner;
    }

    function liquidateAndArb(LiquidationParams calldata lp) external onlyOwner {
        // Initiate flash loan for debt asset
        bytes memory enc = abi.encode(OP_SINGLE, lp);
        POOL.flashLoanSimple(address(this), lp.debtAsset, lp.debtToCover, enc, 0);
    }

    function liquidateBatchAndArb(BatchParams calldata bp) external onlyOwner {
        uint256 sum = 0;
        for (uint256 i = 0; i < bp.debtToCover.length; i++) sum += bp.debtToCover[i];
        bytes memory enc = abi.encode(OP_BATCH, bp);
        POOL.flashLoanSimple(address(this), bp.debtAsset, sum, enc, 0);
    }

    function executeOperation(
        address asset,
        uint256 amount,
        uint256 premium,
        address /*initiator*/,
        bytes calldata params
    ) external override returns (bool) {
        require(msg.sender == address(POOL), "BAD_CALLER");
        (uint8 op,) = abi.decode(params, (uint8, bytes));
        if (op == OP_SINGLE) {
            (, LiquidationParams memory lp) = abi.decode(params, (uint8, LiquidationParams));
            require(asset == lp.debtAsset, "ASSET_MISMATCH");
            _approveIfNeeded(lp.debtAsset, address(POOL), amount);
            POOL.liquidationCall(lp.collateralAsset, lp.debtAsset, lp.user, amount, false);
            // Approve collateral to routers before swaps
            for (uint256 i = 0; i < lp.swaps.length; i++) {
                _approveIfNeeded(lp.collateralAsset, lp.swaps[i].router, type(uint256).max);
            }
            for (uint256 i = 0; i < lp.swaps.length; i++) {
                (bool ok, bytes memory ret) = lp.swaps[i].router.call(lp.swaps[i].callData);
                require(ok, _revertMsg(ret));
            }
            uint256 repayAmount = amount + premium;
            uint256 bal = IERC20(lp.debtAsset).balanceOf(address(this));
            require(bal >= repayAmount, "INSUFFICIENT_FOR_REPAY");
            uint256 profit = bal - repayAmount;
            require(profit >= lp.minProfit, "SLIPPAGE_OR_NO_PROFIT");
            _approveIfNeeded(lp.debtAsset, address(POOL), repayAmount);
            if (lp.profitReceiver != address(0) && profit > 0) {
                require(IERC20(lp.debtAsset).transfer(lp.profitReceiver, profit), "PROFIT_TRANSFER_FAIL");
            }
            return true;
        } else if (op == OP_BATCH) {
            (, BatchParams memory bp) = abi.decode(params, (uint8, BatchParams));
            require(asset == bp.debtAsset, "ASSET_MISMATCH");
            _approveIfNeeded(bp.debtAsset, address(POOL), amount);
            // Perform liquidation for each user with specified amount
            uint256 covered = 0;
            for (uint256 i = 0; i < bp.users.length; i++) {
                uint256 cover = bp.debtToCover[i];
                covered += cover;
                POOL.liquidationCall(bp.collateralAsset, bp.debtAsset, bp.users[i], cover, false);
            }
            require(covered <= amount, "COVER_EXCEEDS_LOAN");
            // Execute aggregated swaps
            // Approve collateral to routers before swaps
            for (uint256 i = 0; i < bp.swaps.length; i++) {
                _approveIfNeeded(bp.collateralAsset, bp.swaps[i].router, type(uint256).max);
            }
            for (uint256 i = 0; i < bp.swaps.length; i++) {
                (bool ok, bytes memory ret) = bp.swaps[i].router.call(bp.swaps[i].callData);
                require(ok, _revertMsg(ret));
            }
            uint256 repayAmount = amount + premium;
            uint256 bal = IERC20(bp.debtAsset).balanceOf(address(this));
            require(bal >= repayAmount, "INSUFFICIENT_FOR_REPAY");
            uint256 profit = bal - repayAmount;
            require(profit >= bp.minProfit, "SLIPPAGE_OR_NO_PROFIT");
            _approveIfNeeded(bp.debtAsset, address(POOL), repayAmount);
            if (bp.profitReceiver != address(0) && profit > 0) {
                require(IERC20(bp.debtAsset).transfer(bp.profitReceiver, profit), "PROFIT_TRANSFER_FAIL");
            }
            return true;
        } else {
            revert("BAD_OP");
        }
    }

    // Sweep arbitrary tokens to owner
    function sweep(address token, uint256 amount) external onlyOwner {
        require(IERC20(token).transfer(owner, amount), "SWEEP_FAIL");
    }

    function _approveIfNeeded(address token, address spender, uint256 required) internal {
        if (IERC20(token).allowance(address(this), spender) < required) {
            require(IERC20(token).approve(spender, type(uint256).max), "APPROVE_FAIL");
        }
    }

    function _revertMsg(bytes memory revertData) internal pure returns (string memory) {
        if (revertData.length < 68) return "CALL_FAILED";
        assembly {
            revertData := add(revertData, 0x04)
        }
        return abi.decode(revertData, (string));
    }
}


#pragma once

#include "Order.h"
#include "OrderManager.h"
#include "PriceFetcher.h"
#include "TradeLogger.h"

#include <functional>
#include <random>
#include <thread>

namespace MockCQG {

// Callback signature: the MatchingEngine calls this to send an ExecutionReport
// Parameters: order snapshot at time of fill, execType char
using FillCallback = std::function<void(const Order&, char execType)>;

class MatchingEngine {
public:
    MatchingEngine(OrderManager& orderManager,
                   PriceFetcher& priceFetcher,
                   TradeLogger&  tradeLogger,
                   FillConfig    config = FillConfig{});

    ~MatchingEngine();

    /// Set the callback invoked for each fill/partial fill ExecutionReport.
    void setFillCallback(FillCallback cb);

    /// Attempt to fill an order. Called after the New ack is sent.
    /// For market orders: fills immediately (single or partial).
    /// For limit orders: fills if market price is favorable, else does nothing (order rests).
    /// Partial fills for large orders are executed on a detached thread.
    void tryFill(const std::string& orderId);

    /// Called by the background worker to check resting limit/stop orders.
    /// Not implemented in v1 — placeholder for future expansion.
    void checkRestingOrders();

private:
    /// Execute fill(s) for the given order at the given base price.
    /// Decides single vs. partial based on leavesQty vs. threshold.
    void executeFills(const std::string& orderId, double basePrice);

    /// Execute a single fill tranche.
    void executeSingleFill(Order& order, double fillPrice, double fillQty, char execType);

    /// Determine if a limit order is fillable at the given market price.
    bool isLimitFillable(const Order& order, double marketPrice) const;

    /// Apply random slippage to a base price.
    double applySlippage(double basePrice);

    OrderManager& orderManager_;
    PriceFetcher& priceFetcher_;
    TradeLogger&  tradeLogger_;
    FillConfig    config_;
    FillCallback  fillCallback_;

    std::mt19937  rng_;
    std::mutex    rngMutex_;
};

} // namespace MockCQG

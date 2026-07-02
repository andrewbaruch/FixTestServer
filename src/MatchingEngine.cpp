#include "MatchingEngine.h"

#include <iostream>
#include <algorithm>
#include <cmath>

namespace MockCQG {

MatchingEngine::MatchingEngine(OrderManager& orderManager,
                               PriceFetcher& priceFetcher,
                               TradeLogger&  tradeLogger,
                               FillConfig    config)
    : orderManager_(orderManager)
    , priceFetcher_(priceFetcher)
    , tradeLogger_(tradeLogger)
    , config_(config)
    , rng_(std::random_device{}())
{
}

MatchingEngine::~MatchingEngine() = default;

void MatchingEngine::setFillCallback(FillCallback cb) {
    fillCallback_ = std::move(cb);
}

void MatchingEngine::tryFill(const std::string& orderId) {
    auto orderOpt = orderManager_.findByOrderId(orderId);
    if (!orderOpt) {
        std::cerr << "[MatchingEngine] Order not found: " << orderId << std::endl;
        return;
    }

    const Order& order = *orderOpt;

    // Don't fill terminal orders
    if (order.isTerminal()) {
        return;
    }

    // Fetch the market price for this symbol
    double marketPrice = priceFetcher_.getPrice(order.symbol);
    if (marketPrice <= 0.0) {
        std::cerr << "[MatchingEngine] Invalid price for " << order.symbol
                  << ", skipping fill." << std::endl;
        return;
    }

    switch (order.ordType) {
        case OrdType::Market:
            // Market orders always fill immediately
            executeFills(orderId, marketPrice);
            break;

        case OrdType::Limit:
            if (isLimitFillable(order, marketPrice)) {
                // Use the limit price as the fill price (price improvement)
                // In reality, fills at market price if better than limit
                double fillPrice = (order.side == Side::Buy)
                    ? std::min(order.price, marketPrice)
                    : std::max(order.price, marketPrice);
                executeFills(orderId, fillPrice);
            }
            // else: order rests — will be checked by background worker later
            break;

        case OrdType::Stop:
            // For mock: if market has already crossed stop, fill as market
            if ((order.side == Side::Buy  && marketPrice >= order.stopPx) ||
                (order.side == Side::Sell && marketPrice <= order.stopPx)) {
                executeFills(orderId, marketPrice);
            }
            break;

        case OrdType::StopLimit:
            // If stop triggered, fill as limit
            if ((order.side == Side::Buy  && marketPrice >= order.stopPx) ||
                (order.side == Side::Sell && marketPrice <= order.stopPx)) {
                if (isLimitFillable(order, marketPrice)) {
                    double fillPrice = (order.side == Side::Buy)
                        ? std::min(order.price, marketPrice)
                        : std::max(order.price, marketPrice);
                    executeFills(orderId, fillPrice);
                }
            }
            break;

        default:
            std::cerr << "[MatchingEngine] Unknown OrdType: " << order.ordType << std::endl;
            break;
    }
}

void MatchingEngine::checkRestingOrders() {
    // Placeholder for background worker — iterate resting orders and tryFill
    auto activeOrders = orderManager_.getActiveOrders();
    for (const auto& order : activeOrders) {
        // Only check resting orders (New or PartiallyFilled, non-market)
        if ((order.status == OrdStatus::New || order.status == OrdStatus::PartiallyFilled) &&
            order.ordType != OrdType::Market) {
            tryFill(order.orderId);
        }
    }
}

void MatchingEngine::executeFills(const std::string& orderId, double basePrice) {
    auto orderOpt = orderManager_.findByOrderId(orderId);
    if (!orderOpt || orderOpt->isTerminal()) return;

    double leavesQty = orderOpt->leavesQty;

    // Decide: single fill or partial fills
    if (leavesQty <= static_cast<double>(config_.partialFillThreshold)) {
        // ── Single fill ─────────────────────────────────────────────
        Order order = *orderOpt;
        char execType = ExecType::Fill;
        executeSingleFill(order, basePrice, order.leavesQty, execType);
        orderManager_.updateOrder(order);
        tradeLogger_.logFill(order, execType);
        if (fillCallback_) {
            fillCallback_(order, execType);
        }
    } else {
        // ── Partial fills on a detached thread ──────────────────────
        int numChunks;
        {
            std::lock_guard<std::mutex> lock(rngMutex_);
            std::uniform_int_distribution<int> chunkDist(config_.minFillChunks, config_.maxFillChunks);
            numChunks = chunkDist(rng_);
        }

        // Capture orderId by value for the thread
        std::string capturedOrderId = orderId;
        double capturedBasePrice = basePrice;
        int capturedNumChunks = numChunks;

        std::thread fillThread([this, capturedOrderId, capturedBasePrice, capturedNumChunks]() {
            double remainingQty = 0.0;
            {
                auto orderOpt = orderManager_.findByOrderId(capturedOrderId);
                if (!orderOpt) return;
                remainingQty = orderOpt->leavesQty;
            }

            for (int i = 0; i < capturedNumChunks && remainingQty > 0.0; ++i) {
                // Check if cancel was requested
                auto orderOpt = orderManager_.findByOrderId(capturedOrderId);
                if (!orderOpt || orderOpt->cancelRequested || orderOpt->isTerminal()) {
                    break;
                }

                Order order = *orderOpt;

                // Calculate chunk size
                double chunkQty;
                if (i == capturedNumChunks - 1) {
                    // Last chunk gets the remainder
                    chunkQty = remainingQty;
                } else {
                    // Random chunk size, ensuring at least 1 remains for each future chunk
                    double maxChunk = remainingQty - (capturedNumChunks - i - 1);
                    if (maxChunk < 1.0) maxChunk = 1.0;
                    {
                        std::lock_guard<std::mutex> lock(rngMutex_);
                        std::uniform_real_distribution<double> qtyDist(1.0, std::max(1.0, maxChunk));
                        chunkQty = std::floor(qtyDist(rng_));
                        if (chunkQty < 1.0) chunkQty = 1.0;
                    }
                }

                // Apply slippage
                double fillPrice = applySlippage(capturedBasePrice);

                // Determine ExecType
                bool isFinalFill = (remainingQty - chunkQty) <= 0.0;
                char execType = isFinalFill ? ExecType::Fill : ExecType::PartialFill;

                // Execute the fill
                executeSingleFill(order, fillPrice, chunkQty, execType);
                orderManager_.updateOrder(order);
                tradeLogger_.logFill(order, execType);

                if (fillCallback_) {
                    fillCallback_(order, execType);
                }

                remainingQty -= chunkQty;

                // Delay between partial fills (not after the last one)
                if (!isFinalFill && remainingQty > 0.0) {
                    int delayMs;
                    {
                        std::lock_guard<std::mutex> lock(rngMutex_);
                        std::uniform_int_distribution<int> delayDist(
                            config_.fillDelayMsMin, config_.fillDelayMsMax);
                        delayMs = delayDist(rng_);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                }
            }
        });

        fillThread.detach();
    }
}

void MatchingEngine::executeSingleFill(Order& order, double fillPrice,
                                        double fillQty, char execType) {
    // Weighted average price calculation
    double totalCost = order.avgPx * order.cumQty + fillPrice * fillQty;
    order.cumQty += fillQty;
    order.leavesQty -= fillQty;
    if (order.leavesQty < 0.0) order.leavesQty = 0.0;
    order.avgPx = (order.cumQty > 0.0) ? totalCost / order.cumQty : 0.0;
    order.lastPx = fillPrice;
    order.lastShares = fillQty;

    // Set status
    if (order.leavesQty <= 0.0) {
        order.status = OrdStatus::Filled;
    } else {
        order.status = OrdStatus::PartiallyFilled;
    }

    order.updateTime = std::chrono::system_clock::now();
}

bool MatchingEngine::isLimitFillable(const Order& order, double marketPrice) const {
    if (order.side == Side::Buy) {
        return marketPrice <= order.price; // Market at or below limit
    } else {
        return marketPrice >= order.price; // Market at or above limit
    }
}

double MatchingEngine::applySlippage(double basePrice) {
    std::lock_guard<std::mutex> lock(rngMutex_);
    std::uniform_real_distribution<double> slippageDist(
        -config_.slippageBps, config_.slippageBps);
    double slippageFraction = slippageDist(rng_) / 10000.0;
    return basePrice * (1.0 + slippageFraction);
}

} // namespace MockCQG

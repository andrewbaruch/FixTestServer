#include "OrderManager.h"
#include <algorithm>

namespace MockCQG {

// ── addOrder ────────────────────────────────────────────────────────
std::string OrderManager::addOrder(Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Assign server-generated order ID
    order.orderId   = IdGenerator::nextOrderId();
    order.status    = OrdStatus::New;
    order.leavesQty = order.orderQty;
    order.cumQty    = 0.0;
    order.avgPx     = 0.0;
    order.createTime = std::chrono::system_clock::now();
    order.updateTime = order.createTime;

    // Store in primary map
    ordersByOrderId_[order.orderId] = order;

    // Index by clOrdId for fast lookup
    if (!order.clOrdId.empty()) {
        clOrdIdToOrderId_[order.clOrdId] = order.orderId;
    }

    return order.orderId;
}

// ── findByClOrdId ───────────────────────────────────────────────────
std::optional<Order> OrderManager::findByClOrdId(const std::string& clOrdId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto mapIt = clOrdIdToOrderId_.find(clOrdId);
    if (mapIt == clOrdIdToOrderId_.end()) {
        return std::nullopt;
    }

    auto orderIt = ordersByOrderId_.find(mapIt->second);
    if (orderIt == ordersByOrderId_.end()) {
        return std::nullopt;  // Shouldn't happen if maps are consistent
    }

    return orderIt->second;
}

// ── findByOrderId ───────────────────────────────────────────────────
std::optional<Order> OrderManager::findByOrderId(const std::string& orderId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = ordersByOrderId_.find(orderId);
    if (it == ordersByOrderId_.end()) {
        return std::nullopt;
    }

    return it->second;
}

// ── updateOrder ─────────────────────────────────────────────────────
bool OrderManager::updateOrder(const Order& order) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = ordersByOrderId_.find(order.orderId);
    if (it == ordersByOrderId_.end()) {
        return false;  // Order not found
    }

    // If the clOrdId has changed (e.g. cancel/replace), update the index
    const std::string& oldClOrdId = it->second.clOrdId;
    if (oldClOrdId != order.clOrdId) {
        // Keep the old clOrdId mapping (it was a valid ID at some point)
        // and add the new one
        if (!order.clOrdId.empty()) {
            clOrdIdToOrderId_[order.clOrdId] = order.orderId;
        }
    }

    // Replace the stored order with the updated copy
    Order updated = order;
    updated.updateTime = std::chrono::system_clock::now();
    it->second = updated;

    return true;
}

// ── getAllOrders ─────────────────────────────────────────────────────
std::vector<Order> OrderManager::getAllOrders() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Order> result;
    result.reserve(ordersByOrderId_.size());
    for (const auto& [id, order] : ordersByOrderId_) {
        result.push_back(order);
    }
    return result;
}

// ── getActiveOrders ─────────────────────────────────────────────────
std::vector<Order> OrderManager::getActiveOrders() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Order> result;
    for (const auto& [id, order] : ordersByOrderId_) {
        if (!order.isTerminal()) {
            result.push_back(order);
        }
    }
    return result;
}

// ── getOrdersByAccount ──────────────────────────────────────────────
std::vector<Order> OrderManager::getOrdersByAccount(const std::string& account) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Order> result;
    for (const auto& [id, order] : ordersByOrderId_) {
        if (order.account == account) {
            result.push_back(order);
        }
    }
    return result;
}

// ── getOrdersBySymbol ───────────────────────────────────────────────
std::vector<Order> OrderManager::getOrdersBySymbol(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Order> result;
    for (const auto& [id, order] : ordersByOrderId_) {
        if (order.symbol == symbol) {
            result.push_back(order);
        }
    }
    return result;
}

// ── clOrdIdExists ───────────────────────────────────────────────────
bool OrderManager::clOrdIdExists(const std::string& clOrdId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clOrdIdToOrderId_.find(clOrdId) != clOrdIdToOrderId_.end();
}

// ── setCancelRequested ──────────────────────────────────────────────
bool OrderManager::setCancelRequested(const std::string& orderId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = ordersByOrderId_.find(orderId);
    if (it == ordersByOrderId_.end()) {
        return false;
    }

    it->second.cancelRequested = true;
    it->second.updateTime = std::chrono::system_clock::now();
    return true;
}

} // namespace MockCQG

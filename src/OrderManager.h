#pragma once

#include "Order.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <optional>

namespace MockCQG {

class OrderManager {
public:
    OrderManager() = default;

    // Add a new order. Assigns orderId, sets createTime, initializes leavesQty = orderQty, status = New.
    // Returns the assigned orderId.
    std::string addOrder(Order& order);

    // Lookup by client order ID. Returns nullopt if not found.
    std::optional<Order> findByClOrdId(const std::string& clOrdId) const;

    // Lookup by server order ID. Returns nullopt if not found.
    std::optional<Order> findByOrderId(const std::string& orderId) const;

    // Update an existing order (matched by orderId).
    bool updateOrder(const Order& order);

    // Get all orders (snapshot).
    std::vector<Order> getAllOrders() const;

    // Get all non-terminal (active) orders.
    std::vector<Order> getActiveOrders() const;

    // Filter by account.
    std::vector<Order> getOrdersByAccount(const std::string& account) const;

    // Filter by symbol.
    std::vector<Order> getOrdersBySymbol(const std::string& symbol) const;

    // Check if a ClOrdID has been used already (for duplicate detection).
    bool clOrdIdExists(const std::string& clOrdId) const;

    // Set cancel flag on an order (to interrupt partial fills).
    bool setCancelRequested(const std::string& orderId);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Order> ordersByOrderId_;    // orderId -> Order
    std::unordered_map<std::string, std::string> clOrdIdToOrderId_; // clOrdId -> orderId
};

} // namespace MockCQG

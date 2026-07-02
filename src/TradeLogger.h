#pragma once

#include "Order.h"
#include <string>
#include <mutex>
#include <fstream>

namespace MockCQG {

class TradeLogger {
public:
    explicit TradeLogger(const std::string& filePath = "trades.csv");
    ~TradeLogger();

    // Log a fill event (partial or full). Each partial fill is a separate row.
    void logFill(const Order& order, char execType);

    // Log a non-fill event (New, Cancelled, Rejected, Replaced, etc.)
    void logEvent(const Order& order, char execType);

private:
    void writeHeader();
    std::string currentTimestamp() const;
    std::string sideToString(char side) const;
    std::string statusToString(char status) const;
    std::string execTypeToString(char execType) const;

    std::ofstream file_;
    std::mutex    mutex_;
};

} // namespace MockCQG

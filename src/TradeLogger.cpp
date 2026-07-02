#include "TradeLogger.h"
#include <chrono>
#include <iomanip>
#include <sstream>

namespace MockCQG {

// ────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ────────────────────────────────────────────────────────────────────

TradeLogger::TradeLogger(const std::string& filePath)
{
    // Open in append + read mode to check if the file already has content.
    file_.open(filePath, std::ios::app | std::ios::ate);
    if (file_.is_open() && file_.tellp() == 0) {
        writeHeader();
    }
}

TradeLogger::~TradeLogger()
{
    if (file_.is_open()) {
        file_.close();
    }
}

// ────────────────────────────────────────────────────────────────────
// CSV Header
// ────────────────────────────────────────────────────────────────────

void TradeLogger::writeHeader()
{
    file_ << "Timestamp,ExecID,OrderID,ClOrdID,Symbol,Side,"
             "OrderQty,FillQty,FillPrice,CumQty,LeavesQty,AvgPx,"
             "OrdStatus,ExecType,Account\n";
    file_.flush();
}

// ────────────────────────────────────────────────────────────────────
// Public API
// ────────────────────────────────────────────────────────────────────

void TradeLogger::logFill(const Order& order, char execType)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_.is_open()) return;

    std::string execId = IdGenerator::nextExecId();

    file_ << currentTimestamp()          << ","
          << execId                      << ","
          << order.orderId               << ","
          << order.clOrdId               << ","
          << order.symbol                << ","
          << sideToString(order.side)    << ","
          << order.orderQty              << ","
          << order.lastShares            << ","   // FillQty
          << order.lastPx                << ","   // FillPrice
          << order.cumQty                << ","
          << order.leavesQty             << ","
          << order.avgPx                 << ","
          << statusToString(order.status)<< ","
          << execTypeToString(execType)  << ","
          << order.account               << "\n";
    file_.flush();
}

void TradeLogger::logEvent(const Order& order, char execType)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_.is_open()) return;

    std::string execId = IdGenerator::nextExecId();

    file_ << currentTimestamp()          << ","
          << execId                      << ","
          << order.orderId               << ","
          << order.clOrdId               << ","
          << order.symbol                << ","
          << sideToString(order.side)    << ","
          << order.orderQty              << ","
          << 0                           << ","   // FillQty  (no fill)
          << 0                           << ","   // FillPrice (no fill)
          << order.cumQty                << ","
          << order.leavesQty             << ","
          << order.avgPx                 << ","
          << statusToString(order.status)<< ","
          << execTypeToString(execType)  << ","
          << order.account               << "\n";
    file_.flush();
}

// ────────────────────────────────────────────────────────────────────
// Helpers
// ────────────────────────────────────────────────────────────────────

std::string TradeLogger::currentTimestamp() const
{
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) % 1000;

    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif

    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(3) << ms.count()
        << 'Z';
    return oss.str();
}

std::string TradeLogger::sideToString(char side) const
{
    switch (side) {
        case Side::Buy:  return "Buy";
        case Side::Sell: return "Sell";
        default:         return std::string(1, side);
    }
}

std::string TradeLogger::statusToString(char status) const
{
    switch (status) {
        case OrdStatus::New:             return "New";
        case OrdStatus::PartiallyFilled: return "PartiallyFilled";
        case OrdStatus::Filled:          return "Filled";
        case OrdStatus::Cancelled:       return "Cancelled";
        case OrdStatus::Replaced:        return "Replaced";
        case OrdStatus::PendingCancel:   return "PendingCancel";
        case OrdStatus::Rejected:        return "Rejected";
        case OrdStatus::PendingReplace:  return "PendingReplace";
        default:                         return std::string(1, status);
    }
}

std::string TradeLogger::execTypeToString(char execType) const
{
    switch (execType) {
        case ExecType::New:           return "New";
        case ExecType::PartialFill:   return "PartialFill";
        case ExecType::Fill:          return "Fill";
        case ExecType::Cancelled:     return "Cancelled";
        case ExecType::Replaced:      return "Replaced";
        case ExecType::PendingCancel: return "PendingCancel";
        case ExecType::Rejected:      return "Rejected";
        case ExecType::PendingReplace:return "PendingReplace";
        default:                      return std::string(1, execType);
    }
}

} // namespace MockCQG

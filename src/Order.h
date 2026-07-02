#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <sstream>
#include <iomanip>

namespace MockCQG {

// ── FIX 4.2 OrdStatus (Tag 39) ─────────────────────────────────────
namespace OrdStatus {
    constexpr char New             = '0';
    constexpr char PartiallyFilled = '1';
    constexpr char Filled          = '2';
    constexpr char Cancelled       = '4';
    constexpr char Replaced        = '5';
    constexpr char PendingCancel   = '6';
    constexpr char Rejected        = '8';
    constexpr char PendingReplace  = 'E';
}

// ── FIX 4.2 ExecType (Tag 150) ─────────────────────────────────────
namespace ExecType {
    constexpr char New             = '0';
    constexpr char PartialFill     = '1';
    constexpr char Fill            = '2';
    constexpr char Cancelled       = '4';
    constexpr char Replaced        = '5';
    constexpr char PendingCancel   = '6';
    constexpr char Rejected        = '8';
    constexpr char PendingReplace  = 'E';
}

// ── FIX 4.2 OrdType (Tag 40) ───────────────────────────────────────
namespace OrdType {
    constexpr char Market    = '1';
    constexpr char Limit     = '2';
    constexpr char Stop      = '3';
    constexpr char StopLimit = '4';
}

// ── FIX 4.2 Side (Tag 54) ──────────────────────────────────────────
namespace Side {
    constexpr char Buy  = '1';
    constexpr char Sell = '2';
}

// ── FIX 4.2 MassStatusReqType (Tag 585) ────────────────────────────
namespace MassStatusReqType {
    constexpr int BySymbol   = 1;
    constexpr int ByAccount  = 3;
    constexpr int AllOrders  = 7;
}

// ── Partial Fill Configuration ──────────────────────────────────────
struct FillConfig {
    int    partialFillThreshold = 100;   // Orders with qty > this get partial fills
    int    minFillChunks        = 2;
    int    maxFillChunks        = 6;
    int    fillDelayMsMin       = 50;    // Min ms between partial fills
    int    fillDelayMsMax       = 500;   // Max ms between partial fills
    double slippageBps          = 5.0;   // Max slippage in basis points (±)
};

// ── Order Struct ────────────────────────────────────────────────────
struct Order {
    std::string orderId;        // Server-assigned (CQG-XXXX)
    std::string clOrdId;        // Client-assigned (Tag 11)
    std::string origClOrdId;    // For cancel/replace (Tag 41)
    std::string symbol;         // Tag 55
    char        side     = '1'; // Tag 54 (1=Buy, 2=Sell)
    double      orderQty = 0.0; // Tag 38
    char        ordType  = '1'; // Tag 40 (1=Market, 2=Limit, etc.)
    double      price    = 0.0; // Tag 44 (Limit price)
    double      stopPx   = 0.0; // Tag 99 (Stop price)
    char        status   = OrdStatus::New; // Tag 39
    double      cumQty   = 0.0; // Tag 14  (total filled so far)
    double      leavesQty= 0.0; // Tag 151 (remaining)
    double      avgPx    = 0.0; // Tag 6   (weighted avg fill price)
    double      lastPx   = 0.0; // Tag 31  (last fill price)
    double      lastShares=0.0; // Tag 32  (last fill qty)
    std::string account;        // Tag 1
    char timeInForce = '0';     // Tag 59 (Default to Day)
    char handlInst = '1';       // Tag 21 (Default to automated execution, private)
    std::string text;           // Tag 58  (free text / reject reason)

    // Timestamps
    std::chrono::system_clock::time_point createTime;
    std::chrono::system_clock::time_point updateTime;

    // Fill tracking
    bool cancelRequested = false; // Flag to abort ongoing partial fills

    bool isTerminal() const {
        return status == OrdStatus::Filled
            || status == OrdStatus::Cancelled
            || status == OrdStatus::Rejected;
    }
};

// ── Unique ID Generators ────────────────────────────────────────────
class IdGenerator {
public:
    static std::string nextOrderId() {
        int id = orderCounter_++;
        std::ostringstream oss;
        oss << "CQG-" << std::setfill('0') << std::setw(6) << id;
        return oss.str();
    }

    static std::string nextExecId() {
        int id = execCounter_++;
        return std::to_string(id);
    }

private:
    static inline std::atomic<int> orderCounter_{1};
    static inline std::atomic<int> execCounter_{1};
};

} // namespace MockCQG

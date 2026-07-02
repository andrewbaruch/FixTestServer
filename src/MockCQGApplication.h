#pragma once

#include "Order.h"
#include "OrderManager.h"
#include "MatchingEngine.h"
#include "PriceFetcher.h"
#include "TradeLogger.h"

#include <quickfix/Application.h>
#include <quickfix/MessageCracker.h>
#include <quickfix/SessionID.h>
#include <quickfix/Session.h>
#include <quickfix/fix42/NewOrderSingle.h>
#include <quickfix/fix42/OrderCancelRequest.h>
#include <quickfix/fix42/OrderCancelReplaceRequest.h>
#include <quickfix/fix42/OrderStatusRequest.h>
#include <quickfix/fix42/ExecutionReport.h>
#include <quickfix/fix42/OrderCancelReject.h>
#include <quickfix/fix42/MessageCracker.h>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <deque>

namespace MockCQG {

class MockCQGApplication : public FIX::Application, public FIX42::MessageCracker {
public:
    MockCQGApplication();
    ~MockCQGApplication() override;

private:
    // ── FIX::Application callbacks ──────────────────────────────────
    void onCreate(const FIX::SessionID& sessionID) override;
    void onLogon(const FIX::SessionID& sessionID) override;
    void onLogout(const FIX::SessionID& sessionID) override;
    void toAdmin(FIX::Message& message, const FIX::SessionID& sessionID) override;
    void toApp(FIX::Message& message, const FIX::SessionID& sessionID)
        throw(FIX::DoNotSend) override;
    void fromAdmin(const FIX::Message& message, const FIX::SessionID& sessionID)
        throw(FIX::FieldNotFound, FIX::IncorrectDataFormat,
              FIX::IncorrectTagValue, FIX::RejectLogon) override;
    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID)
        throw(FIX::FieldNotFound, FIX::IncorrectDataFormat,
              FIX::IncorrectTagValue, FIX::UnsupportedMessageType) override;

    // ── FIX42::MessageCracker overrides ─────────────────────────────
    void onMessage(const FIX42::NewOrderSingle& message,
                   const FIX::SessionID& sessionID) override;
    void onMessage(const FIX42::OrderCancelRequest& message,
                   const FIX::SessionID& sessionID) override;
    void onMessage(const FIX42::OrderCancelReplaceRequest& message,
                   const FIX::SessionID& sessionID) override;
    void onMessage(const FIX42::OrderStatusRequest& message,
                   const FIX::SessionID& sessionID) override;

    // ── OrderMassStatusRequest handler (manual, not in MessageCracker) ──
    void handleOrderMassStatusRequest(const FIX::Message& message,
                                      const FIX::SessionID& sessionID);

    // ── ExecutionReport builders ────────────────────────────────────
    FIX42::ExecutionReport buildExecReport(const Order& order, char execType,
                                            char execTransType,
                                            const FIX::SessionID& sessionID);

    void sendExecReport(const Order& order, char execType, char execTransType,
                        const FIX::SessionID& sessionID);

    FIX42::OrderCancelReject buildCancelReject(
        const std::string& clOrdId, const std::string& origClOrdId,
        const std::string& orderId, int cxlRejReason, const std::string& reason,
        const FIX::SessionID& sessionID);

    // ── UBR Ack builder ─────────────────────────────────────────────
    void sendOrderMassStatusAck(const std::string& reqId, int reqType, int result,
                                const FIX::SessionID& sessionID);

    // ── Mass Status Rate Limiting ───────────────────────────────────
    bool checkMassStatusRateLimit(const std::string& account);

    // ── Fill callback from MatchingEngine ────────────────────────────
    void onFill(const Order& order, char execType);

    // ── Components ──────────────────────────────────────────────────
    OrderManager  orderManager_;
    PriceFetcher  priceFetcher_;
    TradeLogger   tradeLogger_;
    std::unique_ptr<MatchingEngine> matchingEngine_;

    // ── Session tracking ────────────────────────────────────────────
    // Map orderId -> sessionID for routing fill callbacks to the right session
    std::unordered_map<std::string, FIX::SessionID> orderSessionMap_;
    std::mutex sessionMapMutex_;

    // ── Mass Status Rate Limiting ───────────────────────────────────
    // account -> timestamps of recent mass status requests
    std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>>
        massStatusTimestamps_;
    std::mutex rateLimitMutex_;
    static constexpr int    MASS_STATUS_MAX_REQUESTS = 5;
    static constexpr int    MASS_STATUS_WINDOW_SECONDS = 20;
};

} // namespace MockCQG

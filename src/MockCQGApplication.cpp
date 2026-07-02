#include "MockCQGApplication.h"

#include <quickfix/FieldTypes.h>
#include <quickfix/Fields.h>
#include <quickfix/FieldConvertors.h>
#include <quickfix/fix42/Reject.h>

#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace {
    std::string getSendingTimeHR() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()) % 1000000;
        auto timer = std::chrono::system_clock::to_time_t(now);
        std::tm bt;
#ifdef _WIN32
        gmtime_s(&bt, &timer);
#else
        gmtime_r(&timer, &bt);
#endif
        std::ostringstream oss;
        oss << std::put_time(&bt, "%Y%m%d-%H:%M:%S") << '.' << std::setfill('0') << std::setw(6) << ms.count();
        return oss.str();
    }
}

namespace MockCQG {

// ─────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────

MockCQGApplication::MockCQGApplication()
    : priceFetcher_(60)  // 60-second cache TTL
    , tradeLogger_("trades.csv")
{
    matchingEngine_ = std::make_unique<MatchingEngine>(
        orderManager_, priceFetcher_, tradeLogger_);

    // Set the fill callback so MatchingEngine can notify us of async fills
    matchingEngine_->setFillCallback(
        [this](const Order& order, char execType) {
            onFill(order, execType);
        });

    std::cout << "[MockCQG] Application initialized." << std::endl;
}

MockCQGApplication::~MockCQGApplication() = default;

// ─────────────────────────────────────────────────────────────────────
// FIX::Application session lifecycle
// ─────────────────────────────────────────────────────────────────────

void MockCQGApplication::onCreate(const FIX::SessionID& sessionID) {
    std::cout << "[MockCQG] Session created: " << sessionID << std::endl;
}

void MockCQGApplication::onLogon(const FIX::SessionID& sessionID) {
    std::cout << "[MockCQG] Client logged on: " << sessionID << std::endl;
}

void MockCQGApplication::onLogout(const FIX::SessionID& sessionID) {
    std::cout << "[MockCQG] Client logged out: " << sessionID << std::endl;
}

void MockCQGApplication::toAdmin(FIX::Message& message,
                                  const FIX::SessionID& /*sessionID*/) {
    // CQG requires tag 20173 (SendingTimeHR) on all messages
    message.getHeader().setField(FIX::StringField(20173, getSendingTimeHR()));
}

void MockCQGApplication::toApp(FIX::Message& message,
                                const FIX::SessionID& /*sessionID*/)
    throw(FIX::DoNotSend) {
    // CQG requires tag 20173 (SendingTimeHR) on all messages
    message.getHeader().setField(FIX::StringField(20173, getSendingTimeHR()));
}

void MockCQGApplication::fromAdmin(const FIX::Message& /*message*/,
                                    const FIX::SessionID& /*sessionID*/)
    throw(FIX::FieldNotFound, FIX::IncorrectDataFormat,
          FIX::IncorrectTagValue, FIX::RejectLogon) {
    // Accept all admin messages (Logon, Heartbeat, etc.)
}

void MockCQGApplication::fromApp(const FIX::Message& message,
                                  const FIX::SessionID& sessionID)
    throw(FIX::FieldNotFound, FIX::IncorrectDataFormat,
          FIX::IncorrectTagValue, FIX::UnsupportedMessageType) {

    // Check for OrderMassStatusRequest (AF) which isn't in the standard
    // FIX42 MessageCracker (it was added in FIX 4.2 extensions)
    FIX::MsgType msgType;
    message.getHeader().getField(msgType);

    if (msgType.getValue() == "UAF") {
        handleOrderMassStatusRequest(message, sessionID);
        return;
    }

    // Standard message cracking for D, F, G, H
    crack(message, sessionID);
}

// ─────────────────────────────────────────────────────────────────────
// NewOrderSingle (D)
// ─────────────────────────────────────────────────────────────────────

void MockCQGApplication::onMessage(const FIX42::NewOrderSingle& message,
                                    const FIX::SessionID& sessionID) {
    // Extract fields
    FIX::ClOrdID    clOrdId;
    FIX::Symbol     symbol;
    FIX::Side       side;
    FIX::OrdType    ordType;
    FIX::OrderQty   orderQty;

    message.get(clOrdId);
    message.get(symbol);
    message.get(side);
    message.get(ordType);
    message.get(orderQty);

    std::cout << "[MockCQG] NewOrderSingle: ClOrdID=" << clOrdId.getValue()
              << " Symbol=" << symbol.getValue()
              << " Side=" << side.getValue()
              << " OrdType=" << ordType.getValue()
              << " Qty=" << orderQty.getValue() << std::endl;

    // ── Validation ──────────────────────────────────────────────────

    // Check for Account (Required by CQG)
    if (!message.isSetField(FIX::FIELD::Account)) {
        Order rejectOrder;
        rejectOrder.clOrdId = clOrdId.getValue();
        rejectOrder.symbol = symbol.getValue();
        rejectOrder.side = side.getValue();
        rejectOrder.orderQty = orderQty.getValue();
        rejectOrder.ordType = ordType.getValue();
        rejectOrder.status = OrdStatus::Rejected;
        rejectOrder.orderId = IdGenerator::nextOrderId();
        rejectOrder.text = "Account (1) is required";
        rejectOrder.leavesQty = 0;
        rejectOrder.cumQty = 0;

        sendExecReport(rejectOrder, ExecType::Rejected, FIX::ExecTransType_NEW, sessionID);
        tradeLogger_.logEvent(rejectOrder, ExecType::Rejected);
        std::cerr << "[MockCQG] Rejected: Missing Account" << std::endl;
        return;
    }

    // Check for duplicate ClOrdID
    if (orderManager_.clOrdIdExists(clOrdId.getValue())) {
        Order rejectOrder;
        rejectOrder.clOrdId = clOrdId.getValue();
        rejectOrder.symbol = symbol.getValue();
        rejectOrder.side = side.getValue();
        rejectOrder.orderQty = orderQty.getValue();
        rejectOrder.ordType = ordType.getValue();
        rejectOrder.status = OrdStatus::Rejected;
        rejectOrder.orderId = IdGenerator::nextOrderId();
        rejectOrder.text = "Duplicate ClOrdID";
        rejectOrder.leavesQty = 0;
        rejectOrder.cumQty = 0;

        sendExecReport(rejectOrder, ExecType::Rejected, FIX::ExecTransType_NEW, sessionID);
        tradeLogger_.logEvent(rejectOrder, ExecType::Rejected);
        std::cerr << "[MockCQG] Rejected: Duplicate ClOrdID " << clOrdId.getValue() << std::endl;
        return;
    }

    // Check for valid quantity
    if (orderQty.getValue() <= 0) {
        Order rejectOrder;
        rejectOrder.clOrdId = clOrdId.getValue();
        rejectOrder.symbol = symbol.getValue();
        rejectOrder.side = side.getValue();
        rejectOrder.orderQty = orderQty.getValue();
        rejectOrder.ordType = ordType.getValue();
        rejectOrder.status = OrdStatus::Rejected;
        rejectOrder.orderId = IdGenerator::nextOrderId();
        rejectOrder.text = "Order quantity must be > 0";
        rejectOrder.leavesQty = 0;
        rejectOrder.cumQty = 0;

        sendExecReport(rejectOrder, ExecType::Rejected, FIX::ExecTransType_NEW, sessionID);
        tradeLogger_.logEvent(rejectOrder, ExecType::Rejected);
        std::cerr << "[MockCQG] Rejected: Invalid qty " << orderQty.getValue() << std::endl;
        return;
    }

    // ── Build the Order ─────────────────────────────────────────────
    Order order;
    order.clOrdId = clOrdId.getValue();
    order.symbol  = symbol.getValue();
    order.side    = side.getValue();
    order.ordType = ordType.getValue();
    order.orderQty = orderQty.getValue();

    // Optional fields
    FIX::Price price;
    if (message.isSet(price)) {
        message.get(price);
        order.price = price.getValue();
    }

    FIX::StopPx stopPx;
    if (message.isSet(stopPx)) {
        message.get(stopPx);
        order.stopPx = stopPx.getValue();
    }

    FIX::Account account;
    if (message.isSet(account)) {
        message.get(account);
        order.account = account.getValue();
    }

    if (message.isSetField(FIX::FIELD::TimeInForce)) {
        order.timeInForce = FIX::CharConvertor::convert(message.getField(FIX::FIELD::TimeInForce));
    }

    if (message.isSetField(FIX::FIELD::HandlInst)) {
        order.handlInst = FIX::CharConvertor::convert(message.getField(FIX::FIELD::HandlInst));
    }

    // Add to OrderManager (assigns orderId, sets status=New)
    std::string orderId = orderManager_.addOrder(order);

    // Track which session this order belongs to (for async fill callbacks)
    {
        std::lock_guard<std::mutex> lock(sessionMapMutex_);
        orderSessionMap_[orderId] = sessionID;
    }

    // ── Send New Ack ────────────────────────────────────────────────
    sendExecReport(order, ExecType::New, FIX::ExecTransType_NEW, sessionID);
    tradeLogger_.logEvent(order, ExecType::New);

    // ── Attempt Fill ────────────────────────────────────────────────
    matchingEngine_->tryFill(orderId);
}

// ─────────────────────────────────────────────────────────────────────
// OrderCancelRequest (F)
// ─────────────────────────────────────────────────────────────────────

void MockCQGApplication::onMessage(const FIX42::OrderCancelRequest& message,
                                    const FIX::SessionID& sessionID) {
    FIX::ClOrdID     clOrdId;
    FIX::OrigClOrdID origClOrdId;

    message.get(clOrdId);
    message.get(origClOrdId);

    std::cout << "[MockCQG] OrderCancelRequest: ClOrdID=" << clOrdId.getValue()
              << " OrigClOrdID=" << origClOrdId.getValue() << std::endl;

    // Find the original order
    auto orderOpt = orderManager_.findByClOrdId(origClOrdId.getValue());
    if (!orderOpt) {
        // Order not found — send OrderCancelReject
        auto reject = buildCancelReject(
            clOrdId.getValue(), origClOrdId.getValue(), "UNKNOWN",
            1, "Unknown order", sessionID); // 1 = Unknown order
        FIX::Session::sendToTarget(reject, sessionID);
        std::cerr << "[MockCQG] Cancel rejected: Unknown OrigClOrdID "
                  << origClOrdId.getValue() << std::endl;
        return;
    }

    Order order = *orderOpt;

    // Validate OrderID if provided
    if (message.isSetField(FIX::FIELD::OrderID)) {
        if (message.getField(FIX::FIELD::OrderID) != order.orderId) {
            auto reject = buildCancelReject(
                clOrdId.getValue(), origClOrdId.getValue(), order.orderId,
                1, "OrderID does not match", sessionID); // 1 = Unknown order
            FIX::Session::sendToTarget(reject, sessionID);
            return;
        }
    }

    // Can't cancel terminal orders
    if (order.isTerminal()) {
        auto reject = buildCancelReject(
            clOrdId.getValue(), origClOrdId.getValue(), order.orderId,
            0, "Order already in terminal state", sessionID); // 0 = Too late to cancel
        FIX::Session::sendToTarget(reject, sessionID);
        return;
    }

    // Set cancel flag to interrupt any ongoing partial fills
    orderManager_.setCancelRequested(order.orderId);

    // ── PendingCancel ack ───────────────────────────────────────────
    order.status = OrdStatus::PendingCancel;
    order.clOrdId = clOrdId.getValue();  // Update to cancel request's ClOrdID
    order.origClOrdId = origClOrdId.getValue();
    orderManager_.updateOrder(order);
    sendExecReport(order, ExecType::PendingCancel, FIX::ExecTransType_NEW, sessionID);

    // ── Cancelled confirm ───────────────────────────────────────────
    order.status = OrdStatus::Cancelled;
    order.leavesQty = 0;
    orderManager_.updateOrder(order);
    sendExecReport(order, ExecType::Cancelled, FIX::ExecTransType_NEW, sessionID);
    tradeLogger_.logEvent(order, ExecType::Cancelled);

    std::cout << "[MockCQG] Order cancelled: " << order.orderId
              << " CumQty=" << order.cumQty << std::endl;
}

// ─────────────────────────────────────────────────────────────────────
// OrderCancelReplaceRequest (G)
// ─────────────────────────────────────────────────────────────────────

void MockCQGApplication::onMessage(const FIX42::OrderCancelReplaceRequest& message,
                                    const FIX::SessionID& sessionID) {
    FIX::ClOrdID     clOrdId;
    FIX::OrigClOrdID origClOrdId;

    message.get(clOrdId);
    message.get(origClOrdId);

    std::cout << "[MockCQG] OrderCancelReplaceRequest: ClOrdID=" << clOrdId.getValue()
              << " OrigClOrdID=" << origClOrdId.getValue() << std::endl;

    // Validate Account
    if (!message.isSetField(FIX::FIELD::Account)) {
        auto reject = buildCancelReject(
            clOrdId.getValue(), origClOrdId.getValue(), "UNKNOWN",
            1, "Account (1) is required for replace", sessionID);
        FIX::Session::sendToTarget(reject, sessionID);
        return;
    }

    // Find the original order
    auto orderOpt = orderManager_.findByClOrdId(origClOrdId.getValue());
    if (!orderOpt) {
        auto reject = buildCancelReject(
            clOrdId.getValue(), origClOrdId.getValue(), "UNKNOWN",
            1, "Unknown order", sessionID);
        FIX::Session::sendToTarget(reject, sessionID);
        return;
    }

    Order order = *orderOpt;

    if (order.isTerminal()) {
        auto reject = buildCancelReject(
            clOrdId.getValue(), origClOrdId.getValue(), order.orderId,
            0, "Order already in terminal state", sessionID);
        FIX::Session::sendToTarget(reject, sessionID);
        return;
    }

    // Validate OrderID if provided
    if (message.isSetField(FIX::FIELD::OrderID)) {
        if (message.getField(FIX::FIELD::OrderID) != order.orderId) {
            auto reject = buildCancelReject(
                clOrdId.getValue(), origClOrdId.getValue(), order.orderId,
                1, "OrderID does not match", sessionID);
            FIX::Session::sendToTarget(reject, sessionID);
            return;
        }
    }

    // Validate Side matches
    if (message.isSetField(FIX::FIELD::Side)) {
        if (message.getField(FIX::FIELD::Side) != std::string(1, order.side)) {
            auto reject = buildCancelReject(
                clOrdId.getValue(), origClOrdId.getValue(), order.orderId,
                2, "Side cannot be changed", sessionID); // 2 = Broker / Exchange option
            FIX::Session::sendToTarget(reject, sessionID);
            return;
        }
    }

    // Validate HandlInst matches
    if (message.isSetField(FIX::FIELD::HandlInst)) {
        if (message.getField(FIX::FIELD::HandlInst) != std::string(1, order.handlInst)) {
            auto reject = buildCancelReject(
                clOrdId.getValue(), origClOrdId.getValue(), order.orderId,
                2, "HandlInst cannot be changed", sessionID);
            FIX::Session::sendToTarget(reject, sessionID);
            return;
        }
    }

    // ── PendingReplace ack ──────────────────────────────────────────
    order.status = OrdStatus::PendingReplace;
    order.origClOrdId = origClOrdId.getValue();
    order.clOrdId = clOrdId.getValue();
    orderManager_.updateOrder(order);
    sendExecReport(order, ExecType::PendingReplace, FIX::ExecTransType_NEW, sessionID);

    // ── Apply modifications ─────────────────────────────────────────
    FIX::OrderQty orderQty;
    if (message.isSet(orderQty)) {
        message.get(orderQty);
        double newQty = orderQty.getValue();
        if (newQty > order.cumQty) {
            order.orderQty = newQty;
            order.leavesQty = newQty - order.cumQty;
        }
    }

    FIX::Price price;
    if (message.isSet(price)) {
        message.get(price);
        order.price = price.getValue();
    }

    FIX::StopPx stopPx;
    if (message.isSet(stopPx)) {
        message.get(stopPx);
        order.stopPx = stopPx.getValue();
    }

    // ── Replaced confirm ────────────────────────────────────────────
    order.status = OrdStatus::New; // Replaced order goes back to New
    orderManager_.updateOrder(order);
    sendExecReport(order, ExecType::Replaced, FIX::ExecTransType_NEW, sessionID);
    tradeLogger_.logEvent(order, ExecType::Replaced);

    // Update session map for the new ClOrdID
    {
        std::lock_guard<std::mutex> lock(sessionMapMutex_);
        orderSessionMap_[order.orderId] = sessionID;
    }

    std::cout << "[MockCQG] Order replaced: " << order.orderId << std::endl;
}

// ─────────────────────────────────────────────────────────────────────
// OrderStatusRequest (H)
// ─────────────────────────────────────────────────────────────────────

void MockCQGApplication::onMessage(const FIX42::OrderStatusRequest& message,
                                    const FIX::SessionID& sessionID) {
    FIX::ClOrdID clOrdId;
    message.get(clOrdId);

    std::cout << "[MockCQG] OrderStatusRequest: ClOrdID=" << clOrdId.getValue() << std::endl;

    auto orderOpt = orderManager_.findByClOrdId(clOrdId.getValue());
    if (!orderOpt) {
        std::cerr << "[MockCQG] OrderStatusRequest: Unknown ClOrdID "
                  << clOrdId.getValue() << std::endl;
        return;
    }

    // Send current status as an ExecutionReport
    sendExecReport(*orderOpt, orderOpt->status == OrdStatus::New ? ExecType::New :
                   orderOpt->status == OrdStatus::PartiallyFilled ? ExecType::PartialFill :
                   orderOpt->status == OrdStatus::Filled ? ExecType::Fill :
                   orderOpt->status == OrdStatus::Cancelled ? ExecType::Cancelled :
                   ExecType::New, FIX::ExecTransType_NEW, sessionID);
}

// ─────────────────────────────────────────────────────────────────────
// OrderMassStatusRequest (AF) — manual handler
// ─────────────────────────────────────────────────────────────────────

void MockCQGApplication::handleOrderMassStatusRequest(
    const FIX::Message& message, const FIX::SessionID& sessionID) {

    // Extract required fields
    std::string massStatusReqId;
    int massStatusReqType = 7; // Default: all orders

    if (message.isSetField(584)) { // MassStatusReqID
        massStatusReqId = message.getField(584);
    }
    if (message.isSetField(585)) { // MassStatusReqType
        massStatusReqType = FIX::IntConvertor::convert(message.getField(585));
    }

    std::cout << "[MockCQG] OrderMassStatusRequest: ReqID=" << massStatusReqId
              << " Type=" << massStatusReqType << std::endl;

    // Rate limiting
    std::string account;
    if (message.isSetField(FIX::FIELD::Account)) {
        account = message.getField(FIX::FIELD::Account);
    }
    if (!account.empty() && !checkMassStatusRateLimit(account)) {
        // Send Business Reject
        FIX::Message reject;
        reject.getHeader().setField(FIX::MsgType("j")); // BusinessMessageReject
        reject.getHeader().setField(FIX::BeginString("FIX.4.2"));
        reject.setField(FIX::RefMsgType("UAF"));
        reject.setField(FIX::Text("Mass status request rate limit exceeded (max "
            + std::to_string(MASS_STATUS_MAX_REQUESTS) + " per "
            + std::to_string(MASS_STATUS_WINDOW_SECONDS) + "s)"));
        reject.setField(FIX::IntField(380, 5)); // BusinessRejectReason: Other
        FIX::Session::sendToTarget(reject, sessionID);
        std::cerr << "[MockCQG] Mass status rate limit exceeded for account: "
                  << account << std::endl;
        return;
    }

    // Get matching orders based on MassStatusReqType
    std::vector<Order> orders;
    switch (massStatusReqType) {
        case MassStatusReqType::BySymbol: {
            std::string symbol;
            if (message.isSetField(FIX::FIELD::Symbol)) {
                symbol = message.getField(FIX::FIELD::Symbol);
            }
            orders = orderManager_.getOrdersBySymbol(symbol);
            break;
        }
        case MassStatusReqType::ByAccount: {
            orders = orderManager_.getOrdersByAccount(account);
            break;
        }
        case MassStatusReqType::AllOrders:
        default:
            orders = orderManager_.getActiveOrders();
            break;
    }

    int totalReports = static_cast<int>(orders.size());

    std::cout << "[MockCQG] MassStatus: " << totalReports
              << " orders match." << std::endl;

    // Send one ExecutionReport per matching order
    for (int i = 0; i < totalReports; ++i) {
        const Order& order = orders[i];

        // Determine the appropriate ExecType for the current status
        char execType;
        switch (order.status) {
            case OrdStatus::PartiallyFilled: execType = ExecType::PartialFill; break;
            case OrdStatus::Filled:          execType = ExecType::Fill; break;
            case OrdStatus::Cancelled:       execType = ExecType::Cancelled; break;
            case OrdStatus::Rejected:        execType = ExecType::Rejected; break;
            default:                         execType = ExecType::New; break;
        }

        FIX42::ExecutionReport report = buildExecReport(order, execType, FIX::ExecTransType_STATUS, sessionID);

        // Add mass status specific fields
        report.setField(FIX::StringField(584, massStatusReqId)); // MassStatusReqID
        report.setField(FIX::IntField(911, totalReports));       // TotNumReports

        // LastRptRequested on the final report
        if (i == totalReports - 1) {
            report.setField(FIX::BoolField(912, true));          // LastRptRequested
        }

        FIX::Session::sendToTarget(report, sessionID);
    }
    
    // Send UBR Ack
    sendOrderMassStatusAck(massStatusReqId, massStatusReqType, totalReports == 0 ? 2 : 0, sessionID);
}

// ─────────────────────────────────────────────────────────────────────
// ExecutionReport Builder
// ─────────────────────────────────────────────────────────────────────

FIX42::ExecutionReport MockCQGApplication::buildExecReport(
    const Order& order, char execType, char execTransType, const FIX::SessionID& /*sessionID*/) {

    FIX42::ExecutionReport report(
        FIX::OrderID(order.orderId),
        FIX::ExecID(IdGenerator::nextExecId()),
        FIX::ExecTransType(execTransType),
        FIX::ExecType(execType),
        FIX::OrdStatus(order.status),
        FIX::Symbol(order.symbol),
        FIX::Side(order.side),
        FIX::LeavesQty(order.leavesQty),
        FIX::CumQty(order.cumQty),
        FIX::AvgPx(order.avgPx)
    );

    report.set(FIX::ClOrdID(order.clOrdId));
    report.set(FIX::OrderQty(order.orderQty));

    report.setField(FIX::CharField(FIX::FIELD::TimeInForce, order.timeInForce));
    report.setField(FIX::CharField(FIX::FIELD::HandlInst, order.handlInst));

    if (!order.origClOrdId.empty()) {
        report.set(FIX::OrigClOrdID(order.origClOrdId));
    }

    if (order.lastShares > 0) {
        report.set(FIX::LastShares(order.lastShares));
        report.set(FIX::LastPx(order.lastPx));
    }

    if (order.price > 0) {
        report.set(FIX::Price(order.price));
    }

    if (!order.account.empty()) {
        report.set(FIX::Account(order.account));
    }

    if (!order.text.empty()) {
        report.set(FIX::Text(order.text));
    }

    // Add OrdRejReason for rejected orders
    if (execType == ExecType::Rejected) {
        report.set(FIX::OrdRejReason(FIX::OrdRejReason_OTHER));
    }

    report.set(FIX::TransactTime(FIX::UtcTimeStamp::now()));

    return report;
}

void MockCQGApplication::sendExecReport(const Order& order, char execType,
                                          char execTransType, const FIX::SessionID& sessionID) {
    FIX42::ExecutionReport report = buildExecReport(order, execType, execTransType, sessionID);
    FIX::Session::sendToTarget(report, sessionID);
}

// ─────────────────────────────────────────────────────────────────────
// OrderCancelReject Builder
// ─────────────────────────────────────────────────────────────────────

FIX42::OrderCancelReject MockCQGApplication::buildCancelReject(
    const std::string& clOrdId, const std::string& origClOrdId,
    const std::string& orderId, int cxlRejReason, const std::string& reason,
    const FIX::SessionID& /*sessionID*/) {

    FIX42::OrderCancelReject reject;
    reject.set(FIX::OrderID(orderId));
    reject.set(FIX::ClOrdID(clOrdId));
    reject.set(FIX::OrigClOrdID(origClOrdId));
    reject.set(FIX::OrdStatus(OrdStatus::Rejected));
    reject.set(FIX::CxlRejResponseTo(FIX::CxlRejResponseTo_ORDER_CANCEL_REQUEST));
    reject.set(FIX::CxlRejReason(cxlRejReason)); // Tag 102

    reject.set(FIX::Text(reason));
    reject.set(FIX::TransactTime(FIX::UtcTimeStamp::now()));

    return reject;
}

// ─────────────────────────────────────────────────────────────────────
// Mass Status Ack Builder
// ─────────────────────────────────────────────────────────────────────

void MockCQGApplication::sendOrderMassStatusAck(
    const std::string& reqId, int reqType, int result, const FIX::SessionID& sessionID) {
    
    FIX::Message ack;
    ack.getHeader().setField(FIX::MsgType("UBR"));
    ack.getHeader().setField(FIX::BeginString("FIX.4.2"));
    
    if (!reqId.empty()) {
        ack.setField(FIX::StringField(584, reqId));
    }
    ack.setField(FIX::IntField(585, reqType));
    ack.setField(FIX::IntField(20022, result));
    
    FIX::Session::sendToTarget(ack, sessionID);
}

// ─────────────────────────────────────────────────────────────────────
// Mass Status Rate Limiting
// ─────────────────────────────────────────────────────────────────────

bool MockCQGApplication::checkMassStatusRateLimit(const std::string& account) {
    std::lock_guard<std::mutex> lock(rateLimitMutex_);

    auto now = std::chrono::steady_clock::now();
    auto& timestamps = massStatusTimestamps_[account];

    // Remove timestamps outside the window
    auto windowStart = now - std::chrono::seconds(MASS_STATUS_WINDOW_SECONDS);
    while (!timestamps.empty() && timestamps.front() < windowStart) {
        timestamps.pop_front();
    }

    if (static_cast<int>(timestamps.size()) >= MASS_STATUS_MAX_REQUESTS) {
        return false; // Rate limit exceeded
    }

    timestamps.push_back(now);
    return true;
}

// ─────────────────────────────────────────────────────────────────────
// Fill Callback (called from MatchingEngine, possibly from another thread)
// ─────────────────────────────────────────────────────────────────────

void MockCQGApplication::onFill(const Order& order, char execType) {
    FIX::SessionID sessionID;
    {
        std::lock_guard<std::mutex> lock(sessionMapMutex_);
        auto it = orderSessionMap_.find(order.orderId);
        if (it == orderSessionMap_.end()) {
            std::cerr << "[MockCQG] onFill: No session for order "
                      << order.orderId << std::endl;
            return;
        }
        sessionID = it->second;
    }

    // Build and send the ExecutionReport
    FIX42::ExecutionReport report = buildExecReport(order, execType, FIX::ExecTransType_NEW, sessionID);
    FIX::Session::sendToTarget(report, sessionID);
}

} // namespace MockCQG

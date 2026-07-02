# Mock CQG FIX 4.2 Server

A robust, C++ based Mock FIX 4.2 Server specifically designed to emulate the [CQG FIX 4.2 API](https://help.cqg.com/api/fix/4.2/). 
This server acts as a drop-in replacement for the CQG test environment, allowing developers to test their CQG trading integrations locally without requiring an active CQG session or dealing with remote API rate limits.

## Key Features

- **FIX 4.2 Conformance**: Built on top of QuickFIX, handling all session-level logic automatically (Logon, Logout, Heartbeats, Sequence Resets).
- **CQG Specific Validation**:
  - Enforces mandatory `Account (1)` field on `NewOrderSingle` and `OrderCancelReplaceRequest`.
  - Rejects unrecognized or poorly formatted orders according to strict CQG specifications.
  - Returns appropriate `CxlRejReason` tags (`102`) specific to CQG (e.g. `0` for Too Late to Cancel, `1` for Unknown Order).
- **Stateful Order Management**: 
  - Simulates an active matching engine.
  - Handles `OrderCancelRequest` and `OrderCancelReplaceRequest` correctly, adjusting state and emitting appropriate `ExecutionReport` or `OrderCancelReject` messages.
  - Maintains the OrigClOrdID -> ClOrdID chain.
- **OrderMassStatusRequest**: Fully supports the `OrderMassStatusRequest` (`UAF`) message as per CQG documentation, returning an `OrderMassStatusReport` (`UBR`) followed by execution reports for all active orders, concluding with a final `UBR` indicating completion.
- **Live Pricing via Yahoo Finance**:
  - Automatically fetches real live prices for orders based on the requested instrument using the Yahoo Finance API.
  - Extracts the root commodity code from CQG's futures symbol format (e.g. `F.US.CLF27` -> `CL`).
  - Contains explicit symbol mappings for major futures indices (e.g., `NQ=F`, `ES=F`, `XIN9.FGI`).
  - Employs a deterministic fallback price for unmapped/failed symbols to ensure continuous operation.
  - Caches prices with a configurable TTL (default 60 seconds) to prevent API rate limits.

## Dependencies

- **C++17 Compiler** (MSVC, GCC, Clang)
- **CMake** 3.14+
- **libcurl**: For HTTP requests to Yahoo Finance. (Automatically fetched and built via CMake).
- **nlohmann_json**: For parsing JSON responses. (Automatically fetched).
- **QuickFIX**: The underlying FIX engine. (Automatically fetched and built).

## Building

This project uses CMake's `FetchContent` to manage all dependencies automatically.

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Running the Server

Start the server using the compiled executable. It will listen on `127.0.0.1:5001` (by default) for incoming FIX connections.

```bash
./Release/MockCQGServer.exe
```

## Testing

A python test script (`test_client.py`) is provided in the root directory. It acts as a mock client, sending a variety of test cases (positive and negative flows) to the server to verify conformance and behavior.

```bash
python test_client.py
```

### Supported Test Flows:
- **Negative Tests**: Missing Account, Invalid OrigClOrdID in Cancel Replace, Invalid OrigClOrdID in Cancel Request.
- **Positive Flows**: New Order Single, Cancel / Replace (reducing quantity), Order Cancel Request, Order Status Request.
- **Mass Status**: Order Mass Status Request (`UAF` -> `UBR` responses).

## License
MIT License

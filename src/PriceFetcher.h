#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <chrono>

namespace MockCQG {

struct CachedPrice {
    double price;
    std::chrono::steady_clock::time_point fetchTime;
};

class PriceFetcher {
public:
    explicit PriceFetcher(int cacheTtlSeconds = 60);
    ~PriceFetcher();

    // Get the price for a symbol. Tries cache first, then HTTP, then fallback.
    double getPrice(const std::string& symbol);

private:
    // Fetch from Yahoo Finance API
    double fetchFromApi(const std::string& symbol);

    // Deterministic fallback price based on symbol hash
    double fallbackPrice(const std::string& symbol) const;

    // libcurl write callback
    static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* output);

    // Strip CQG symbology (e.g., "F.US.CLF27" -> "CL")
    std::string normalizeSymbol(const std::string& symbol) const;

    std::unordered_map<std::string, CachedPrice> cache_;
    std::mutex mutex_;
    int cacheTtlSeconds_;
};

} // namespace MockCQG

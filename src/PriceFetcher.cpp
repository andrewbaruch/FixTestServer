#include "PriceFetcher.h"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <functional>

namespace MockCQG {

// ---------------------------------------------------------------------------
// Construction / Destruction
// ---------------------------------------------------------------------------

PriceFetcher::PriceFetcher(int cacheTtlSeconds)
    : cacheTtlSeconds_(cacheTtlSeconds)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

PriceFetcher::~PriceFetcher()
{
    curl_global_cleanup();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

double PriceFetcher::getPrice(const std::string& symbol)
{
    std::string normalized = normalizeSymbol(symbol);

    // 1. Check cache (short-lived lock)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(normalized);
        if (it != cache_.end()) {
            auto elapsed = std::chrono::steady_clock::now() - it->second.fetchTime;
            if (elapsed < std::chrono::seconds(cacheTtlSeconds_)) {
                return it->second.price;
            }
        }
    }

    // 2. Fetch from API (no lock held during network I/O)
    double price = fetchFromApi(normalized);

    // 3. Cache on success, fallback on failure
    if (price > 0.0) {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_[normalized] = CachedPrice{price, std::chrono::steady_clock::now()};
        return price;
    }

    return fallbackPrice(normalized);
}

// ---------------------------------------------------------------------------
// Yahoo Finance HTTP fetch
// ---------------------------------------------------------------------------

double PriceFetcher::fetchFromApi(const std::string& symbol)
{
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[PriceFetcher] curl_easy_init() failed\n";
        return 0.0;
    }

    std::string querySymbol = symbol;
    if (symbol == "NQ" || symbol == "ENQ") querySymbol = "NQ=F";
    else if (symbol == "ES" || symbol == "EP") querySymbol = "ES=F";
    else if (symbol == "YM") querySymbol = "YM=F";
    else if (symbol == "RTY") querySymbol = "RTY=F";
    else if (symbol == "CL" || symbol == "CLE") querySymbol = "CL=F";
    else if (symbol == "GC" || symbol == "GCE") querySymbol = "GC=F";
    else if (symbol == "CN") querySymbol = "XIN9.FGI";

    std::string url =
        "https://query1.finance.yahoo.com/v8/finance/chart/" + querySymbol +
        "?interval=1d&range=1d";

    std::string responseBody;

    try {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        // Yahoo requires a browser-like User-Agent
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        CURLcode res = curl_easy_perform(curl);

        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            std::cerr << "[PriceFetcher] curl error for " << symbol
                      << ": " << curl_easy_strerror(res) << "\n";
            curl_easy_cleanup(curl);
            return 0.0;
        }

        // Parse the JSON response
        auto json = nlohmann::json::parse(responseBody);
        
        if (json.contains("chart") && json["chart"].contains("result") && !json["chart"]["result"].is_null()) {
            auto& result = json["chart"]["result"];
            if (result.is_array() && !result.empty()) {
                auto& meta = result[0]["meta"];
                if (meta.contains("regularMarketPrice") && !meta["regularMarketPrice"].is_null()) {
                    double price = meta["regularMarketPrice"].get<double>();
                    curl_easy_cleanup(curl);
                    std::cerr << "[PriceFetcher] Fetched " << symbol << " = " << price << "\n";
                    return price;
                }
            }
        }

        std::cerr << "[PriceFetcher] Failed to find price in JSON for " << symbol << "\n";
        curl_easy_cleanup(curl);
        return 0.0;

    } catch (const std::exception& ex) {
        std::cerr << "[PriceFetcher] Exception fetching " << symbol
                  << ": " << ex.what() << "\n";
        curl_easy_cleanup(curl);
        return 0.0;
    }
}

// ---------------------------------------------------------------------------
// Deterministic fallback price
// ---------------------------------------------------------------------------

double PriceFetcher::fallbackPrice(const std::string& symbol) const
{
    std::hash<std::string> hasher;
    size_t h = hasher(symbol);

    // Map to a price in the range [10.00, 500.00)
    double price = 10.0 + static_cast<double>(h % 49000) / 100.0;

    std::cerr << "[PriceFetcher] WARNING: Using fallback price for "
              << symbol << " = " << price << "\n";
    return price;
}

// ---------------------------------------------------------------------------
// libcurl write callback
// ---------------------------------------------------------------------------

size_t PriceFetcher::writeCallback(void* contents, size_t size, size_t nmemb,
                                   std::string* output)
{
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// ---------------------------------------------------------------------------
// CQG symbol normalisation
// ---------------------------------------------------------------------------

std::string PriceFetcher::normalizeSymbol(const std::string& symbol) const
{
    // CQG futures format: "F.US.CLF27"  ->  commodity code "CL"
    //   Part layout:  F . <exchange> . <commodity><monthCode><yearDigits>
    if (symbol.size() >= 4 && symbol[0] == 'F' && symbol[1] == '.') {
        // Split by '.'
        std::istringstream ss(symbol);
        std::string token;
        std::vector<std::string> parts;
        while (std::getline(ss, token, '.')) {
            parts.push_back(token);
        }

        if (parts.size() >= 3) {
            // parts[2] is e.g. "CLF27" — strip the trailing month code (1 char)
            // and year digits (typically 2) to get the commodity root.
            const std::string& contract = parts[2];
            if (contract.size() > 3) {
                // Strip last 3 characters (month letter + 2-digit year)
                return contract.substr(0, contract.size() - 3);
            }
            // If the contract code is very short, return it as-is
            return contract;
        }
    }

    // Not a CQG futures symbol — return as-is
    return symbol;
}

} // namespace MockCQG

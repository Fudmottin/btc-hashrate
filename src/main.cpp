// main.cpp

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <array>
#include <chrono>
#include <cstdio>
#include <iomanip>
#include <boost/json.hpp>

namespace json = boost::json;

constexpr int blocks_per_day = 144;     // Average number of blocks mined per day on Bitcoin

// Runs a shell command and captures its output
std::string run_command(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) throw std::runtime_error("popen() failed");
    while (fgets(buffer.data(), buffer.size(), pipe)) {
        result += buffer.data();
    }
    pclose(pipe);
    return result;
}

// Parses a JSON string into a Boost.JSON object
json::object parse_json(const std::string& raw) {
    if (raw.empty() || raw[0] != '{') {
        throw std::runtime_error("Invalid or empty JSON input: " + raw);
    }
    return json::parse(raw).as_object();
}

// Gets the block hash for a given block height
std::string get_block_hash(int height) {
    std::ostringstream cmd;
    cmd << "bitcoin-cli getblockhash " << height;
    return run_command(cmd.str());
}

// Gets the block header as a JSON string for a given block hash
std::string get_block_header(const std::string& hash) {
    std::ostringstream cmd;
    cmd << "bitcoin-cli getblockheader " << hash;
    return run_command(cmd.str());
}

// Converts seconds to a natural d:h:m:s formatted string
std::string format_duration(int seconds) {
    int days = seconds / 86400;
    seconds %= 86400;
    int hours = seconds / 3600;
    seconds %= 3600;
    int minutes = seconds / 60;
    seconds %= 60;
    std::ostringstream oss;
    if (days > 0) oss << days << "d:";
    if (days > 0 || hours > 0) oss << hours << "h:";
    if (days > 0 || hours > 0 || minutes > 0) oss << minutes << "m:";
    oss << seconds << "s";
    return oss.str();
}

// Formats large numbers with commas
std::string format_number(double value) {
    std::ostringstream oss;
    oss.imbue(std::locale(""));
    oss << std::fixed << std::setprecision(1) << value;
    return oss.str();
}

// Converts hash rate into a human-readable string
std::string format_hashrate(double hps) {
    static const char* units[] = {"H/s", "kH/s", "MH/s", "GH/s", "TH/s", "PH/s", "EH/s"};
    int unit = 0;
    while (hps >= 1000.0 && unit < 6) {
        hps /= 1000.0;
        ++unit;
    }
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1) << hps << " " << units[unit];
    return oss.str();
}

// Converts a numerical value to type double
double to_double(const json::value& val) {
    switch (val.kind()) {
        case json::kind::int64:
            return static_cast<double>(val.as_int64());
        case json::kind::uint64:
            return static_cast<double>(val.as_uint64());
        case json::kind::double_:
            return val.as_double();
        default:
            throw std::runtime_error("Expected numeric JSON value");
    }
}

int main(int argc, char** argv) {
    double days = 1.0; // Default to 1 day if no argument is provided

    // Parse command line argument for number of days
    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [days]\n"
                  << "Hint: Run with no arguments for 1 day. Optionally pass number of days as a floating-point value.\n";
        return 1;
    }
    if (argc == 2) days = std::stod(argv[1]);

    // Get current blockchain info
    auto info = parse_json(run_command("bitcoin-cli getblockchaininfo"));
    int current_height = info["blocks"].as_int64();

    // Compute height of past block "days" ago (rounded down)
    int offset = std::floor(days * blocks_per_day);
    int past_height = current_height - offset;

    if (past_height < 0) {
        std::cerr << "Error: Requested history exceeds blockchain height.\n";
        return 1;
    }

    // Get current block header and extract mediantime
    auto head_hash = get_block_hash(current_height);
    auto head_header = parse_json(get_block_header(head_hash));
    int32_t head_time = head_header["mediantime"].as_int64();

    // Get past block header and extract mediantime
    auto past_hash = get_block_hash(past_height);
    auto past_header = parse_json(get_block_header(past_hash));
    int32_t past_time = past_header["mediantime"].as_int64();

    // Sum difficulty for all blocks in range
    double total_diff = 0.0;
    auto header = past_header;
    for (int i = past_height; i <= current_height; ++i) {
        // Get difficulty
        double difficulty = to_double(header["difficulty"]);
        total_diff += difficulty;

        // Get next header
        if (auto* val = header.if_contains("nextblockhash")) {
            auto hash = val->as_string().c_str();
            header = parse_json(get_block_header(hash));
        } else
            break;
    }

    int time_delta = head_time - past_time;
    int block_delta = offset;
    double avg_diff = total_diff / (current_height - past_height + 1);

    // Calculate estimated hash rate using average difficulty
    // Formula: hashrate = avg_difficulty * 2^32 / average_block_time
    double avg_block_time = static_cast<double>(time_delta) / block_delta;
    double hash_rate = avg_diff * std::pow(2.0, 32) / avg_block_time;

    // Output results
    std::cout << "Days: " << days << "\n"
              << "Blocks: " << block_delta << "\n"
              << "Expected Time: " << format_duration(static_cast<int>(days * 86400)) << "\n"
              << "Actual Time:   " << format_duration(time_delta) << "\n"
              << "Average Difficulty: " << format_number(avg_diff) << "\n"
              << "Estimated Hashrate: " << format_hashrate(hash_rate) << "\n";

    return 0;
}


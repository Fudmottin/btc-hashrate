// src/main.cpp

#include <boost/json.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "pool-names.hpp"

namespace json = boost::json;
using namespace boost::multiprecision;

namespace {

constexpr int kDefaultBlocks = 144;
constexpr int kTargetBlockIntervalSeconds = 600;
constexpr int kDifficultyAdjustmentInterval = 2016;
constexpr std::uint32_t kDifficulty1Bits = 0x1d00ffff;

struct OnlineStats {
   std::size_t count{0};
   double mean_value{0.0};
   double m2{0.0};

   void add(double x) {
      ++count;
      const double delta = x - mean_value;
      mean_value += delta / static_cast<double>(count);
      const double delta2 = x - mean_value;
      m2 += delta * delta2;
   }

   [[nodiscard]] double mean() const { return count == 0 ? 0.0 : mean_value; }

   [[nodiscard]] double sample_standard_deviation() const {
      if (count < 2) return 0.0;
      return std::sqrt(m2 / static_cast<double>(count - 1));
   }
};

struct BlockSelection {
   int first_height{};
   int last_height{};

   [[nodiscard]] int block_count() const {
      return last_height - first_height + 1;
   }
};

struct BlockSample {
   int height{};
   std::int64_t time{};
   uint256_t chainwork{};
   std::string hash;
   std::string bits;
   std::string miner;
};

std::string trim_ascii_whitespace(std::string s) {
   auto is_space = [](unsigned char ch) {
      switch (ch) {
      case ' ':
      case '\t':
      case '\n':
      case '\r':
      case '\f':
      case '\v':
         return true;
      default:
         return false;
      }
   };

   while (!s.empty() && is_space(static_cast<unsigned char>(s.front()))) {
      s.erase(s.begin());
   }
   while (!s.empty() && is_space(static_cast<unsigned char>(s.back()))) {
      s.pop_back();
   }
   return s;
}

std::string run_command(const std::string& cmd) {
   std::array<char, 256> buffer{};
   std::string result;

   FILE* pipe = popen(cmd.c_str(), "r");
   if (!pipe) throw std::runtime_error("popen() failed");

   while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
      result += buffer.data();
   }

   const int rc = pclose(pipe);
   if (rc != 0) {
      throw std::runtime_error("Command failed: " + cmd);
   }

   return result;
}

json::object parse_json_object(const std::string& raw) {
   const std::string trimmed = trim_ascii_whitespace(raw);
   if (trimmed.empty() || trimmed.front() != '{') {
      throw std::runtime_error("Invalid or empty JSON input");
   }
   return json::parse(trimmed).as_object();
}

int get_tip_height() {
   const json::object info =
      parse_json_object(run_command("bitcoin-cli getblockchaininfo"));

   auto* blocks = info.if_contains("blocks");
   if (!blocks || !blocks->is_int64()) {
      throw std::runtime_error("getblockchaininfo missing blocks");
   }

   return static_cast<int>(blocks->as_int64());
}

std::string get_block_hash(int height) {
   std::ostringstream cmd;
   cmd << "bitcoin-cli getblockhash " << height;
   return trim_ascii_whitespace(run_command(cmd.str()));
}

json::object get_block(const std::string& hash) {
   std::ostringstream cmd;
   cmd << "bitcoin-cli getblock " << hash << " 1";
   return parse_json_object(run_command(cmd.str()));
}

json::object get_raw_transaction(const std::string& txid) {
   std::ostringstream cmd;
   cmd << "bitcoin-cli getrawtransaction " << txid << " true";
   return parse_json_object(run_command(cmd.str()));
}

std::string format_duration(std::int64_t seconds) {
   if (seconds < 0) seconds = 0;

   const std::int64_t days = seconds / 86400;
   seconds %= 86400;
   const std::int64_t hours = seconds / 3600;
   seconds %= 3600;
   const std::int64_t minutes = seconds / 60;
   seconds %= 60;

   std::ostringstream oss;
   if (days > 0) oss << days << "d:";
   if (days > 0 || hours > 0) oss << hours << "h:";
   if (days > 0 || hours > 0 || minutes > 0) oss << minutes << "m:";
   oss << seconds << "s";
   return oss.str();
}

std::string format_datetime(std::int64_t unix_time) {
   const std::time_t t = static_cast<std::time_t>(unix_time);
   std::tm tm{};

   if (localtime_r(&t, &tm) == nullptr) {
      throw std::runtime_error("Failed to format datetime");
   }

   std::ostringstream oss;
   oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
   return oss.str();
}

std::string format_number(long double value) {
   std::ostringstream oss;
   oss.imbue(std::locale(""));
   oss << std::fixed << std::setprecision(1) << value;
   return oss.str();
}

std::string format_hashrate(double hps) {
   static constexpr const char* units[] = {"H/s",  "kH/s", "MH/s", "GH/s",
                                           "TH/s", "PH/s", "EH/s"};
   int unit = 0;
   while (hps >= 1000.0 && unit < 6) {
      hps /= 1000.0;
      ++unit;
   }

   std::ostringstream oss;
   oss << std::fixed << std::setprecision(1) << hps << " " << units[unit];
   return oss.str();
}

std::string format_minutes(std::optional<double> seconds) {
   if (!seconds.has_value()) return "n/a";

   std::ostringstream oss;
   oss << std::fixed << std::setprecision(2) << (*seconds / 60.0) << "m";
   return oss.str();
}

double median_time(std::vector<std::int32_t> intervals) {
   if (intervals.empty()) return 0.0;

   std::ranges::sort(intervals);
   const std::size_t n = intervals.size();

   if ((n % 2U) == 0U) {
      return (static_cast<double>(intervals[n / 2 - 1]) +
              static_cast<double>(intervals[n / 2])) /
             2.0;
   }

   return static_cast<double>(intervals[n / 2]);
}

int next_adjustment(int current_block_height) {
   const int blocks_into_epoch =
      current_block_height % kDifficultyAdjustmentInterval;
   return kDifficultyAdjustmentInterval - blocks_into_epoch;
}

std::uint32_t parse_bits(std::string_view hex) {
   std::uint32_t value{};
   const auto [ptr, ec] =
      std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);

   if (ec != std::errc{} || ptr != hex.data() + hex.size()) {
      throw std::runtime_error("Invalid bits field");
   }

   return value;
}

uint256_t parse_uint256_hex(std::string_view hex) {
   auto hex_value = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      return -1;
   };

   if (hex.empty()) {
      throw std::runtime_error("Invalid uint256 hex: empty");
   }

   uint256_t value{0};
   for (char c : hex) {
      const int nibble = hex_value(c);
      if (nibble < 0) {
         throw std::runtime_error("Invalid uint256 hex");
      }
      value <<= 4;
      value += static_cast<unsigned>(nibble);
   }

   return value;
}

uint256_t expand_compact_target(std::uint32_t compact) {
   const std::uint32_t exponent = (compact >> 24) & 0xff;
   const std::uint32_t coefficient = compact & 0x007fffff;

   uint256_t target{};
   if (exponent <= 3) {
      target = coefficient >> (8 * (3 - exponent));
   } else {
      target = uint256_t(coefficient) * (uint256_t(1) << (8 * (exponent - 3)));
   }
   return target;
}

int parse_positive_int(std::string_view text, const char* what) {
   if (text.empty()) {
      throw std::runtime_error(std::string("Missing ") + what);
   }

   int value{};
   const auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);

   if (ec != std::errc{} || ptr != text.data() + text.size() || value <= 0) {
      throw std::runtime_error(std::string("Invalid ") + what +
                               ": expected a positive integer");
   }

   return value;
}

BlockSelection parse_selection_arg(const char* arg_cstr, int tip_height) {
   const std::string_view arg{arg_cstr};

   if (const std::size_t dash = arg.find('-'); dash != std::string_view::npos) {
      if (arg.find('-', dash + 1) != std::string_view::npos) {
         throw std::runtime_error("Invalid range: too many '-' characters");
      }

      const int first_height =
         parse_positive_int(arg.substr(0, dash), "range start");
      const int last_height =
         parse_positive_int(arg.substr(dash + 1), "range end");

      if (first_height > last_height) {
         throw std::runtime_error("Invalid range: start must be <= end");
      }
      if (last_height > tip_height) {
         throw std::runtime_error("Invalid range: end exceeds current tip");
      }

      return BlockSelection{
         .first_height = first_height,
         .last_height = last_height,
      };
   }

   const int count = parse_positive_int(arg, "block count");
   if (count > tip_height + 1) {
      throw std::runtime_error("Requested history exceeds blockchain height");
   }

   return BlockSelection{
      .first_height = tip_height - count + 1,
      .last_height = tip_height,
   };
}

BlockSelection parse_block_selection(int argc, char** argv, int tip_height) {
   if (argc == 1) {
      return BlockSelection{
         .first_height = tip_height - kDefaultBlocks + 1,
         .last_height = tip_height,
      };
   }

   if (argc != 2) {
      throw std::runtime_error("Usage: btc-hashrate [blocks|start-end]");
   }

   return parse_selection_arg(argv[1], tip_height);
}

std::string hex_to_ascii_printable(std::string_view hex) {
   auto hex_value = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      return -1;
   };

   std::string out;
   out.reserve(hex.size() / 2);

   for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
      const int hi = hex_value(hex[i]);
      const int lo = hex_value(hex[i + 1]);
      if (hi < 0 || lo < 0) continue;

      const unsigned char byte = static_cast<unsigned char>((hi << 4) | lo);

      if (byte >= 32 && byte <= 126) {
         out.push_back(static_cast<char>(byte));
      } else {
         out.push_back(' ');
      }
   }

   return out;
}

std::string to_lower_ascii(std::string s) {
   for (char& ch : s) {
      if (ch >= 'A' && ch <= 'Z') {
         ch = static_cast<char>(ch - 'A' + 'a');
      }
   }
   return s;
}

std::string extract_coinbase_txid(const json::object& block) {
   auto* tx = block.if_contains("tx");
   if (!tx || !tx->is_array() || tx->as_array().empty()) {
      throw std::runtime_error("Block JSON missing tx array");
   }

   const auto& first = tx->as_array().front();
   if (!first.is_string()) {
      throw std::runtime_error("Expected verbosity-1 tx array of txids");
   }

   return std::string(first.as_string().c_str());
}

std::string extract_coinbase_hex(const json::object& tx) {
   auto* vin = tx.if_contains("vin");
   if (!vin || !vin->is_array() || vin->as_array().empty()) {
      throw std::runtime_error("Coinbase transaction missing vin array");
   }

   auto& vin0 = vin->as_array().front().as_object();
   auto* coinbase = vin0.if_contains("coinbase");
   if (!coinbase || !coinbase->is_string()) {
      throw std::runtime_error("Coinbase input missing coinbase scriptSig");
   }

   return std::string(coinbase->as_string().c_str());
}

std::string extract_coinbase_output_address(const json::object& tx) {
   auto* vout = tx.if_contains("vout");
   if (!vout || !vout->is_array() || vout->as_array().empty()) {
      throw std::runtime_error("Coinbase transaction missing vout array");
   }

   const auto& vout0 = vout->as_array().front().as_object();
   auto* script_pub_key = vout0.if_contains("scriptPubKey");
   if (!script_pub_key || !script_pub_key->is_object()) {
      throw std::runtime_error("Missing scriptPubKey");
   }

   auto& spk = script_pub_key->as_object();
   auto* addresses = spk.if_contains("addresses");

   if (addresses && addresses->is_array() && !addresses->as_array().empty()) {
      const auto& addr = addresses->as_array().front();
      if (addr.is_string()) {
         return std::string(addr.as_string().c_str());
      }
   }

   // descriptor-based nodes (modern)
   auto* address = spk.if_contains("address");
   if (address && address->is_string()) {
      return std::string(address->as_string().c_str());
   }

   return {};
}

std::string classify_miner_from_coinbase(const std::string& coinbase_hex,
                                         const std::string& payout_address) {
   // 1. Prefer payout address
   if (!payout_address.empty()) {
      for (std::size_t i = 0; i < kPayoutAddressAliasesCount; ++i) {
         const auto& alias = kPayoutAddressAliases[i];
         if (payout_address == alias.address) {
            return std::string(alias.display_name);
         }
      }
   }

   // 2. Fallback to scriptSig
   const std::string ascii =
      to_lower_ascii(hex_to_ascii_printable(coinbase_hex));

   for (std::size_t i = 0; i < kScriptSigAliasesCount; ++i) {
      const auto& alias = kScriptSigAliases[i];
      if (ascii.find(alias.needle) != std::string::npos) {
         return std::string(alias.display_name);
      }
   }

   std::cerr << "Unknown: " << coinbase_hex << "\n";

   return "Unknown";
}

BlockSample make_block_sample(const json::object& block) {
   auto* hash = block.if_contains("hash");
   auto* height = block.if_contains("height");
   auto* time = block.if_contains("time");
   auto* bits = block.if_contains("bits");
   auto* chainwork = block.if_contains("chainwork");

   if (!hash || !hash->is_string() || !height || !height->is_int64() || !time ||
       !time->is_int64() || !bits || !bits->is_string() || !chainwork ||
       !chainwork->is_string()) {
      throw std::runtime_error("Block JSON missing required fields");
   }

   const std::string coinbase_txid = extract_coinbase_txid(block);
   const json::object coinbase_tx = get_raw_transaction(coinbase_txid);
   const std::string coinbase_hex = extract_coinbase_hex(coinbase_tx);
   const std::string payout_address =
      extract_coinbase_output_address(coinbase_tx);

   return BlockSample{
      .height = static_cast<int>(height->as_int64()),
      .time = time->as_int64(),
      .chainwork = parse_uint256_hex(chainwork->as_string().c_str()),
      .hash = std::string(hash->as_string().c_str()),
      .bits = std::string(bits->as_string().c_str()),
      .miner = classify_miner_from_coinbase(coinbase_hex, payout_address),
   };
}

std::vector<BlockSample> load_blocks(const BlockSelection& selection) {
   std::vector<BlockSample> samples;
   samples.reserve(static_cast<std::size_t>(selection.block_count()));

   std::string hash = get_block_hash(selection.last_height);

   for (int height = selection.last_height; height >= selection.first_height;
        --height) {
      const json::object block = get_block(hash);
      samples.push_back(make_block_sample(block));

      if (height == selection.first_height) {
         break;
      }

      auto* prev = block.if_contains("previousblockhash");
      if (!prev || !prev->is_string()) {
         throw std::runtime_error("Encountered block without previousblockhash "
                                  "before range completed");
      }

      hash = std::string(prev->as_string().c_str());
   }

   std::ranges::reverse(samples);
   return samples;
}

std::string format_percent(double pct) {
   std::ostringstream oss;
   oss << std::fixed << std::setw(6) << std::setprecision(1) << pct << "%";
   return oss.str();
}

} // namespace

int main(int argc, char** argv) {
   try {
      const int tip_height = get_tip_height();
      const BlockSelection selection =
         parse_block_selection(argc, argv, tip_height);
      const std::vector<BlockSample> blocks = load_blocks(selection);

      if (blocks.empty()) {
         throw std::runtime_error("No blocks loaded");
      }

      uint256_t total_target{0};
      std::vector<std::int32_t> header_intervals;
      header_intervals.reserve(blocks.size() > 0 ? blocks.size() - 1 : 0);

      OnlineStats interval_stats;
      std::map<std::string, int> miner_counts;

      for (const BlockSample& block : blocks) {
         total_target += expand_compact_target(parse_bits(block.bits));
         ++miner_counts[block.miner];
      }

      for (std::size_t i = 1; i < blocks.size(); ++i) {
         const auto delta =
            static_cast<std::int32_t>(blocks[i].time - blocks[i - 1].time);
         header_intervals.push_back(delta);
         interval_stats.add(static_cast<double>(delta));
      }

      const int first_height = blocks.front().height;
      const int last_height = blocks.back().height;
      const std::int64_t time_delta = blocks.back().time - blocks.front().time;
      const std::size_t sampled_blocks = blocks.size();
      const std::size_t sampled_intervals = header_intervals.size();

      const uint256_t avg_target = total_target / sampled_blocks;
      const long double difficulty1_target =
         expand_compact_target(kDifficulty1Bits).convert_to<long double>();
      const long double avg_diff =
         difficulty1_target / avg_target.convert_to<long double>();

      std::optional<double> avg_block_time_seconds;
      std::optional<double> median_block_time_seconds;
      std::optional<double> stddev_block_time_seconds;
      std::optional<long double> hash_rate_hps;

      if (!header_intervals.empty()) {
         avg_block_time_seconds = interval_stats.mean();
         median_block_time_seconds = median_time(header_intervals);
         stddev_block_time_seconds = interval_stats.sample_standard_deviation();
      }

      if (time_delta > 0) {
         const uint256_t chainwork_delta =
            blocks.back().chainwork - blocks.front().chainwork;

         hash_rate_hps =
            chainwork_delta.convert_to<long double>() /
            static_cast<long double>(time_delta);
      }

      std::cout
         << "Block Range: " << first_height << " - " << last_height << "\n"
         << "Begin Time: " << format_datetime(blocks.front().time) << "\n"
         << "End Time:   " << format_datetime(blocks.back().time) << "\n"
         << "Sampled Blocks: " << sampled_blocks << "\n"
         << "Sampled Intervals: " << sampled_intervals << "\n"
         << "Next Diff Adjustment In: " << next_adjustment(last_height)
         << " Blocks\n"
         << "Expected Time: "
         << format_duration(static_cast<std::int64_t>(sampled_intervals) *
                            kTargetBlockIntervalSeconds)
         << "\n"
         << "Actual Time:   " << format_duration(time_delta) << "\n"
         << "Average Block Time: " << format_minutes(avg_block_time_seconds)
         << "\n"
         << "Median Block Time:  " << format_minutes(median_block_time_seconds)
         << "\n"
         << "Std Dev:            " << format_minutes(stddev_block_time_seconds)
         << "\n"
         << "Average Difficulty: " << format_number(avg_diff) << "\n";

      if (hash_rate_hps.has_value()) {
         std::cout << "Estimated Network Hashrate: "
                   << format_hashrate(static_cast<double>(*hash_rate_hps))
                   << "\n";
      } else {
         std::cout << "Estimated Hashrate: n/a\n";
      }

      std::vector<std::pair<std::string, int>> miner_rows(miner_counts.begin(),
                                                          miner_counts.end());

      std::ranges::sort(miner_rows, [](const auto& lhs, const auto& rhs) {
         if (lhs.second != rhs.second) return lhs.second > rhs.second;
         return lhs.first < rhs.first;
      });

      std::cout << "\nMiners / Pools\n";
      for (const auto& [miner, count] : miner_rows) {
         const double pct = (100.0 * static_cast<double>(count)) /
                            static_cast<double>(sampled_blocks);

         std::cout << "  " << std::left << std::setw(24) << miner << std::right
                   << " -- " << std::setw(5) << count << " -- "
                   << format_percent(pct) << "\n";
      }

      return 0;
   } catch (const std::exception& ex) {
      std::cerr << "Error: " << ex.what() << "\n";
      return 1;
   }
}


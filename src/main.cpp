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
#include <iomanip>
#include <iostream>
#include <optional>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

struct BlockHeaderSample {
   int height{};
   std::int64_t time{};
   std::string hash;
   std::string bits;
};

struct BlockSelection {
   int first_height{};
   int last_height{};

   [[nodiscard]] int block_count() const {
      return last_height - first_height + 1;
   }
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

json::object get_block_header(const std::string& hash) {
   std::ostringstream cmd;
   cmd << "bitcoin-cli getblockheader " << hash;
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

BlockHeaderSample make_block_header_sample(const json::object& header) {
   auto* hash = header.if_contains("hash");
   auto* height = header.if_contains("height");
   auto* time = header.if_contains("time");
   auto* bits = header.if_contains("bits");

   if (!hash || !hash->is_string() || !height || !height->is_int64() || !time ||
       !time->is_int64() || !bits || !bits->is_string()) {
      throw std::runtime_error("Block header JSON missing required fields");
   }

   return BlockHeaderSample{
      .height = static_cast<int>(height->as_int64()),
      .time = time->as_int64(),
      .hash = std::string(hash->as_string().c_str()),
      .bits = std::string(bits->as_string().c_str()),
   };
}

std::vector<BlockHeaderSample>
load_block_headers(const BlockSelection& selection) {
   std::vector<BlockHeaderSample> samples;
   samples.reserve(static_cast<std::size_t>(selection.block_count()));

   for (int height = selection.first_height; height <= selection.last_height;
        ++height) {
      const std::string hash = get_block_hash(height);
      const json::object header = get_block_header(hash);
      samples.push_back(make_block_header_sample(header));
   }

   return samples;
}

} // namespace

int main(int argc, char** argv) {
   try {
      const int tip_height = get_tip_height();
      const BlockSelection selection =
         parse_block_selection(argc, argv, tip_height);
      const std::vector<BlockHeaderSample> blocks =
         load_block_headers(selection);

      if (blocks.empty()) {
         throw std::runtime_error("No blocks loaded");
      }

      uint256_t total_target{0};
      std::vector<std::int32_t> header_intervals;
      header_intervals.reserve(blocks.size() > 0 ? blocks.size() - 1 : 0);

      OnlineStats interval_stats;

      for (const BlockHeaderSample& block : blocks) {
         total_target += expand_compact_target(parse_bits(block.bits));
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
      std::optional<double> hash_rate_hps;

      if (!header_intervals.empty()) {
         avg_block_time_seconds = interval_stats.mean();
         median_block_time_seconds = median_time(header_intervals);
         stddev_block_time_seconds = interval_stats.sample_standard_deviation();
         hash_rate_hps = static_cast<double>(avg_diff) * std::pow(2.0, 32) /
                         *avg_block_time_seconds;
      }

      std::cout
         << "Block Range: " << first_height << " - " << last_height << "\n"
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
         std::cout << "Estimated Hashrate: " << format_hashrate(*hash_rate_hps)
                   << "\n";
      } else {
         std::cout << "Estimated Hashrate: n/a\n";
      }

      return 0;
   } catch (const std::exception& ex) {
      std::cerr << "Error: " << ex.what() << "\n";
      return 1;
   }
}


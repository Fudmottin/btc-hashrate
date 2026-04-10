#ifndef POOL_NAMES_HPP
#define POOL_NAMES_HPP

#include <string_view>

struct MinerAlias {
   std::string_view display_name;
   std::string_view needle;
};

struct PayoutAlias {
   std::string_view address;
   std::string_view display_name;
};

extern const MinerAlias kScriptSigAliases[];
extern const std::size_t kScriptSigAliasesCount;

extern const PayoutAlias kPayoutAddressAliases[];
extern const std::size_t kPayoutAddressAliasesCount;

#endif


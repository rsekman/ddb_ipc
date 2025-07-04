#ifndef DDB_IPC_FMT_OPTIONAL_H
#define DDB_IPC_FMT_OPTIONAL_H

#include <fmt/core.h>

#include <optional>

// Generic formatter specialization for std::optional<T>
template <typename T>
struct fmt::formatter<std::optional<T>> {
    // The underlying formatter for T, to support format specs
    fmt::formatter<T> value_formatter;

    // Parse the format specifiers and forward them to the formatter<T>
    constexpr auto parse(format_parse_context& ctx) {
        return value_formatter.parse(ctx);
    }

    // Format the optional<T> by forwarding formatting to formatter<T>
    template <typename FormatContext>
    auto format(const std::optional<T>& opt, FormatContext& ctx) const {
        if (opt) {
            // If present, format the contained value with the inner formatter
            return value_formatter.format(*opt, ctx);
        } else {
            // If absent, print "NULL"
            return fmt::format_to(ctx.out(), "NULL");
        }
    }
};
#endif

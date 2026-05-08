#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace simpledb {

enum class ColumnType {
    Int64,
    Text,
};

struct Column {
    std::string name;
    ColumnType type;
};

using Value = std::variant<int64_t, std::string>;
using Row = std::vector<Value>;

std::string toUpper(std::string input);
std::string columnTypeToString(ColumnType type);
bool tryParseColumnType(const std::string& token, ColumnType& outType);
std::string valueToString(const Value& value);
bool isTypeCompatible(const Value& value, ColumnType type);

}  // namespace simpledb

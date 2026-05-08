#pragma once

#include "types.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace simpledb {

struct CreateTableStatement {
    std::string tableName;
    std::vector<Column> columns;
};

struct InsertStatement {
    std::string tableName;
    std::vector<Value> values;
};

struct CreateIndexStatement {
    std::string indexName;
    std::string tableName;
    std::string columnName;
};

struct WhereEquals {
    std::string columnName;
    Value value;
};

struct SelectStatement {
    std::string tableName;
    std::vector<std::string> columns;
    bool selectAll = false;
    std::optional<WhereEquals> where;
};

using Statement = std::variant<CreateTableStatement, InsertStatement, CreateIndexStatement, SelectStatement>;

bool tryParseStatement(const std::string& sql, Statement& outStatement, std::string& outError);

}  // namespace simpledb

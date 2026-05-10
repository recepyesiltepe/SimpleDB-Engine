#pragma once

#include "types.h"

#include <cstddef>
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

enum class ComparisonOperator {
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
};

struct WhereClause {
    std::string columnName;
    ComparisonOperator op = ComparisonOperator::Equal;
    Value value;
};

struct UpdateStatement {
    std::string tableName;
    std::string columnName;
    Value value;
    std::optional<WhereClause> where;
};

struct DeleteStatement {
    std::string tableName;
    std::optional<WhereClause> where;
};

struct InnerJoinClause {
    std::string tableName;
    std::string leftColumnName;
    std::string rightColumnName;
};

struct OrderByClause {
    std::string columnName;
    bool ascending = true;
};

struct SelectStatement {
    std::string tableName;
    std::vector<std::string> columns;
    bool selectAll = false;
    bool countAll = false;
    std::optional<InnerJoinClause> join;
    std::vector<WhereClause> whereClauses;
    std::optional<OrderByClause> orderBy;
    std::optional<std::size_t> limit;
};

using Statement = std::variant<CreateTableStatement, InsertStatement, CreateIndexStatement, SelectStatement, UpdateStatement, DeleteStatement>;

bool tryParseStatement(const std::string& sql, Statement& outStatement, std::string& outError);

}  // namespace simpledb

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
    std::vector<std::string> columns;
    std::vector<Value> values;
};

struct CreateIndexStatement {
    std::string indexName;
    std::string tableName;
    std::string columnName;
};

struct DropTableStatement {
    std::string tableName;
};

struct DropIndexStatement {
    std::string indexName;
};

struct DescribeTableStatement {
    std::string tableName;
};

struct AlterTableAddColumnStatement {
    std::string tableName;
    Column column;
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

enum class WhereExpressionKind {
    Predicate,
    And,
    Or,
    Not,
};

struct WhereExpression {
    WhereExpressionKind kind = WhereExpressionKind::Predicate;
    WhereClause predicate;
    std::vector<WhereExpression> children;
};

struct UpdateSetClause {
    std::string columnName;
    Value value;
};

struct UpdateStatement {
    std::string tableName;
    std::vector<UpdateSetClause> setClauses;
    std::optional<WhereExpression> whereExpression;
};

struct DeleteStatement {
    std::string tableName;
    std::optional<WhereExpression> whereExpression;
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
    std::optional<WhereExpression> whereExpression;
    std::optional<OrderByClause> orderBy;
    std::optional<std::size_t> limit;
};

using Statement = std::variant<CreateTableStatement, InsertStatement, CreateIndexStatement, DropTableStatement,
                               DropIndexStatement, DescribeTableStatement, AlterTableAddColumnStatement,
                               SelectStatement, UpdateStatement, DeleteStatement>;

bool tryParseStatement(const std::string& sql, Statement& outStatement, std::string& outError);

}  // namespace simpledb

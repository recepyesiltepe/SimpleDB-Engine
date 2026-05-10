#include "sql.h"

#include <cctype>
#include <sstream>

namespace simpledb {

namespace {

enum class TokenKind {
    Identifier,
    Number,
    String,
    Symbol,
    End,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
};

class Tokenizer {
public:
    explicit Tokenizer(const std::string& input)
        : input_(input) {}

    Token next() {
        skipWhitespace();
        if (pos_ >= input_.size()) {
            return {TokenKind::End, ""};
        }

        char c = input_[pos_];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::size_t start = pos_++;
            while (pos_ < input_.size()) {
                char ch = input_[pos_];
                if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '_') {
                    break;
                }
                ++pos_;
            }
            return {TokenKind::Identifier, input_.substr(start, pos_ - start)};
        }

        if (std::isdigit(static_cast<unsigned char>(c)) || c == '-') {
            std::size_t start = pos_++;
            while (pos_ < input_.size() && std::isdigit(static_cast<unsigned char>(input_[pos_]))) {
                ++pos_;
            }
            return {TokenKind::Number, input_.substr(start, pos_ - start)};
        }

        if (c == '\'') {
            ++pos_;
            std::string text;
            while (pos_ < input_.size()) {
                char ch = input_[pos_++];
                if (ch == '\'') {
                    if (pos_ < input_.size() && input_[pos_] == '\'') {
                        text.push_back('\'');
                        ++pos_;
                        continue;
                    }
                    return {TokenKind::String, text};
                }
                text.push_back(ch);
            }
            return {TokenKind::End, ""};
        }

        ++pos_;
        return {TokenKind::Symbol, std::string(1, c)};
    }

private:
    void skipWhitespace() {
        while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
            ++pos_;
        }
    }

    std::string input_;
    std::size_t pos_ = 0;
};

class Parser {
public:
    explicit Parser(const std::string& sql)
        : tokenizer_(sql) {
        consume();
    }

    bool parse(Statement& outStatement, std::string& outError) {
        if (matchKeyword("CREATE")) {
            consume();
            if (matchKeyword("TABLE")) {
                consume();
                CreateTableStatement statement;
                if (!parseIdentifier(statement.tableName, outError)) {
                    return false;
                }
                if (!expectSymbol("(", outError)) {
                    return false;
                }
                while (true) {
                    Column column;
                    if (!parseIdentifier(column.name, outError)) {
                        return false;
                    }

                    std::string typeToken;
                    if (!parseIdentifier(typeToken, outError)) {
                        return false;
                    }
                    if (!tryParseColumnType(typeToken, column.type)) {
                        outError = "Unknown column type: " + typeToken;
                        return false;
                    }
                    statement.columns.push_back(column);

                    if (matchSymbol(")")) {
                        consume();
                        break;
                    }
                    if (!expectSymbol(",", outError)) {
                        return false;
                    }
                }

                consumeSemicolonIfPresent();
                if (current_.kind != TokenKind::End) {
                    outError = "Unexpected trailing tokens";
                    return false;
                }
                outStatement = statement;
                return true;
            }

            if (matchKeyword("INDEX")) {
                consume();
                CreateIndexStatement statement;
                if (!parseIdentifier(statement.indexName, outError)) {
                    return false;
                }
                if (!expectKeyword("ON", outError)) {
                    return false;
                }
                if (!parseIdentifier(statement.tableName, outError)) {
                    return false;
                }
                if (!expectSymbol("(", outError)) {
                    return false;
                }
                if (!parseIdentifier(statement.columnName, outError)) {
                    return false;
                }
                if (!expectSymbol(")", outError)) {
                    return false;
                }
                consumeSemicolonIfPresent();
                if (current_.kind != TokenKind::End) {
                    outError = "Unexpected trailing tokens";
                    return false;
                }
                outStatement = statement;
                return true;
            }

            outError = "Expected TABLE or INDEX after CREATE";
            return false;
        }

        if (matchKeyword("INSERT")) {
            consume();
            if (!expectKeyword("INTO", outError)) {
                return false;
            }

            InsertStatement statement;
            if (!parseIdentifier(statement.tableName, outError)) {
                return false;
            }
            if (!expectKeyword("VALUES", outError)) {
                return false;
            }
            if (!expectSymbol("(", outError)) {
                return false;
            }
            while (true) {
                Value value;
                if (!parseValue(value, outError)) {
                    return false;
                }
                statement.values.push_back(value);
                if (matchSymbol(")")) {
                    consume();
                    break;
                }
                if (!expectSymbol(",", outError)) {
                    return false;
                }
            }
            consumeSemicolonIfPresent();
            if (current_.kind != TokenKind::End) {
                outError = "Unexpected trailing tokens";
                return false;
            }
            outStatement = statement;
            return true;
        }

        if (matchKeyword("SELECT")) {
            consume();
            SelectStatement statement;
            if (matchSymbol("*")) {
                statement.selectAll = true;
                consume();
            } else if (matchKeyword("COUNT")) {
                consume();
                if (!expectSymbol("(", outError)) {
                    return false;
                }
                if (!expectSymbol("*", outError)) {
                    return false;
                }
                if (!expectSymbol(")", outError)) {
                    return false;
                }
                statement.countAll = true;
            } else {
                while (true) {
                    std::string name;
                    if (!parseColumnToken(name, outError)) {
                        return false;
                    }
                    statement.columns.push_back(name);
                    if (!matchSymbol(",")) {
                        break;
                    }
                    consume();
                }
            }
            if (!expectKeyword("FROM", outError)) {
                return false;
            }
            if (!parseIdentifier(statement.tableName, outError)) {
                return false;
            }
            if (matchKeyword("INNER")) {
                consume();
                if (!expectKeyword("JOIN", outError)) {
                    return false;
                }
                InnerJoinClause join;
                if (!parseIdentifier(join.tableName, outError)) {
                    return false;
                }
                if (!expectKeyword("ON", outError)) {
                    return false;
                }
                if (!parseColumnToken(join.leftColumnName, outError)) {
                    return false;
                }
                if (!expectSymbol("=", outError)) {
                    return false;
                }
                if (!parseColumnToken(join.rightColumnName, outError)) {
                    return false;
                }
                statement.join = join;
            }
            if (matchKeyword("WHERE")) {
                consume();
                while (true) {
                    WhereClause where;
                    if (!parseColumnToken(where.columnName, outError)) {
                        return false;
                    }
                    if (!parseComparisonOperator(where.op, outError)) {
                        return false;
                    }
                    if (!parseValue(where.value, outError)) {
                        return false;
                    }
                    statement.whereClauses.push_back(where);
                    if (!matchKeyword("AND")) {
                        break;
                    }
                    consume();
                }
            }
            if (matchKeyword("ORDER")) {
                consume();
                if (!expectKeyword("BY", outError)) {
                    return false;
                }
                OrderByClause orderBy;
                if (!parseColumnToken(orderBy.columnName, outError)) {
                    return false;
                }
                if (matchKeyword("ASC")) {
                    consume();
                } else if (matchKeyword("DESC")) {
                    orderBy.ascending = false;
                    consume();
                }
                statement.orderBy = orderBy;
            }
            if (matchKeyword("LIMIT")) {
                consume();
                if (current_.kind != TokenKind::Number) {
                    outError = "Expected LIMIT number";
                    return false;
                }
                std::istringstream in(current_.text);
                int64_t parsed = -1;
                in >> parsed;
                if (in.fail() || parsed < 0) {
                    outError = "Invalid LIMIT value: " + current_.text;
                    return false;
                }
                statement.limit = static_cast<std::size_t>(parsed);
                consume();
            }
            consumeSemicolonIfPresent();
            if (current_.kind != TokenKind::End) {
                outError = "Unexpected trailing tokens";
                return false;
            }
            outStatement = statement;
            return true;
        }

        if (matchKeyword("UPDATE")) {
            consume();
            UpdateStatement statement;
            if (!parseIdentifier(statement.tableName, outError)) {
                return false;
            }
            if (!expectKeyword("SET", outError)) {
                return false;
            }
            if (!parseIdentifier(statement.columnName, outError)) {
                return false;
            }
            if (!expectSymbol("=", outError)) {
                return false;
            }
            if (!parseValue(statement.value, outError)) {
                return false;
            }
            if (matchKeyword("WHERE")) {
                consume();
                WhereClause where;
                if (!parseIdentifier(where.columnName, outError)) {
                    return false;
                }
                if (!parseComparisonOperator(where.op, outError)) {
                    return false;
                }
                if (!parseValue(where.value, outError)) {
                    return false;
                }
                statement.where = where;
            }
            consumeSemicolonIfPresent();
            if (current_.kind != TokenKind::End) {
                outError = "Unexpected trailing tokens";
                return false;
            }
            outStatement = statement;
            return true;
        }

        if (matchKeyword("DELETE")) {
            consume();
            if (!expectKeyword("FROM", outError)) {
                return false;
            }
            DeleteStatement statement;
            if (!parseIdentifier(statement.tableName, outError)) {
                return false;
            }
            if (matchKeyword("WHERE")) {
                consume();
                WhereClause where;
                if (!parseIdentifier(where.columnName, outError)) {
                    return false;
                }
                if (!parseComparisonOperator(where.op, outError)) {
                    return false;
                }
                if (!parseValue(where.value, outError)) {
                    return false;
                }
                statement.where = where;
            }
            consumeSemicolonIfPresent();
            if (current_.kind != TokenKind::End) {
                outError = "Unexpected trailing tokens";
                return false;
            }
            outStatement = statement;
            return true;
        }

        outError = "Unsupported statement";
        return false;
    }

private:
    void consume() {
        current_ = tokenizer_.next();
    }

    bool matchKeyword(const std::string& keyword) const {
        return current_.kind == TokenKind::Identifier && toUpper(current_.text) == keyword;
    }

    bool matchSymbol(const std::string& symbol) const {
        return current_.kind == TokenKind::Symbol && current_.text == symbol;
    }

    bool expectKeyword(const std::string& keyword, std::string& outError) {
        if (!matchKeyword(keyword)) {
            outError = "Expected keyword " + keyword;
            return false;
        }
        consume();
        return true;
    }

    bool expectSymbol(const std::string& symbol, std::string& outError) {
        if (!matchSymbol(symbol)) {
            outError = "Expected symbol " + symbol;
            return false;
        }
        consume();
        return true;
    }

    bool parseIdentifier(std::string& outIdentifier, std::string& outError) {
        if (current_.kind != TokenKind::Identifier) {
            outError = "Expected identifier";
            return false;
        }
        outIdentifier = current_.text;
        consume();
        return true;
    }

    bool parseColumnToken(std::string& outColumn, std::string& outError) {
        std::string left;
        if (!parseIdentifier(left, outError)) {
            return false;
        }
        if (matchSymbol(".")) {
            consume();
            std::string right;
            if (!parseIdentifier(right, outError)) {
                return false;
            }
            outColumn = left + "." + right;
            return true;
        }
        outColumn = left;
        return true;
    }

    bool parseValue(Value& outValue, std::string& outError) {
        if (current_.kind == TokenKind::Number) {
            std::istringstream in(current_.text);
            int64_t value = 0;
            in >> value;
            if (in.fail()) {
                outError = "Invalid number: " + current_.text;
                return false;
            }
            outValue = value;
            consume();
            return true;
        }
        if (current_.kind == TokenKind::String) {
            outValue = current_.text;
            consume();
            return true;
        }
        outError = "Expected value";
        return false;
    }

    bool parseComparisonOperator(ComparisonOperator& outOperator, std::string& outError) {
        if (matchSymbol("=")) {
            outOperator = ComparisonOperator::Equal;
            consume();
            return true;
        }
        if (matchSymbol("!")) {
            consume();
            if (!matchSymbol("=")) {
                outError = "Expected = after !";
                return false;
            }
            consume();
            outOperator = ComparisonOperator::NotEqual;
            return true;
        }
        if (matchSymbol("<")) {
            consume();
            if (matchSymbol("=")) {
                consume();
                outOperator = ComparisonOperator::LessEqual;
            } else {
                outOperator = ComparisonOperator::Less;
            }
            return true;
        }
        if (matchSymbol(">")) {
            consume();
            if (matchSymbol("=")) {
                consume();
                outOperator = ComparisonOperator::GreaterEqual;
            } else {
                outOperator = ComparisonOperator::Greater;
            }
            return true;
        }
        outError = "Expected comparison operator";
        return false;
    }

    void consumeSemicolonIfPresent() {
        if (matchSymbol(";")) {
            consume();
        }
    }

    Tokenizer tokenizer_;
    Token current_;
};

}  // namespace

bool tryParseStatement(const std::string& sql, Statement& outStatement, std::string& outError) {
    Parser parser(sql);
    return parser.parse(outStatement, outError);
}

}  // namespace simpledb

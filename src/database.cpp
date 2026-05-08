#include "database.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>

namespace simpledb {

namespace {

std::string trim(const std::string& text) {
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    std::size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(start, end - start);
}

std::filesystem::path catalogPath(const std::filesystem::path& dataDir) {
    return dataDir / "catalog.txt";
}

std::filesystem::path tablePath(const std::filesystem::path& dataDir, const std::string& tableName) {
    return dataDir / (tableName + ".rows");
}

bool readInt64(std::ifstream& in, int64_t& outValue) {
    in.read(reinterpret_cast<char*>(&outValue), sizeof(outValue));
    return in.good();
}

bool writeInt64(std::ofstream& out, int64_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return out.good();
}

bool readUInt32(std::ifstream& in, uint32_t& outValue) {
    in.read(reinterpret_cast<char*>(&outValue), sizeof(outValue));
    return in.good();
}

bool writeUInt32(std::ofstream& out, uint32_t value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    return out.good();
}

bool equalsValue(const Value& left, const Value& right) {
    if (left.index() != right.index()) {
        return false;
    }
    if (std::holds_alternative<int64_t>(left)) {
        return std::get<int64_t>(left) == std::get<int64_t>(right);
    }
    return std::get<std::string>(left) == std::get<std::string>(right);
}

bool parseQualifiedColumn(const std::string& text, std::string& outTable, std::string& outColumn) {
    std::size_t dot = text.find('.');
    if (dot == std::string::npos) {
        outTable.clear();
        outColumn = text;
        return !outColumn.empty();
    }
    if (dot == 0 || dot + 1 >= text.size() || text.find('.', dot + 1) != std::string::npos) {
        return false;
    }
    outTable = text.substr(0, dot);
    outColumn = text.substr(dot + 1);
    return !outTable.empty() && !outColumn.empty();
}

}  // namespace

std::string toUpper(std::string input) {
    std::transform(input.begin(), input.end(), input.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return input;
}

std::string columnTypeToString(ColumnType type) {
    if (type == ColumnType::Int64) {
        return "INT";
    }
    return "TEXT";
}

bool tryParseColumnType(const std::string& token, ColumnType& outType) {
    const std::string upper = toUpper(token);
    if (upper == "INT" || upper == "INTEGER" || upper == "BIGINT") {
        outType = ColumnType::Int64;
        return true;
    }
    if (upper == "TEXT" || upper == "STRING") {
        outType = ColumnType::Text;
        return true;
    }
    return false;
}

std::string valueToString(const Value& value) {
    if (std::holds_alternative<int64_t>(value)) {
        return std::to_string(std::get<int64_t>(value));
    }
    return std::get<std::string>(value);
}

bool isTypeCompatible(const Value& value, ColumnType type) {
    if (type == ColumnType::Int64) {
        return std::holds_alternative<int64_t>(value);
    }
    return std::holds_alternative<std::string>(value);
}

Database::Database(std::filesystem::path dataDir)
    : dataDir_(std::move(dataDir)) {}

bool Database::initialize(std::string& outError) {
    std::error_code ec;
    std::filesystem::create_directories(dataDir_, ec);
    if (ec) {
        outError = "Failed to create data directory: " + ec.message();
        return false;
    }
    return loadCatalog(outError);
}

bool Database::loadCatalog(std::string& outError) {
    tables_.clear();
    const auto path = catalogPath(dataDir_);
    if (!std::filesystem::exists(path)) {
        return true;
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        outError = "Failed to open catalog file";
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        std::istringstream iss(line);
        std::string kind;
        iss >> kind;
        if (kind == "TABLE") {
            std::string tableName;
            std::size_t columnCount = 0;
            iss >> tableName >> columnCount;
            if (tableName.empty() || columnCount == 0) {
                outError = "Malformed TABLE entry in catalog";
                return false;
            }
            TableData table;
            for (std::size_t i = 0; i < columnCount; ++i) {
                Column column;
                std::string typeText;
                iss >> column.name >> typeText;
                if (column.name.empty()) {
                    outError = "Malformed column entry in catalog";
                    return false;
                }
                if (!tryParseColumnType(typeText, column.type)) {
                    outError = "Unknown column type in catalog: " + typeText;
                    return false;
                }
                table.columns.push_back(column);
            }
            tables_[tableName] = std::move(table);
            continue;
        }

        if (kind == "INDEX") {
            std::string indexName;
            std::string tableName;
            std::string columnName;
            iss >> indexName >> tableName >> columnName;
            auto tableIt = tables_.find(tableName);
            if (tableIt == tables_.end()) {
                outError = "INDEX references unknown table: " + tableName;
                return false;
            }
            auto& table = tableIt->second;
            auto colIt = std::find_if(table.columns.begin(), table.columns.end(), [&](const Column& col) {
                return equalsIgnoreCase(col.name, columnName);
            });
            if (colIt == table.columns.end()) {
                outError = "INDEX references unknown column: " + columnName;
                return false;
            }
            IndexInfo idx;
            idx.name = indexName;
            idx.columnName = colIt->name;
            idx.columnIndex = static_cast<std::size_t>(colIt - table.columns.begin());
            if (colIt->type != ColumnType::Int64) {
                outError = "Only INT columns can be indexed";
                return false;
            }
            table.indexes.push_back(std::move(idx));
            continue;
        }

        outError = "Unknown catalog entry kind: " + kind;
        return false;
    }

    for (auto& item : tables_) {
        if (!loadTableRows(item.first, item.second, outError)) {
            return false;
        }
    }
    return true;
}

bool Database::persistCatalog(std::string& outError) const {
    std::ofstream out(catalogPath(dataDir_), std::ios::trunc);
    if (!out.is_open()) {
        outError = "Failed to write catalog";
        return false;
    }

    for (const auto& pair : tables_) {
        const auto& tableName = pair.first;
        const auto& table = pair.second;
        out << "TABLE " << tableName << " " << table.columns.size();
        for (const auto& column : table.columns) {
            out << " " << column.name << " " << columnTypeToString(column.type);
        }
        out << "\n";
    }

    for (const auto& pair : tables_) {
        const auto& tableName = pair.first;
        const auto& table = pair.second;
        for (const auto& index : table.indexes) {
            out << "INDEX " << index.name << " " << tableName << " " << index.columnName << "\n";
        }
    }

    if (!out.good()) {
        outError = "Failed while writing catalog";
        return false;
    }
    return true;
}

bool Database::loadTableRows(const std::string& tableName, TableData& table, std::string& outError) {
    table.rows.clear();
    for (auto& index : table.indexes) {
        index.tree.clear();
    }

    const auto path = tablePath(dataDir_, tableName);
    if (!std::filesystem::exists(path)) {
        return true;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        outError = "Failed to open table data file: " + tableName;
        return false;
    }

    while (true) {
        Row row;
        row.reserve(table.columns.size());
        for (const auto& column : table.columns) {
            if (column.type == ColumnType::Int64) {
                int64_t value = 0;
                if (!readInt64(in, value)) {
                    if (in.eof() && row.empty()) {
                        return true;
                    }
                    outError = "Corrupt table data file (int read failed): " + tableName;
                    return false;
                }
                row.push_back(value);
            } else {
                uint32_t length = 0;
                if (!readUInt32(in, length)) {
                    if (in.eof() && row.empty()) {
                        return true;
                    }
                    outError = "Corrupt table data file (length read failed): " + tableName;
                    return false;
                }
                std::string value(length, '\0');
                in.read(value.data(), static_cast<std::streamsize>(length));
                if (!in.good()) {
                    outError = "Corrupt table data file (string read failed): " + tableName;
                    return false;
                }
                row.push_back(value);
            }
        }
        const std::size_t rowId = table.rows.size();
        table.rows.push_back(row);
        for (auto& index : table.indexes) {
            index.tree.insert(std::get<int64_t>(row[index.columnIndex]), rowId);
        }
    }
}

bool Database::appendRowToDisk(const std::string& tableName, const TableData& table, const Row& row, std::string& outError) {
    std::ofstream out(tablePath(dataDir_, tableName), std::ios::binary | std::ios::app);
    if (!out.is_open()) {
        outError = "Failed to append table data file: " + tableName;
        return false;
    }

    for (std::size_t i = 0; i < table.columns.size(); ++i) {
        const auto& column = table.columns[i];
        if (column.type == ColumnType::Int64) {
            if (!writeInt64(out, std::get<int64_t>(row[i]))) {
                outError = "Failed to write INT value";
                return false;
            }
        } else {
            const auto& text = std::get<std::string>(row[i]);
            if (text.size() > static_cast<std::size_t>(UINT32_MAX)) {
                outError = "TEXT value too large";
                return false;
            }
            if (!writeUInt32(out, static_cast<uint32_t>(text.size()))) {
                outError = "Failed to write TEXT length";
                return false;
            }
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
            if (!out.good()) {
                outError = "Failed to write TEXT value";
                return false;
            }
        }
    }
    return true;
}

bool Database::rewriteTableRows(const std::string& tableName, const TableData& table, std::string& outError) {
    std::ofstream out(tablePath(dataDir_, tableName), std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        outError = "Failed to rewrite table data file: " + tableName;
        return false;
    }
    for (const auto& row : table.rows) {
        for (std::size_t i = 0; i < table.columns.size(); ++i) {
            const auto& column = table.columns[i];
            if (column.type == ColumnType::Int64) {
                if (!writeInt64(out, std::get<int64_t>(row[i]))) {
                    outError = "Failed to write INT value";
                    return false;
                }
            } else {
                const auto& text = std::get<std::string>(row[i]);
                if (text.size() > static_cast<std::size_t>(UINT32_MAX)) {
                    outError = "TEXT value too large";
                    return false;
                }
                if (!writeUInt32(out, static_cast<uint32_t>(text.size()))) {
                    outError = "Failed to write TEXT length";
                    return false;
                }
                out.write(text.data(), static_cast<std::streamsize>(text.size()));
                if (!out.good()) {
                    outError = "Failed to write TEXT value";
                    return false;
                }
            }
        }
    }
    return true;
}

void Database::rebuildIndexes(TableData& table) {
    for (auto& index : table.indexes) {
        index.tree.clear();
    }
    for (std::size_t rowId = 0; rowId < table.rows.size(); ++rowId) {
        for (auto& index : table.indexes) {
            index.tree.insert(std::get<int64_t>(table.rows[rowId][index.columnIndex]), rowId);
        }
    }
}

bool Database::execute(const Statement& statement, std::string& outMessage, std::optional<SelectResult>& outSelectResult) {
    outSelectResult.reset();

    if (std::holds_alternative<CreateTableStatement>(statement)) {
        return executeCreateTable(std::get<CreateTableStatement>(statement), outMessage);
    }
    if (std::holds_alternative<InsertStatement>(statement)) {
        return executeInsert(std::get<InsertStatement>(statement), outMessage);
    }
    if (std::holds_alternative<CreateIndexStatement>(statement)) {
        return executeCreateIndex(std::get<CreateIndexStatement>(statement), outMessage);
    }
    if (std::holds_alternative<UpdateStatement>(statement)) {
        return executeUpdate(std::get<UpdateStatement>(statement), outMessage);
    }
    if (std::holds_alternative<DeleteStatement>(statement)) {
        return executeDelete(std::get<DeleteStatement>(statement), outMessage);
    }
    return executeSelect(std::get<SelectStatement>(statement), outSelectResult, outMessage);
}

bool Database::executeCreateTable(const CreateTableStatement& statement, std::string& outMessage) {
    if (statement.columns.empty()) {
        outMessage = "CREATE TABLE requires at least one column";
        return false;
    }
    if (tables_.find(statement.tableName) != tables_.end()) {
        outMessage = "Table already exists: " + statement.tableName;
        return false;
    }

    TableData table;
    table.columns = statement.columns;
    tables_[statement.tableName] = std::move(table);

    std::string persistError;
    if (!persistCatalog(persistError)) {
        tables_.erase(statement.tableName);
        outMessage = persistError;
        return false;
    }

    outMessage = "Table created: " + statement.tableName;
    return true;
}

bool Database::executeInsert(const InsertStatement& statement, std::string& outMessage) {
    auto tableIt = tables_.find(statement.tableName);
    if (tableIt == tables_.end()) {
        outMessage = "Unknown table: " + statement.tableName;
        return false;
    }
    auto& table = tableIt->second;
    if (statement.values.size() != table.columns.size()) {
        outMessage = "Column count mismatch for table " + statement.tableName;
        return false;
    }

    Row row;
    row.reserve(table.columns.size());
    for (std::size_t i = 0; i < table.columns.size(); ++i) {
        if (!isTypeCompatible(statement.values[i], table.columns[i].type)) {
            outMessage = "Type mismatch at column " + table.columns[i].name;
            return false;
        }
        row.push_back(statement.values[i]);
    }

    std::string diskError;
    if (!appendRowToDisk(statement.tableName, table, row, diskError)) {
        outMessage = diskError;
        return false;
    }

    const std::size_t rowId = table.rows.size();
    table.rows.push_back(row);
    for (auto& index : table.indexes) {
        index.tree.insert(std::get<int64_t>(row[index.columnIndex]), rowId);
    }

    outMessage = "1 row inserted";
    return true;
}

bool Database::executeCreateIndex(const CreateIndexStatement& statement, std::string& outMessage) {
    auto tableIt = tables_.find(statement.tableName);
    if (tableIt == tables_.end()) {
        outMessage = "Unknown table: " + statement.tableName;
        return false;
    }
    auto& table = tableIt->second;

    auto colIt = std::find_if(table.columns.begin(), table.columns.end(), [&](const Column& col) {
        return equalsIgnoreCase(col.name, statement.columnName);
    });
    if (colIt == table.columns.end()) {
        outMessage = "Unknown column: " + statement.columnName;
        return false;
    }
    if (colIt->type != ColumnType::Int64) {
        outMessage = "Only INT columns are supported for indexes";
        return false;
    }

    for (const auto& index : table.indexes) {
        if (equalsIgnoreCase(index.name, statement.indexName)) {
            outMessage = "Index already exists: " + statement.indexName;
            return false;
        }
        if (equalsIgnoreCase(index.columnName, colIt->name)) {
            outMessage = "An index already exists for column: " + colIt->name;
            return false;
        }
    }

    IndexInfo idx;
    idx.name = statement.indexName;
    idx.columnName = colIt->name;
    idx.columnIndex = static_cast<std::size_t>(colIt - table.columns.begin());
    for (std::size_t rowId = 0; rowId < table.rows.size(); ++rowId) {
        idx.tree.insert(std::get<int64_t>(table.rows[rowId][idx.columnIndex]), rowId);
    }
    table.indexes.push_back(std::move(idx));

    std::string persistError;
    if (!persistCatalog(persistError)) {
        table.indexes.pop_back();
        outMessage = persistError;
        return false;
    }

    outMessage = "Index created: " + statement.indexName;
    return true;
}

bool Database::executeSelect(const SelectStatement& statement, std::optional<SelectResult>& outSelectResult, std::string& outMessage) {
    auto tableIt = tables_.find(statement.tableName);
    if (tableIt == tables_.end()) {
        outMessage = "Unknown table: " + statement.tableName;
        return false;
    }
    const auto& leftTable = tableIt->second;

    if (statement.join.has_value()) {
        const auto& join = statement.join.value();
        auto rightTableIt = tables_.find(join.tableName);
        if (rightTableIt == tables_.end()) {
            outMessage = "Unknown joined table: " + join.tableName;
            return false;
        }
        const auto& rightTable = rightTableIt->second;

        auto resolveJoinColumn = [&](const std::string& token, bool forLeft, std::size_t& outIndex) -> bool {
            std::string qualifier;
            std::string column;
            if (!parseQualifiedColumn(token, qualifier, column)) {
                outMessage = "Invalid column reference: " + token;
                return false;
            }

            auto findIn = [&](const TableData& table, const std::string& col, std::size_t& idx) -> bool {
                auto it = std::find_if(table.columns.begin(), table.columns.end(), [&](const Column& c) {
                    return equalsIgnoreCase(c.name, col);
                });
                if (it == table.columns.end()) {
                    return false;
                }
                idx = static_cast<std::size_t>(it - table.columns.begin());
                return true;
            };

            if (!qualifier.empty()) {
                if (forLeft && !equalsIgnoreCase(qualifier, statement.tableName)) {
                    outMessage = "JOIN left column must reference table " + statement.tableName;
                    return false;
                }
                if (!forLeft && !equalsIgnoreCase(qualifier, join.tableName)) {
                    outMessage = "JOIN right column must reference table " + join.tableName;
                    return false;
                }
            }
            if (forLeft) {
                if (!findIn(leftTable, column, outIndex)) {
                    outMessage = "Unknown JOIN column in left table: " + token;
                    return false;
                }
            } else {
                if (!findIn(rightTable, column, outIndex)) {
                    outMessage = "Unknown JOIN column in right table: " + token;
                    return false;
                }
            }
            return true;
        };

        std::size_t leftJoinIndex = 0;
        std::size_t rightJoinIndex = 0;
        if (!resolveJoinColumn(join.leftColumnName, true, leftJoinIndex)) {
            return false;
        }
        if (!resolveJoinColumn(join.rightColumnName, false, rightJoinIndex)) {
            return false;
        }

        struct ProjectionColumn {
            bool fromLeft = true;
            std::size_t index = 0;
            std::string outputName;
        };
        std::vector<ProjectionColumn> projection;
        if (statement.selectAll) {
            for (std::size_t i = 0; i < leftTable.columns.size(); ++i) {
                projection.push_back({true, i, statement.tableName + "." + leftTable.columns[i].name});
            }
            for (std::size_t i = 0; i < rightTable.columns.size(); ++i) {
                projection.push_back({false, i, join.tableName + "." + rightTable.columns[i].name});
            }
        } else {
            for (const auto& token : statement.columns) {
                std::string qualifier;
                std::string column;
                if (!parseQualifiedColumn(token, qualifier, column)) {
                    outMessage = "Invalid selected column: " + token;
                    return false;
                }

                auto findCol = [&](const TableData& table, const std::string& name, std::size_t& idx) -> bool {
                    auto it = std::find_if(table.columns.begin(), table.columns.end(), [&](const Column& c) {
                        return equalsIgnoreCase(c.name, name);
                    });
                    if (it == table.columns.end()) {
                        return false;
                    }
                    idx = static_cast<std::size_t>(it - table.columns.begin());
                    return true;
                };

                std::size_t idx = 0;
                if (!qualifier.empty()) {
                    if (equalsIgnoreCase(qualifier, statement.tableName) && findCol(leftTable, column, idx)) {
                        projection.push_back({true, idx, token});
                        continue;
                    }
                    if (equalsIgnoreCase(qualifier, join.tableName) && findCol(rightTable, column, idx)) {
                        projection.push_back({false, idx, token});
                        continue;
                    }
                    outMessage = "Unknown selected column: " + token;
                    return false;
                }

                bool foundLeft = findCol(leftTable, column, idx);
                if (foundLeft) {
                    projection.push_back({true, idx, token});
                    continue;
                }
                bool foundRight = findCol(rightTable, column, idx);
                if (foundRight) {
                    projection.push_back({false, idx, token});
                    continue;
                }
                outMessage = "Unknown selected column: " + token;
                return false;
            }
        }

        struct WhereBinding {
            bool fromLeft = true;
            std::size_t index = 0;
            Value value;
        };
        std::optional<WhereBinding> whereBinding;
        if (statement.where.has_value()) {
            std::string qualifier;
            std::string column;
            const auto& where = statement.where.value();
            if (!parseQualifiedColumn(where.columnName, qualifier, column)) {
                outMessage = "Invalid WHERE column: " + where.columnName;
                return false;
            }

            auto resolveWhere = [&](const TableData& table, const std::string& colName, std::size_t& idx) -> bool {
                auto it = std::find_if(table.columns.begin(), table.columns.end(), [&](const Column& c) {
                    return equalsIgnoreCase(c.name, colName);
                });
                if (it == table.columns.end()) {
                    return false;
                }
                idx = static_cast<std::size_t>(it - table.columns.begin());
                return true;
            };

            std::size_t idx = 0;
            if (!qualifier.empty()) {
                if (equalsIgnoreCase(qualifier, statement.tableName) && resolveWhere(leftTable, column, idx)) {
                    if (!isTypeCompatible(where.value, leftTable.columns[idx].type)) {
                        outMessage = "WHERE value type mismatch";
                        return false;
                    }
                    whereBinding = WhereBinding{true, idx, where.value};
                } else if (equalsIgnoreCase(qualifier, join.tableName) && resolveWhere(rightTable, column, idx)) {
                    if (!isTypeCompatible(where.value, rightTable.columns[idx].type)) {
                        outMessage = "WHERE value type mismatch";
                        return false;
                    }
                    whereBinding = WhereBinding{false, idx, where.value};
                } else {
                    outMessage = "Unknown WHERE column: " + where.columnName;
                    return false;
                }
            } else {
                bool foundLeft = resolveWhere(leftTable, column, idx);
                if (foundLeft) {
                    if (!isTypeCompatible(where.value, leftTable.columns[idx].type)) {
                        outMessage = "WHERE value type mismatch";
                        return false;
                    }
                    whereBinding = WhereBinding{true, idx, where.value};
                } else if (resolveWhere(rightTable, column, idx)) {
                    if (!isTypeCompatible(where.value, rightTable.columns[idx].type)) {
                        outMessage = "WHERE value type mismatch";
                        return false;
                    }
                    whereBinding = WhereBinding{false, idx, where.value};
                } else {
                    outMessage = "Unknown WHERE column: " + where.columnName;
                    return false;
                }
            }
        }

        SelectResult result;
        for (const auto& p : projection) {
            result.columnNames.push_back(p.outputName);
        }

        for (const auto& leftRow : leftTable.rows) {
            for (const auto& rightRow : rightTable.rows) {
                if (!equalsValue(leftRow[leftJoinIndex], rightRow[rightJoinIndex])) {
                    continue;
                }
                if (whereBinding.has_value()) {
                    const auto& binding = whereBinding.value();
                    const Value& current = binding.fromLeft ? leftRow[binding.index] : rightRow[binding.index];
                    if (!equalsValue(current, binding.value)) {
                        continue;
                    }
                }

                Row outRow;
                outRow.reserve(projection.size());
                for (const auto& p : projection) {
                    outRow.push_back(p.fromLeft ? leftRow[p.index] : rightRow[p.index]);
                }
                result.rows.push_back(std::move(outRow));
            }
        }

        outSelectResult = result;
        outMessage = std::to_string(result.rows.size()) + " row(s)";
        return true;
    }

    const auto& table = leftTable;

    std::vector<std::size_t> selectedColumnIndexes;
    if (statement.selectAll) {
        for (std::size_t i = 0; i < table.columns.size(); ++i) {
            selectedColumnIndexes.push_back(i);
        }
    } else {
        for (const auto& name : statement.columns) {
            auto colIt = std::find_if(table.columns.begin(), table.columns.end(), [&](const Column& col) {
                return equalsIgnoreCase(col.name, name);
            });
            if (colIt == table.columns.end()) {
                outMessage = "Unknown selected column: " + name;
                return false;
            }
            selectedColumnIndexes.push_back(static_cast<std::size_t>(colIt - table.columns.begin()));
        }
    }

    std::vector<std::size_t> candidateRows;
    candidateRows.reserve(table.rows.size());
    for (std::size_t rowId = 0; rowId < table.rows.size(); ++rowId) {
        candidateRows.push_back(rowId);
    }

    if (statement.where.has_value()) {
        const auto& where = statement.where.value();
        auto whereColIt = std::find_if(table.columns.begin(), table.columns.end(), [&](const Column& col) {
            return equalsIgnoreCase(col.name, where.columnName);
        });
        if (whereColIt == table.columns.end()) {
            outMessage = "Unknown WHERE column: " + where.columnName;
            return false;
        }
        std::size_t whereIndex = static_cast<std::size_t>(whereColIt - table.columns.begin());
        if (!isTypeCompatible(where.value, whereColIt->type)) {
            outMessage = "WHERE value type mismatch for column " + whereColIt->name;
            return false;
        }

        bool usedIndex = false;
        if (whereColIt->type == ColumnType::Int64) {
            for (const auto& index : table.indexes) {
                if (index.columnIndex == whereIndex) {
                    candidateRows = index.tree.searchEqual(std::get<int64_t>(where.value));
                    usedIndex = true;
                    break;
                }
            }
        }

        if (!usedIndex) {
            std::vector<std::size_t> filtered;
            filtered.reserve(candidateRows.size());
            for (std::size_t rowId : candidateRows) {
                if (equalsValue(table.rows[rowId][whereIndex], where.value)) {
                    filtered.push_back(rowId);
                }
            }
            candidateRows = std::move(filtered);
        }
    }

    SelectResult result;
    for (std::size_t idx : selectedColumnIndexes) {
        result.columnNames.push_back(table.columns[idx].name);
    }
    for (std::size_t rowId : candidateRows) {
        Row outRow;
        outRow.reserve(selectedColumnIndexes.size());
        for (std::size_t idx : selectedColumnIndexes) {
            outRow.push_back(table.rows[rowId][idx]);
        }
        result.rows.push_back(std::move(outRow));
    }

    outSelectResult = result;
    outMessage = std::to_string(result.rows.size()) + " row(s)";
    return true;
}

bool Database::executeUpdate(const UpdateStatement& statement, std::string& outMessage) {
    auto tableIt = tables_.find(statement.tableName);
    if (tableIt == tables_.end()) {
        outMessage = "Unknown table: " + statement.tableName;
        return false;
    }
    auto& table = tableIt->second;

    auto targetColIt = std::find_if(table.columns.begin(), table.columns.end(), [&](const Column& col) {
        return equalsIgnoreCase(col.name, statement.columnName);
    });
    if (targetColIt == table.columns.end()) {
        outMessage = "Unknown column: " + statement.columnName;
        return false;
    }
    std::size_t targetIndex = static_cast<std::size_t>(targetColIt - table.columns.begin());
    if (!isTypeCompatible(statement.value, targetColIt->type)) {
        outMessage = "Type mismatch for SET column " + targetColIt->name;
        return false;
    }

    std::optional<std::size_t> whereIndex;
    if (statement.where.has_value()) {
        const auto& where = statement.where.value();
        auto whereColIt = std::find_if(table.columns.begin(), table.columns.end(), [&](const Column& col) {
            return equalsIgnoreCase(col.name, where.columnName);
        });
        if (whereColIt == table.columns.end()) {
            outMessage = "Unknown WHERE column: " + where.columnName;
            return false;
        }
        if (!isTypeCompatible(where.value, whereColIt->type)) {
            outMessage = "WHERE value type mismatch for column " + whereColIt->name;
            return false;
        }
        whereIndex = static_cast<std::size_t>(whereColIt - table.columns.begin());
    }

    std::size_t updated = 0;
    for (auto& row : table.rows) {
        bool matches = true;
        if (whereIndex.has_value()) {
            matches = equalsValue(row[whereIndex.value()], statement.where.value().value);
        }
        if (!matches) {
            continue;
        }
        row[targetIndex] = statement.value;
        ++updated;
    }

    std::string diskError;
    if (!rewriteTableRows(statement.tableName, table, diskError)) {
        outMessage = diskError;
        return false;
    }
    rebuildIndexes(table);
    outMessage = std::to_string(updated) + " row(s) updated";
    return true;
}

bool Database::executeDelete(const DeleteStatement& statement, std::string& outMessage) {
    auto tableIt = tables_.find(statement.tableName);
    if (tableIt == tables_.end()) {
        outMessage = "Unknown table: " + statement.tableName;
        return false;
    }
    auto& table = tableIt->second;

    std::optional<std::size_t> whereIndex;
    if (statement.where.has_value()) {
        const auto& where = statement.where.value();
        auto whereColIt = std::find_if(table.columns.begin(), table.columns.end(), [&](const Column& col) {
            return equalsIgnoreCase(col.name, where.columnName);
        });
        if (whereColIt == table.columns.end()) {
            outMessage = "Unknown WHERE column: " + where.columnName;
            return false;
        }
        if (!isTypeCompatible(where.value, whereColIt->type)) {
            outMessage = "WHERE value type mismatch for column " + whereColIt->name;
            return false;
        }
        whereIndex = static_cast<std::size_t>(whereColIt - table.columns.begin());
    }

    std::vector<Row> keptRows;
    keptRows.reserve(table.rows.size());
    std::size_t deleted = 0;
    for (const auto& row : table.rows) {
        bool matches = true;
        if (whereIndex.has_value()) {
            matches = equalsValue(row[whereIndex.value()], statement.where.value().value);
        }
        if (matches) {
            ++deleted;
        } else {
            keptRows.push_back(row);
        }
    }
    table.rows = std::move(keptRows);

    std::string diskError;
    if (!rewriteTableRows(statement.tableName, table, diskError)) {
        outMessage = diskError;
        return false;
    }
    rebuildIndexes(table);
    outMessage = std::to_string(deleted) + " row(s) deleted";
    return true;
}

bool Database::equalsIgnoreCase(const std::string& left, const std::string& right) {
    return toUpper(left) == toUpper(right);
}

}  // namespace simpledb

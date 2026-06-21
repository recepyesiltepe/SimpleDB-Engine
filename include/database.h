#pragma once

#include "bplustree.h"
#include "sql.h"
#include "types.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace simpledb {

struct SelectResult {
    std::vector<std::string> columnNames;
    std::vector<Row> rows;
};

class Database {
public:
    explicit Database(std::filesystem::path dataDir);

    bool initialize(std::string& outError);
    bool execute(const Statement& statement, std::string& outMessage, std::optional<SelectResult>& outSelectResult);

private:
    struct IndexInfo {
        std::string name;
        std::string columnName;
        std::size_t columnIndex = 0;
        BPlusTree tree;
    };

    struct TableData {
        std::vector<Column> columns;
        std::vector<Row> rows;
        std::vector<IndexInfo> indexes;
    };

    std::filesystem::path dataDir_;
    std::unordered_map<std::string, TableData> tables_;

    bool loadCatalog(std::string& outError);
    bool persistCatalog(std::string& outError) const;
    bool loadTableRows(const std::string& tableName, TableData& table, std::string& outError);
    bool appendRowToDisk(const std::string& tableName, const TableData& table, const Row& row, std::string& outError);
    bool rewriteTableRows(const std::string& tableName, const TableData& table, std::string& outError);
    void rebuildIndexes(TableData& table);
    bool validateUniqueConstraints(const TableData& table, const Row& row, std::optional<std::size_t> ignoredRowId, std::string& outMessage) const;

    bool executeCreateTable(const CreateTableStatement& statement, std::string& outMessage);
    bool executeInsert(const InsertStatement& statement, std::string& outMessage);
    bool executeCreateIndex(const CreateIndexStatement& statement, std::string& outMessage);
    bool executeDropTable(const DropTableStatement& statement, std::string& outMessage);
    bool executeDropIndex(const DropIndexStatement& statement, std::string& outMessage);
    bool executeDescribeTable(const DescribeTableStatement& statement, std::optional<SelectResult>& outSelectResult, std::string& outMessage);
    bool executeAlterTableAddColumn(const AlterTableAddColumnStatement& statement, std::string& outMessage);
    bool executeSelect(const SelectStatement& statement, std::optional<SelectResult>& outSelectResult, std::string& outMessage);
    bool executeUpdate(const UpdateStatement& statement, std::string& outMessage);
    bool executeDelete(const DeleteStatement& statement, std::string& outMessage);

    static bool equalsIgnoreCase(const std::string& left, const std::string& right);
};

}  // namespace simpledb

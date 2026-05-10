#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace simpledb {

class BPlusTree {
public:
    explicit BPlusTree(std::size_t maxKeys = 8);

    void insert(int64_t key, std::size_t rowId);
    std::vector<std::size_t> searchEqual(int64_t key) const;
    std::vector<std::size_t> searchRange(std::optional<int64_t> minKey, bool includeMin,
                                         std::optional<int64_t> maxKey, bool includeMax) const;
    void clear();

private:
    struct Node {
        bool leaf = true;
        std::vector<int64_t> keys;
        std::vector<std::shared_ptr<Node>> children;
        std::vector<std::vector<std::size_t>> rowIds;
        std::shared_ptr<Node> nextLeaf;
    };

    std::shared_ptr<Node> root_;
    std::size_t maxKeys_;

    struct InsertResult {
        bool split = false;
        int64_t promotedKey = 0;
        std::shared_ptr<Node> rightNode;
    };

    InsertResult insertRecursive(const std::shared_ptr<Node>& node, int64_t key, std::size_t rowId);
    static std::size_t findChildIndex(const std::vector<int64_t>& keys, int64_t key);
};

}  // namespace simpledb

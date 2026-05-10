#include "bplustree.h"

#include <algorithm>

namespace simpledb {

BPlusTree::BPlusTree(std::size_t maxKeys)
    : root_(std::make_shared<Node>()),
      maxKeys_(std::max<std::size_t>(3, maxKeys)) {}

void BPlusTree::clear() {
    root_ = std::make_shared<Node>();
}

std::size_t BPlusTree::findChildIndex(const std::vector<int64_t>& keys, int64_t key) {
    return static_cast<std::size_t>(std::upper_bound(keys.begin(), keys.end(), key) - keys.begin());
}

void BPlusTree::insert(int64_t key, std::size_t rowId) {
    InsertResult result = insertRecursive(root_, key, rowId);
    if (!result.split) {
        return;
    }

    auto newRoot = std::make_shared<Node>();
    newRoot->leaf = false;
    newRoot->keys.push_back(result.promotedKey);
    newRoot->children.push_back(root_);
    newRoot->children.push_back(result.rightNode);
    root_ = newRoot;
}

BPlusTree::InsertResult BPlusTree::insertRecursive(const std::shared_ptr<Node>& node, int64_t key, std::size_t rowId) {
    if (node->leaf) {
        auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
        std::size_t pos = static_cast<std::size_t>(it - node->keys.begin());

        if (it != node->keys.end() && *it == key) {
            node->rowIds[pos].push_back(rowId);
        } else {
            node->keys.insert(it, key);
            node->rowIds.insert(node->rowIds.begin() + static_cast<std::ptrdiff_t>(pos), std::vector<std::size_t>{rowId});
        }

        if (node->keys.size() <= maxKeys_) {
            return {};
        }

        std::size_t middle = node->keys.size() / 2;
        auto right = std::make_shared<Node>();
        right->leaf = true;
        right->keys.assign(node->keys.begin() + static_cast<std::ptrdiff_t>(middle), node->keys.end());
        right->rowIds.assign(node->rowIds.begin() + static_cast<std::ptrdiff_t>(middle), node->rowIds.end());

        node->keys.erase(node->keys.begin() + static_cast<std::ptrdiff_t>(middle), node->keys.end());
        node->rowIds.erase(node->rowIds.begin() + static_cast<std::ptrdiff_t>(middle), node->rowIds.end());

        right->nextLeaf = node->nextLeaf;
        node->nextLeaf = right;

        InsertResult split;
        split.split = true;
        split.promotedKey = right->keys.front();
        split.rightNode = right;
        return split;
    }

    std::size_t childIndex = findChildIndex(node->keys, key);
    InsertResult childResult = insertRecursive(node->children[childIndex], key, rowId);
    if (!childResult.split) {
        return {};
    }

    node->keys.insert(node->keys.begin() + static_cast<std::ptrdiff_t>(childIndex), childResult.promotedKey);
    node->children.insert(node->children.begin() + static_cast<std::ptrdiff_t>(childIndex + 1), childResult.rightNode);

    if (node->keys.size() <= maxKeys_) {
        return {};
    }

    std::size_t middle = node->keys.size() / 2;
    int64_t promoted = node->keys[middle];

    auto right = std::make_shared<Node>();
    right->leaf = false;
    right->keys.assign(node->keys.begin() + static_cast<std::ptrdiff_t>(middle + 1), node->keys.end());
    right->children.assign(node->children.begin() + static_cast<std::ptrdiff_t>(middle + 1), node->children.end());

    node->keys.erase(node->keys.begin() + static_cast<std::ptrdiff_t>(middle), node->keys.end());
    node->children.erase(node->children.begin() + static_cast<std::ptrdiff_t>(middle + 1), node->children.end());

    InsertResult split;
    split.split = true;
    split.promotedKey = promoted;
    split.rightNode = right;
    return split;
}

std::vector<std::size_t> BPlusTree::searchEqual(int64_t key) const {
    std::shared_ptr<Node> node = root_;
    while (!node->leaf) {
        std::size_t childIndex = findChildIndex(node->keys, key);
        node = node->children[childIndex];
    }

    auto it = std::lower_bound(node->keys.begin(), node->keys.end(), key);
    if (it == node->keys.end() || *it != key) {
        return {};
    }

    std::size_t pos = static_cast<std::size_t>(it - node->keys.begin());
    return node->rowIds[pos];
}

std::vector<std::size_t> BPlusTree::searchRange(std::optional<int64_t> minKey, bool includeMin,
                                                std::optional<int64_t> maxKey, bool includeMax) const {
    std::vector<std::size_t> result;
    std::shared_ptr<Node> node = root_;

    if (minKey.has_value()) {
        while (!node->leaf) {
            std::size_t childIndex = findChildIndex(node->keys, minKey.value());
            node = node->children[childIndex];
        }
    } else {
        while (!node->leaf) {
            node = node->children.front();
        }
    }

    while (node) {
        for (std::size_t i = 0; i < node->keys.size(); ++i) {
            int64_t key = node->keys[i];

            if (minKey.has_value()) {
                if (includeMin) {
                    if (key < minKey.value()) {
                        continue;
                    }
                } else if (key <= minKey.value()) {
                    continue;
                }
            }

            if (maxKey.has_value()) {
                if (includeMax) {
                    if (key > maxKey.value()) {
                        return result;
                    }
                } else if (key >= maxKey.value()) {
                    return result;
                }
            }

            const auto& ids = node->rowIds[i];
            result.insert(result.end(), ids.begin(), ids.end());
        }
        node = node->nextLeaf;
    }

    return result;
}

}  // namespace simpledb

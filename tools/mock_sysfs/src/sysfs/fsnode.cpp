#include "fsnode.hpp"

#include <memory>
#include <unordered_map>

#include "mock_sysfs.hpp"
#include "protocol_type.hpp"

namespace mock_sysfs {

// Initialize the static inode counter. Root is 1.
ino_t FileSystemNode::inode_count_ = FUSE_ROOT_ID;

// Private parameterized constructor.
FileSystemNode::FileSystemNode(const std::string& name, NodeType type, mode_t mode, const FileSystemNode* parent,
                               ProtocolType protocol, const std::string& file_content)
    : name_(name),
      type_(type),
      ino_(inode_count_++),
      mode_(mode),
      file_content_(file_content),
      parent_(parent),
      protocol_(protocol) {}

// Factory method: Create a directory node.
std::unique_ptr<FileSystemNode> FileSystemNode::CreateDirectory(const std::string&    node_name,
                                                                const FileSystemNode* parent, ProtocolType protocol) {
  mode_t mode = S_IFDIR | kModeReadExecute;
  return std::unique_ptr<FileSystemNode>(
      new FileSystemNode(node_name, NodeType::DIRECTORY_NODE, mode, parent, protocol, ""));
}

// Factory method: Create a file node.
std::unique_ptr<FileSystemNode> FileSystemNode::CreateFile(const std::string& node_name,
                                                           const std::string& file_content, FileAccess access,
                                                           const FileSystemNode* parent, ProtocolType protocol) {
  mode_t mode = 0;
  // Set the mode based on the specified access mode.
  switch (access) {
    case FileAccess::READ_ONLY:
      mode = S_IFREG | kModeReadOnly;
      break;
    case FileAccess::READ_WRITE:
      mode = S_IFREG | kModeReadWrite;
      break;
    case FileAccess::WRITE_ONLY:
      mode = S_IFREG | kModeWriteOnly;
      break;
  }
  return std::unique_ptr<FileSystemNode>(
      new FileSystemNode(node_name, NodeType::FILE_NODE, mode, parent, protocol, file_content));
}

/*
 *  Caching DFS Tree Traversal. FileTree must NOT change after build.
 *
 * TODO(): If support for dynamic file tree modifications is added,
 * invalidate or update node_map accordingly. One option is to
 * call node_map.erase(ino) when a node is deleted or modified.
 */
FileSystemNode* FindNodeByIno(FileSystemNode* node, ino_t ino) {
  static std::unordered_map<ino_t, FileSystemNode*> node_map;

  if (!node) {
    return nullptr;
  }

  if (auto it = node_map.find(ino); it != node_map.end()) {
    return it->second;
  }

  if (node->GetIno() == ino) {
    node_map[ino] = node;
    return node;
  }

  if (node->GetType() == NodeType::DIRECTORY_NODE) {
    for (const auto& child : node->GetChildren()) {
      auto* node = FindNodeByIno(child.get(), ino);
      if (node) {
        node_map[ino] = node;
        return node;
      }
    }
  }

  return nullptr;
}

}  // namespace mock_sysfs

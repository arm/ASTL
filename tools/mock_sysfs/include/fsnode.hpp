
#ifndef INCLUDE_FSNODE_HPP_
#define INCLUDE_FSNODE_HPP_

#include <memory>
#include <string>
#include <vector>

#define FUSE_USE_VERSION 35  // NOLINT(cppcoreguidelines-macro-usage)
#include <fuse3/fuse_lowlevel.h>

#include "protocol_type.hpp"

namespace mock_sysfs {

class FileSystemNode;

enum class NodeType {
  FILE_NODE,
  DIRECTORY_NODE,
};

enum class FileAccess {
  READ_ONLY,
  READ_WRITE,
  WRITE_ONLY,
};

/**
 * @brief Represents a node in a filesystem.
 *
 * This class models a file system node that may be either a file or a directory.
 * Use the static factory methods CreateDirectory() and CreateFile() to create nodes.
 * For directories, child nodes can be added with AddChild() and retrieved via GetChildren().
 * Must pass a unique pointer to the parent node when constructing a new node if it belongs in a tree.
 */
class FileSystemNode {
 public:
  /**
   * @brief Creates a directory node.
   *
   * Factory method to create a directory node.
   *
   * @param node_name Name of the directory.
   * @param parent Optional pointer to the parent node.
   * @param protocol Optional protocol associated with the node; defaults to ProtocolType::NONE.
   * @return std::unique_ptr<FileSystemNode> A unique pointer to the newly created directory node.
   */
  static std::unique_ptr<FileSystemNode> CreateDirectory(const std::string&    node_name,
                                                         const FileSystemNode* parent   = nullptr,
                                                         ProtocolType          protocol = ProtocolType::NONE);

  /**
   * @brief Creates a file node.
   *
   * Factory method to create a file node.
   *
   * @param node_name Name of the file.
   * @param file_content Initial content of the file.
   * @param access File access permission; defaults to FileAccess::READ_WRITE.
   * @param parent Optional pointer to the parent node.
   * @param protocol Optional protocol associated with the node; defaults to ProtocolType::NONE.
   * @return std::unique_ptr<FileSystemNode> A unique pointer to the newly created file node.
   */
  static std::unique_ptr<FileSystemNode> CreateFile(const std::string& node_name, const std::string& file_content,
                                                    FileAccess            access   = FileAccess::READ_WRITE,
                                                    const FileSystemNode* parent   = nullptr,
                                                    ProtocolType          protocol = ProtocolType::NONE);
  /**
   * @brief Gets the name of the node.
   *
   * @return const std::string& The name of the node.
   */
  const std::string& GetName() const { return name_; }

  /**
   * @brief Gets the type of the node.
   *
   * @return NodeType The type of the node (e.g., file or directory).
   */
  NodeType GetType() const { return type_; }

  /**
   * @brief Gets the inode number of the node.
   *
   * @return ino_t The inode number.
   */
  ino_t GetIno() const { return ino_; }

  /**
   * @brief Gets the file mode (permissions) of the node.
   *
   * @return mode_t The file mode.
   */
  mode_t GetMode() const { return mode_; }

  /**
   * @brief Gets the file content.
   *
   * @return std::string& A modifiable reference to the node's file content.
   */
  std::string& GetFileContent() { return file_content_; }

  /**
   * @brief Gets the child nodes of this node.
   *
   * @return const std::vector<std::unique_ptr<FileSystemNode>>& A vector of unique pointers to the child nodes.
   */
  const std::vector<std::unique_ptr<FileSystemNode>>& GetChildren() const { return children_; }

  /**
   * @brief Gets the parent node.
   *
   * @return const FileSystemNode* A pointer to the parent node, or nullptr if this node is the root.
   */
  const FileSystemNode* GetParent() const { return parent_; }

  /**
   * @brief Gets the protocol associated with the node.
   *
   * @return ProtocolType The protocol type of the node.
   */
  ProtocolType GetProtocol() const { return protocol_; }

  /**
   * @brief Sets the file content.
   *
   * Replaces the existing file content with the provided string.
   *
   * @param content The new content for the file.
   */
  void SetFileContent(const std::string& content) { file_content_ = content; }

  /**
   * @brief Adds a child node to this node.
   *
   * Appends the provided child node to the list of children. The ownership of the child is transferred.
   *
   * @param child A unique pointer to the child FileSystemNode.
   */
  void AddChild(std::unique_ptr<FileSystemNode> child) { children_.emplace_back(std::move(child)); }

  FileSystemNode() = delete;

 private:
  std::string name_;  // The name of the node
  NodeType    type_;  // File or Directory
  ino_t       ino_;   // Unique index node
  mode_t      mode_;  // Permissions (e.g. File RWXRWXRWX)

  std::string file_content_;  // Content of the file. Not applicable for directories.

  std::vector<std::unique_ptr<FileSystemNode>> children_;  // Child nodes. Parent owns child pointers.

  const FileSystemNode* parent_;  // Pointer to the parent node.

  ProtocolType protocol_;  // SCMI Protocol associated with the node.

  static ino_t inode_count_;  // Counter used to assign unique inode numbers.

  /**
   * @brief Constructs a FileSystemNode.
   *
   * The constructor is private. Instances of FileSystemNode should be created via
   * the CreateDirectory and CreateFile factory methods.
   *
   * @param name The name of the node.
   * @param type The type of the node (file or directory).
   * @param mode The file mode (permissions) for the node.
   * @param parent Pointer to the parent node.
   * @param protocol The protocol associated with the node.
   * @param file_content The initial content for the node; defaults to an empty string.
   */
  FileSystemNode(const std::string& name, NodeType type, mode_t mode, const FileSystemNode* parent,
                 ProtocolType protocol, const std::string& file_content = "");
};

FileSystemNode* FindNodeByIno(FileSystemNode* node, ino_t ino);

}  // namespace mock_sysfs

#endif  // INCLUDE_FSNODE_HPP_

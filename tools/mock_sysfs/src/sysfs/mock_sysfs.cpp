#include "mock_sysfs.hpp"

#include <algorithm>
#include <iostream>

#include "common.hpp"
#include "fsnode.hpp"
#include "protocol.hpp"

namespace mock_sysfs {

static void AddDirectoryBuffer(fuse_req_t req, std::vector<char>& buf, const char* name, ino_t ino) {
  struct stat stbuf {};
  stbuf.st_ino = ino;

  off_t  old_size   = static_cast<off_t>(buf.size());
  size_t entry_size = fuse_add_direntry(req, nullptr, 0, name, nullptr, 0);
  buf.resize(old_size + entry_size);

  fuse_add_direntry(req, std::next(buf.data(), old_size), entry_size, name, &stbuf, static_cast<off_t>(buf.size()));
}

static void LowLevelInit(void* user_data, struct fuse_conn_info* conn) {
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(user_data);
  (void)conn;

  std::cout << "Initializing mock filesystem..." << std::endl;
  // Create the absolute root directory.
  fuse_user_data->root = FileSystemNode::CreateDirectory("/");

  InitProtocol(fuse_user_data->root.get());
}

static void LowLevelDestroy(void* user_data) { (void)user_data; }

static void LowLevelLookup(fuse_req_t req, fuse_ino_t parent, const char* name) {
  std::cout << "lookup: parent=" << parent << ", name=" << name << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(req));

  FileSystemNode* parent_node = FindNodeByIno(fuse_user_data->root.get(), parent);
  if (!parent_node) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  for (const auto& child : parent_node->GetChildren()) {
    if (child->GetName() == name) {
      struct fuse_entry_param entry {};
      entry.ino           = child->GetIno();
      entry.attr_timeout  = kAttrTimeoutSec;
      entry.entry_timeout = kEntryTimeoutSec;
      entry.attr.st_ino   = child->GetIno();
      entry.attr.st_mode  = child->GetMode();

      if (child->GetType() == NodeType::DIRECTORY_NODE) {
        entry.attr.st_nlink = 2;  // Directory has at least 2 links (. and ..) define constants
      } else {
        std::string value = HandleProtocolRead(child.get());
        if (!value.empty()) {
          child->SetFileContent(value);
        }
        entry.attr.st_nlink = 1;
        entry.attr.st_size  = static_cast<off_t>(child->GetFileContent().size());
      }

      fuse_reply_entry(req, &entry);
      return;
    }
  }

  fuse_reply_err(req, ENOENT);
}

static void LowLevelGetAttr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* file_info) {
  (void)file_info;
  std::cout << "getattr: ino=" << ino << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(req));

  FileSystemNode* node = FindNodeByIno(fuse_user_data->root.get(), ino);
  if (!node) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  struct stat stbuf {};
  stbuf.st_ino  = node->GetIno();
  stbuf.st_mode = node->GetMode();

  if (node->GetType() == NodeType::DIRECTORY_NODE) {
    stbuf.st_nlink = kDirectoryLinkCount;  // Directory has at least 2 links (. and ..)
    stbuf.st_size  = kDefaultDirSize;
  } else {
    std::string value = HandleProtocolRead(node);
    if (!value.empty()) {
      node->SetFileContent(value);
    }
    stbuf.st_nlink = kFileLinkCount;
    stbuf.st_size  = static_cast<off_t>(node->GetFileContent().size());
  }

  stbuf.st_atime = time(nullptr);
  stbuf.st_mtime = time(nullptr);
  stbuf.st_ctime = time(nullptr);

  fuse_reply_attr(req, &stbuf, 1.0);
}

static void LowLevelOpen(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* file_info) {
  std::cout << "open: ino=" << ino << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(req));

  FileSystemNode* node = FindNodeByIno(fuse_user_data->root.get(), ino);
  if (!node) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  if (node->GetType() == NodeType::DIRECTORY_NODE) {
    fuse_reply_err(req, EISDIR);
    return;
  }

  fuse_reply_open(req, file_info);
}

static int ReplyBufferLimited(fuse_req_t req, const char* buf, size_t bufsize, off_t off, size_t maxsize) {
  if (off >= 0 && static_cast<size_t>(off) < bufsize) {
    return fuse_reply_buf(req, std::next(buf, off), std::min(bufsize - static_cast<size_t>(off), maxsize));
  }

  return fuse_reply_buf(req, nullptr, 0);
}

static void LowLevelRead(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info* file_info) {
  (void)file_info;
  std::cout << "read: ino=" << ino << ", size=" << size << ", off=" << off << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(req));

  FileSystemNode* node = FindNodeByIno(fuse_user_data->root.get(), ino);
  if (!node) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  if (node->GetType() == NodeType::DIRECTORY_NODE) {
    fuse_reply_err(req, EISDIR);
    return;
  }

  std::cout << "value read: " << node->GetFileContent() << "\n";

  ReplyBufferLimited(req, node->GetFileContent().c_str(), node->GetFileContent().size(), off, size);
}

static void LowLevelWrite(fuse_req_t req, fuse_ino_t ino, const char* buf, size_t size, off_t off,
                          struct fuse_file_info* file_info) {
  (void)file_info;
  std::cout << "write: ino=" << ino << ", size=" << size << ", off=" << off << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(req));

  FileSystemNode* node = FindNodeByIno(fuse_user_data->root.get(), ino);

  if (!node) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  if (node->GetType() == NodeType::DIRECTORY_NODE) {
    fuse_reply_err(req, EISDIR);
    return;
  }

  if (HandleProtocolWrite(node, std::string(buf, size)) != ErrorCode::SUCCESS) {
    fuse_reply_err(req, EIO);
  }

  if (off + size > kMaxContentLen - 1) {
    fuse_reply_err(req, EFBIG);
    return;
  }

  // Resize the content if needed
  if (off + size > node->GetFileContent().size()) {
    node->GetFileContent().resize(off + size);
  }

  std::copy(buf, std::next(buf, static_cast<off_t>(size)), node->GetFileContent().begin() + off);

  fuse_reply_write(req, size);
}

static void LowLevelOpenDir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* file_info) {
  std::cout << "opendir: ino=" << ino << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(req));

  FileSystemNode* node = FindNodeByIno(fuse_user_data->root.get(), ino);
  if (!node) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  if (node->GetType() != NodeType::DIRECTORY_NODE) {
    fuse_reply_err(req, ENOTDIR);
    return;
  }

  fuse_reply_open(req, file_info);
}

static void LowLevelReadDir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info* file_info) {
  (void)file_info;
  std::cout << "readdir: ino=" << ino << ", size=" << size << ", off=" << off << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(req));

  FileSystemNode* dir = FindNodeByIno(fuse_user_data->root.get(), ino);
  if (!dir || dir->GetType() != NodeType::DIRECTORY_NODE) {
    fuse_reply_err(req, ENOTDIR);
    return;
  }

  std::vector<char> buf;

  // Add standard entries
  AddDirectoryBuffer(req, buf, ".", dir->GetIno());
  ino_t parent_ino = (dir->GetParent() ? dir->GetParent()->GetIno() : dir->GetIno());  // Check if root
  AddDirectoryBuffer(req, buf, "..", parent_ino);

  // Add children
  for (const auto& child : dir->GetChildren()) {
    AddDirectoryBuffer(req, buf, child->GetName().c_str(), child->GetIno());
  }

  // The 'off' parameter indicates how many bytes have already been read by the client.
  // This function will only return the remaining data starting at 'off', up to 'size' bytes.
  // This allows clients (like ls) to paginate the results if the entire directory listing is large.
  ReplyBufferLimited(req, buf.data(), buf.size(), off, size);
}

const struct fuse_lowlevel_ops kFuseLowLevelOps = {
    .init        = LowLevelInit,
    .destroy     = LowLevelDestroy,
    .lookup      = LowLevelLookup,
    .forget      = NULL,
    .getattr     = LowLevelGetAttr,
    .setattr     = NULL,
    .readlink    = NULL,
    .mknod       = NULL,
    .mkdir       = NULL,
    .unlink      = NULL,
    .rmdir       = NULL,
    .symlink     = NULL,
    .rename      = NULL,
    .link        = NULL,
    .open        = LowLevelOpen,
    .read        = LowLevelRead,
    .write       = LowLevelWrite,
    .flush       = NULL,
    .release     = NULL,
    .fsync       = NULL,
    .opendir     = LowLevelOpenDir,
    .readdir     = LowLevelReadDir,
    .releasedir  = NULL,
    .fsyncdir    = NULL,
    .statfs      = NULL,
    .setxattr    = NULL,
    .getxattr    = NULL,
    .listxattr   = NULL,
    .removexattr = NULL,
    .access      = NULL,
    .create      = NULL,
    .getlk       = NULL,
    .setlk       = NULL,
    .bmap        = NULL,
#if FUSE_USE_VERSION < 35
    .ioctl = NULL,  // Different function signature. See fuse_lowlevel.h
#else
    .ioctl = NULL,
#endif
    .poll            = NULL,
    .write_buf       = NULL,
    .retrieve_reply  = NULL,
    .forget_multi    = NULL,
    .flock           = NULL,
    .fallocate       = NULL,
    .readdirplus     = NULL,
    .copy_file_range = NULL,
    .lseek           = NULL,
};

}  // namespace mock_sysfs

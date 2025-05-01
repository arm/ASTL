#include <fuse_lowlevel.h>
#include <fuse_opt.h>

#include <iostream>

#include "common.hpp"
#include "mock_sysfs.hpp"  // Declares: extern const fuse_lowlevel_ops fuse_ll_ops;

void CleanFuse(struct fuse_args* args, struct fuse_session* fuse_session_handle, struct fuse_loop_config* config) {
  if (fuse_session_handle) {
    fuse_remove_signal_handlers(fuse_session_handle);
    fuse_session_destroy(fuse_session_handle);
  }

  fuse_loop_cfg_destroy(config);
  fuse_opt_free_args(args);
}

int main(int argc, char* argv[]) {
  int                      ret  = -1;
  struct fuse_args         args = FUSE_ARGS_INIT(argc, argv);
  struct fuse_cmdline_opts opts {};
  struct fuse_session*     fuse_session_handle = nullptr;
  struct fuse_loop_config* config{};

  if (fuse_parse_cmdline(&args, &opts)) {
    ret = static_cast<int>(mock_sysfs::ErrorCode::CMDLINE_PARSE_ERROR);
    return ret;
  }

  std::unique_ptr<char, decltype(&std::free)> mountpoint_raii(opts.mountpoint, std::free);

  if (opts.show_help) {
    std::cout << "usage: ./MockSysfs [options] <mountpoint>\n\n";
    fuse_cmdline_help();
    fuse_lowlevel_help();
    CleanFuse(&args, nullptr, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::SUCCESS);
    return ret;
  }

  if (opts.show_version) {
    std::cout << "FUSE library version " << fuse_pkgversion() << "\n";
    fuse_lowlevel_version();
    CleanFuse(&args, nullptr, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::SUCCESS);
    return ret;
  }

  if (opts.mountpoint == nullptr) {
    std::cout << "usage: ./MockSysfs [options] <mountpoint>\n";
    std::cout << "       ./MockSysfs --help\n";
    CleanFuse(&args, nullptr, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::MOUNTPOINT_MISSING);
    return ret;
  }

  mock_sysfs::FuseUserData fuse_user_data{};

  fuse_session_handle =
      fuse_session_new(&args, &mock_sysfs::kFuseLowLevelOps, sizeof(mock_sysfs::kFuseLowLevelOps), &fuse_user_data);
  if (fuse_session_handle == nullptr) {
    CleanFuse(&args, fuse_session_handle, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::SESSION_CREATION_FAILED);
    return ret;
  }

  if (fuse_set_signal_handlers(fuse_session_handle) != 0) {
    CleanFuse(&args, fuse_session_handle, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::SIGNAL_HANDLER_ERROR);
    return ret;
  }

  if (fuse_session_mount(fuse_session_handle, opts.mountpoint) != 0) {
    CleanFuse(&args, fuse_session_handle, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::SYSFS_MOUNT_FAILED);
    return ret;
  }

  if (opts.singlethread) {
    ret = fuse_session_loop(fuse_session_handle);
  } else {
    config = fuse_loop_cfg_create();
    fuse_loop_cfg_set_clone_fd(config, opts.clone_fd);
    fuse_loop_cfg_set_max_threads(config, opts.max_threads);
    ret = fuse_session_loop_mt(fuse_session_handle, config);
  }

  fuse_session_unmount(fuse_session_handle);
  CleanFuse(&args, fuse_session_handle, config);

  return ret ? static_cast<int>(mock_sysfs::ErrorCode::SESSION_LOOP_FAILED)
             : static_cast<int>(mock_sysfs::ErrorCode::SUCCESS);
}

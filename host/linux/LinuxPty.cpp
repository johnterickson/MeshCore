#include "LinuxPty.h"

#include <cerrno>
#include <cstring>
#include <filesystem>

#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

LinuxPty::~LinuxPty() {
  if (master_fd_ >= 0) close(master_fd_);
  if (!link_path_.empty()) unlink(link_path_.c_str());
}

bool LinuxPty::begin(const std::string& link_path, const std::string& group) {
  if (master_fd_ >= 0 || link_path.empty()) {
    last_error_ = "invalid PTY path or PTY already open";
    return false;
  }

  master_fd_ = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (master_fd_ < 0 || grantpt(master_fd_) != 0 || unlockpt(master_fd_) != 0) {
    last_error_ = std::strerror(errno);
    if (master_fd_ >= 0) close(master_fd_);
    master_fd_ = -1;
    return false;
  }

  const char* slave_path = ptsname(master_fd_);
  if (slave_path == nullptr) {
    last_error_ = std::strerror(errno);
    close(master_fd_);
    master_fd_ = -1;
    return false;
  }

  const int slave_fd = open(slave_path, O_RDWR | O_NOCTTY);
  if (slave_fd < 0) {
    last_error_ = std::strerror(errno);
    close(master_fd_);
    master_fd_ = -1;
    return false;
  }
  termios settings{};
  bool configured = tcgetattr(slave_fd, &settings) == 0;
  if (configured) {
    cfmakeraw(&settings);
    configured = tcsetattr(slave_fd, TCSANOW, &settings) == 0;
  }
  close(slave_fd);
  if (!configured) {
    last_error_ = std::strerror(errno);
    close(master_fd_);
    master_fd_ = -1;
    return false;
  }

  if (chmod(slave_path, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP) != 0) {
    last_error_ = std::strerror(errno);
    close(master_fd_);
    master_fd_ = -1;
    return false;
  }
  if (!group.empty()) {
    const struct group* entry = getgrnam(group.c_str());
    if (entry == nullptr || chown(slave_path, static_cast<uid_t>(-1), entry->gr_gid) != 0) {
      last_error_ = entry == nullptr ? "PTY group not found: " + group : std::strerror(errno);
      close(master_fd_);
      master_fd_ = -1;
      return false;
    }
  }

  std::error_code error;
  const std::filesystem::path parent = std::filesystem::path(link_path).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent, error);
  if (error) {
    last_error_ = error.message();
    close(master_fd_);
    master_fd_ = -1;
    return false;
  }
  unlink(link_path.c_str());
  if (symlink(slave_path, link_path.c_str()) != 0) {
    last_error_ = std::strerror(errno);
    close(master_fd_);
    master_fd_ = -1;
    return false;
  }
  link_path_ = link_path;
  return true;
}

bool LinuxPty::isPeerConnected() const {
  if (master_fd_ < 0) return false;
  pollfd descriptor{master_fd_, 0, 0};
  return poll(&descriptor, 1, 0) >= 0 && (descriptor.revents & POLLHUP) == 0;
}

ssize_t LinuxPty::read(void* data, size_t len) {
  return master_fd_ < 0 ? -1 : ::read(master_fd_, data, len);
}

ssize_t LinuxPty::write(const void* data, size_t len) {
  return master_fd_ < 0 ? -1 : ::write(master_fd_, data, len);
}
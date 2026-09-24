#pragma once

#include <cstddef>
#include <string>

class LinuxPty {
public:
  LinuxPty() = default;
  ~LinuxPty();

  LinuxPty(const LinuxPty&) = delete;
  LinuxPty& operator=(const LinuxPty&) = delete;

  bool begin(const std::string& link_path, const std::string& group = "dialout");
  bool isOpen() const { return master_fd_ >= 0; }
  bool isPeerConnected() const;
  int getFileDescriptor() const { return master_fd_; }
  const std::string& getLinkPath() const { return link_path_; }
  const std::string& getLastError() const { return last_error_; }

  ssize_t read(void* data, size_t len);
  ssize_t write(const void* data, size_t len);

private:
  int master_fd_ = -1;
  std::string link_path_;
  std::string last_error_;
};
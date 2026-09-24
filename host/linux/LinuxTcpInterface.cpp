#include "LinuxTcpInterface.h"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

bool setNonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // namespace

LinuxTcpInterface::~LinuxTcpInterface() {
  closeClient();
  if (server_fd_ >= 0) close(server_fd_);
}

bool LinuxTcpInterface::begin(uint16_t port) {
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    last_error_ = std::strerror(errno);
    return false;
  }

  int reuse = 1;
  setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(port);
  if (bind(server_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      listen(server_fd_, 1) != 0 || !setNonblocking(server_fd_)) {
    last_error_ = std::strerror(errno);
    close(server_fd_);
    server_fd_ = -1;
    return false;
  }
  return true;
}

void LinuxTcpInterface::disable() {
  enabled_ = false;
  closeClient();
}

void LinuxTcpInterface::loop() {
  if (!enabled_ || server_fd_ < 0) return;
  acceptClient();
  readClient();
  flushOutput();
}

void LinuxTcpInterface::acceptClient() {
  const int fd = accept(server_fd_, nullptr, nullptr);
  if (fd < 0) return;
  if (!setNonblocking(fd)) {
    close(fd);
    return;
  }
  closeClient();
  client_fd_ = fd;
}

void LinuxTcpInterface::readClient() {
  if (client_fd_ < 0) return;
  uint8_t buffer[512];
  while (true) {
    const ssize_t count = recv(client_fd_, buffer, sizeof(buffer), 0);
    if (count > 0) {
      input_.insert(input_.end(), buffer, buffer + count);
      parseInput();
      continue;
    }
    if (count == 0) {
      closeClient();
    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      closeClient();
    }
    return;
  }
}

void LinuxTcpInterface::parseInput() {
  while (input_.size() >= 3) {
    if (input_[0] != '<') {
      input_.erase(input_.begin());
      continue;
    }
    const size_t length = static_cast<size_t>(input_[1]) |
                          (static_cast<size_t>(input_[2]) << 8);
    if (input_.size() < length + 3) return;
    if (length > 0 && length <= MAX_FRAME_SIZE) {
      received_.emplace_back(input_.begin() + 3, input_.begin() + 3 + length);
    }
    input_.erase(input_.begin(), input_.begin() + 3 + length);
  }
}

void LinuxTcpInterface::flushOutput() {
  while (client_fd_ >= 0 && !output_.empty()) {
    const std::vector<uint8_t>& frame = output_.front();
    const ssize_t count = send(client_fd_, frame.data() + output_offset_,
                               frame.size() - output_offset_, MSG_NOSIGNAL);
    if (count > 0) {
      output_offset_ += static_cast<size_t>(count);
      if (output_offset_ == frame.size()) {
        output_.pop_front();
        output_offset_ = 0;
      }
      continue;
    }
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      closeClient();
    }
    return;
  }
}

void LinuxTcpInterface::closeClient() {
  if (client_fd_ >= 0) close(client_fd_);
  client_fd_ = -1;
  input_.clear();
  received_.clear();
  output_.clear();
  output_offset_ = 0;
}

size_t LinuxTcpInterface::writeFrame(const uint8_t src[], size_t len) {
  if (!enabled_ || client_fd_ < 0 || len == 0 || len > MAX_FRAME_SIZE || isWriteBusy()) return 0;
  std::vector<uint8_t> frame;
  frame.reserve(len + 3);
  frame.push_back('>');
  frame.push_back(static_cast<uint8_t>(len));
  frame.push_back(static_cast<uint8_t>(len >> 8));
  frame.insert(frame.end(), src, src + len);
  output_.push_back(std::move(frame));
  flushOutput();
  return len;
}

size_t LinuxTcpInterface::checkRecvFrame(uint8_t dest[]) {
  loop();
  if (received_.empty()) return 0;
  std::vector<uint8_t> frame = std::move(received_.front());
  received_.pop_front();
  std::copy(frame.begin(), frame.end(), dest);
  return frame.size();
}
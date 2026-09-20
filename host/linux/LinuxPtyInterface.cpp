#include "LinuxPtyInterface.h"

#include <algorithm>
#include <cerrno>

bool LinuxPtyInterface::begin(const std::string& path, const std::string& group) {
  return pty_.begin(path, group);
}

void LinuxPtyInterface::disable() {
  enabled_ = false;
  was_connected_ = false;
  clearConnectionState();
}

void LinuxPtyInterface::loop() {
  if (!enabled_) return;
  const bool connected = pty_.isPeerConnected();
  if (!connected) {
    if (was_connected_) clearConnectionState();
    was_connected_ = false;
    return;
  }
  if (!was_connected_) clearConnectionState();
  was_connected_ = true;

  uint8_t buffer[512];
  while (true) {
    const ssize_t count = pty_.read(buffer, sizeof(buffer));
    if (count > 0) {
      input_.insert(input_.end(), buffer, buffer + count);
      parseInput();
      continue;
    }
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      was_connected_ = false;
      clearConnectionState();
      return;
    }
    break;
  }
  flushOutput();
}

void LinuxPtyInterface::parseInput() {
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

void LinuxPtyInterface::flushOutput() {
  while (isConnected() && !output_.empty()) {
    const std::vector<uint8_t>& frame = output_.front();
    const ssize_t count = pty_.write(frame.data() + output_offset_, frame.size() - output_offset_);
    if (count > 0) {
      output_offset_ += static_cast<size_t>(count);
      if (output_offset_ == frame.size()) {
        output_.pop_front();
        output_offset_ = 0;
      }
      continue;
    }
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      was_connected_ = false;
      clearConnectionState();
    }
    return;
  }
}

void LinuxPtyInterface::clearConnectionState() {
  input_.clear();
  received_.clear();
  output_.clear();
  output_offset_ = 0;
}

size_t LinuxPtyInterface::writeFrame(const uint8_t src[], size_t len) {
  if (!isConnected() || len == 0 || len > MAX_FRAME_SIZE || isWriteBusy()) return 0;
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

size_t LinuxPtyInterface::checkRecvFrame(uint8_t dest[]) {
  loop();
  if (received_.empty()) return 0;
  std::vector<uint8_t> frame = std::move(received_.front());
  received_.pop_front();
  std::copy(frame.begin(), frame.end(), dest);
  return frame.size();
}
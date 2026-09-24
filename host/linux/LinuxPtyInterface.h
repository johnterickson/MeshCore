#pragma once

#include "LinuxPty.h"

#include <helpers/BaseSerialInterface.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class LinuxPtyInterface : public BaseSerialInterface {
public:
  bool begin(const std::string& path, const std::string& group = "dialout");
  const std::string& getLastError() const { return pty_.getLastError(); }

  void enable() override { enabled_ = true; }
  void disable() override;
  bool isEnabled() const override { return enabled_; }
  bool isConnected() const override { return enabled_ && pty_.isPeerConnected(); }
  void loop() override;
  bool isWriteBusy() const override { return output_.size() >= 8; }
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

private:
  void parseInput();
  void flushOutput();
  void clearConnectionState();

  LinuxPty pty_;
  bool enabled_ = false;
  bool was_connected_ = false;
  std::vector<uint8_t> input_;
  std::deque<std::vector<uint8_t>> received_;
  std::deque<std::vector<uint8_t>> output_;
  size_t output_offset_ = 0;
};
#pragma once

#include <helpers/BaseSerialInterface.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class LinuxTcpInterface : public BaseSerialInterface {
public:
  LinuxTcpInterface() = default;
  ~LinuxTcpInterface();

  LinuxTcpInterface(const LinuxTcpInterface&) = delete;
  LinuxTcpInterface& operator=(const LinuxTcpInterface&) = delete;

  bool begin(uint16_t port);
  const std::string& getLastError() const { return last_error_; }

  void enable() override { enabled_ = true; }
  void disable() override;
  bool isEnabled() const override { return enabled_; }
  bool isConnected() const override { return client_fd_ >= 0; }
  void loop() override;
  bool isWriteBusy() const override { return output_.size() >= 8; }
  size_t writeFrame(const uint8_t src[], size_t len) override;
  size_t checkRecvFrame(uint8_t dest[]) override;

private:
  void acceptClient();
  void readClient();
  void parseInput();
  void flushOutput();
  void closeClient();

  int server_fd_ = -1;
  int client_fd_ = -1;
  bool enabled_ = false;
  std::string last_error_;
  std::vector<uint8_t> input_;
  std::deque<std::vector<uint8_t>> received_;
  std::deque<std::vector<uint8_t>> output_;
  size_t output_offset_ = 0;
};
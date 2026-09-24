#pragma once

#include <MeshCore.h>

#include <chrono>
#include <cstdint>

class LinuxRTCClock : public mesh::RTCClock {
public:
  LinuxRTCClock() {
    const auto system_time = std::chrono::system_clock::now().time_since_epoch();
    setCurrentTime(static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(system_time).count()));
  }

  uint32_t getCurrentTime() override {
    const auto elapsed = std::chrono::steady_clock::now() - base_monotonic_;
    return base_time_ + static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
  }

  void setCurrentTime(uint32_t time) override {
    base_time_ = time;
    base_monotonic_ = std::chrono::steady_clock::now();
  }

private:
  uint32_t base_time_ = 0;
  std::chrono::steady_clock::time_point base_monotonic_;
};
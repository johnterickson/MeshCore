#pragma once

#include "LinuxPty.h"

#include <Arduino.h>

#include <array>
#include <cstddef>
#include <string>

class LinuxTextConsole {
public:
  bool begin(const std::string& path, const std::string& group = "dialout") {
    if (!pty_.begin(path, group)) return false;
    Serial.attach(pty_.getFileDescriptor());
    return true;
  }

  const std::string& getLastError() const { return pty_.getLastError(); }

  template <typename Handler>
  void loop(Handler&& handler) {
    while (Serial.available() > 0) {
      const int value = Serial.read();
      if (value < 0) return;
      const char character = static_cast<char>(value);
      if (character == '\n') continue;
      Serial.print(character);
      if (character == '\r') {
        command_[command_len_] = 0;
        Serial.print('\n');
        std::array<char, 512> reply{};
        handler(command_.data(), reply.data());
        if (reply[0] != 0) {
          Serial.print("  -> ");
          Serial.println(reply.data());
        }
        command_len_ = 0;
      } else if (command_len_ + 1 < command_.size()) {
        command_[command_len_++] = character;
      }
    }
  }

private:
  LinuxPty pty_;
  std::array<char, 160> command_{};
  size_t command_len_ = 0;
};
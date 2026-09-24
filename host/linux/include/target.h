#pragma once

#include <MeshCore.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/SensorManager.h>
#include <LittleFS.h>

#include "KissRadio.h"
#include "LinuxRTCClock.h"

#define RP2040_PLATFORM 1

class LinuxBoard : public mesh::MainBoard {
public:
  uint16_t getBattMilliVolts() override { return 5000; }
  float getMCUTemperature() override { return 25.0f; }
  const char* getManufacturerName() const override { return "Linux"; }
  void reboot() override;
  uint8_t getStartupReason() const override { return BD_STARTUP_NORMAL; }
  bool isExternalPowered() override { return true; }
  uint16_t getBootVoltage() override { return 5000; }
};

extern LinuxBoard board;
extern KissRadio radio_driver;
extern LinuxRTCClock rtc_clock;
extern SensorManager sensors;

mesh::LocalIdentity radio_new_identity();
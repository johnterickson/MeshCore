#include "target.h"

#include <cstring>
#include <random>

#include <cstdlib>

LinuxBoard board;
KissRadio radio_driver("/dev/ttyUSB0");
LinuxRTCClock rtc_clock;
SensorManager sensors;

mesh::LocalIdentity radio_new_identity() {
  StdRNG rng;
  rng.begin(static_cast<long>(std::random_device{}()));
  return mesh::LocalIdentity(&rng);
}

void LinuxBoard::reboot() {
  std::exit(0);
}

void LinuxBoard::attachDynamicPrefs(KeyValueStore* prefs) {
  prefs_ = prefs;
  char value[8] = {0};
  uint8_t fem_values = 0;
  if (prefs->getByKey("fem_rxgain", value, sizeof(value)) && std::strcmp(value, "1") == 0) {
    fem_values |= HW_FEM_RX_GAIN;
  }
  value[0] = 0;
  if (prefs->getByKey("fem_txgain", value, sizeof(value)) && std::strcmp(value, "1") == 0) {
    fem_values |= HW_FEM_TX_GAIN;
  }
  radio_driver.setFemState(HW_FEM_RX_GAIN | HW_FEM_TX_GAIN, fem_values);

  value[0] = 0;
  if (prefs->getByKey("agc_int", value, sizeof(value))) {
    radio_driver.setAgcResetInterval(static_cast<uint16_t>(std::strtoul(value, nullptr, 10) * 4));
  }
}

bool LinuxBoard::handleCommand(const char* command, uint32_t, char* reply) {
  uint8_t bit = 0;
  if (std::strstr(command, "radio.fem.rxgain") != nullptr) bit = HW_FEM_RX_GAIN;
  if (std::strstr(command, "radio.fem.txgain") != nullptr) bit = HW_FEM_TX_GAIN;
  if (bit == 0) return false;

  if (std::strncmp(command, "get ", 4) == 0) {
    if ((radio_driver.getFemCapabilities() & bit) == 0) {
      std::strcpy(reply, "Error: unsupported");
    } else {
      std::sprintf(reply, "> %s", (radio_driver.getFemState() & bit) != 0 ? "on" : "off");
    }
    return true;
  }

  const char* state = std::strrchr(command, ' ');
  if (std::strncmp(command, "set ", 4) != 0 || state == nullptr ||
      (std::strcmp(state + 1, "on") != 0 && std::strcmp(state + 1, "off") != 0)) {
    std::strcpy(reply, "Error: state must be on or off");
    return true;
  }
  const bool enabled = std::strcmp(state + 1, "on") == 0;
  if (radio_driver.setFemState(bit, enabled ? bit : 0)) {
    if (prefs_ != nullptr) {
      prefs_->setByKey(bit == HW_FEM_RX_GAIN ? "fem_rxgain" : "fem_txgain", enabled ? "1" : "0");
    }
    std::strcpy(reply, "OK");
  } else {
    std::strcpy(reply, "Error: unsupported");
  }
  return true;
}
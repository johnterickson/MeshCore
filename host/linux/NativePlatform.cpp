#include "target.h"

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

bool LinuxBoard::setLoRaFemLnaEnabled(bool enable) {
  return radio_driver.setFemRxGainEnabled(enable);
}

bool LinuxBoard::isLoRaFemLnaEnabled() const {
  return radio_driver.isFemRxGainEnabled();
}

bool LinuxBoard::setLoRaFemPaGainEnabled(bool enable) {
  return radio_driver.setFemTxGainEnabled(enable);
}

bool LinuxBoard::isLoRaFemPaGainEnabled() const {
  return radio_driver.isFemTxGainEnabled();
}
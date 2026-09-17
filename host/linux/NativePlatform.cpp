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
#include "target.h"

#include <helpers/IdentityStore.h>
#include <helpers/SimpleMeshTables.h>

#include "MyMesh.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <iostream>
#include <random>
#include <string>
#include <thread>

namespace {

std::atomic<bool> stop_requested{false};

void handleSignal(int) {
  stop_requested.store(true);
}

void printUsage() {
  std::cout << "Usage: meshcore-linux-room-server [--device PATH] [--baud RATE] [--name NAME]\n";
}

} // namespace

StdRNG fast_rng;
SimpleMeshTables tables;
ArduinoMillis millis_clock;
MyMesh the_mesh(board, radio_driver, millis_clock, fast_rng, rtc_clock, tables);

int main(int argc, char** argv) {
  std::string device = "/dev/ttyUSB0";
  std::string name;
  int baud = 115200;
  for (int i = 1; i < argc; ++i) {
    const std::string argument(argv[i]);
    if (argument == "--device" && i + 1 < argc) {
      device = argv[++i];
    } else if (argument == "--baud" && i + 1 < argc) {
      baud = std::stoi(argv[++i]);
    } else if (argument == "--name" && i + 1 < argc) {
      name = argv[++i];
    } else if (argument == "--help") {
      printUsage();
      return 0;
    } else {
      printUsage();
      return 2;
    }
  }

  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  radio_driver.setDevice(device, baud);
  radio_driver.begin();
  if (!radio_driver.isOpen()) {
    std::cerr << "Failed to open " << device << ": " << radio_driver.getLastError() << "\n";
    return 1;
  }

  fast_rng.begin(static_cast<long>(std::random_device{}()));
  if (!LittleFS.begin()) {
    std::cerr << "Failed to initialize data directory\n";
    return 1;
  }

  IdentityStore identities(LittleFS, "/identity");
  identities.begin();
  if (!identities.load("_main", the_mesh.self_id)) {
    the_mesh.self_id = mesh::LocalIdentity(&fast_rng);
    identities.save("_main", the_mesh.self_id);
  }

  the_mesh.begin(&LittleFS);
  if (!name.empty()) {
    std::snprintf(the_mesh.getNodePrefs()->node_name,
                  sizeof(the_mesh.getNodePrefs()->node_name), "%s", name.c_str());
    the_mesh.savePrefs();
  }
  the_mesh.sendSelfAdvertisement(16000, false);
  std::cout << "MeshCore room server using " << device << " at " << baud << " baud\n";

  while (!stop_requested.load()) {
    the_mesh.loop();
    sensors.loop();
    rtc_clock.tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return 0;
}
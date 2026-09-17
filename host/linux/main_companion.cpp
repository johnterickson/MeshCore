#include "target.h"

#include "LinuxTcpInterface.h"
#include "DataStore.h"
#include "MyMesh.h"

#include <helpers/SimpleMeshTables.h>

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
  std::cout << "Usage: meshcore-linux-companion [--device PATH] [--baud RATE]"
               " [--port PORT] [--name NAME]\n";
}

} // namespace

DataStore store(LittleFS, rtc_clock);
StdRNG fast_rng;
SimpleMeshTables tables;
LinuxTcpInterface tcp_interface;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store);

int main(int argc, char** argv) {
  std::string device = "/dev/ttyUSB0";
  std::string name;
  int baud = 115200;
  int port = 5000;
  for (int i = 1; i < argc; ++i) {
    const std::string argument(argv[i]);
    if (argument == "--device" && i + 1 < argc) {
      device = argv[++i];
    } else if (argument == "--baud" && i + 1 < argc) {
      baud = std::stoi(argv[++i]);
    } else if (argument == "--port" && i + 1 < argc) {
      port = std::stoi(argv[++i]);
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
  if (port < 1 || port > 65535) {
    std::cerr << "Invalid TCP port\n";
    return 2;
  }

  std::signal(SIGINT, handleSignal);
  std::signal(SIGTERM, handleSignal);

  radio_driver.setDevice(device, baud);
  radio_driver.begin();
  if (!radio_driver.isOpen()) {
    std::cerr << "Failed to open " << device << ": " << radio_driver.getLastError() << "\n";
    return 1;
  }
  if (!tcp_interface.begin(static_cast<uint16_t>(port))) {
    std::cerr << "Failed to listen on port " << port << ": " << tcp_interface.getLastError() << "\n";
    return 1;
  }

  fast_rng.begin(static_cast<long>(std::random_device{}()));
  if (!LittleFS.begin()) {
    std::cerr << "Failed to initialize data directory\n";
    return 1;
  }
  store.begin();
  the_mesh.begin(false);
  if (!name.empty()) {
    std::snprintf(the_mesh.getNodePrefs()->node_name,
                  sizeof(the_mesh.getNodePrefs()->node_name), "%s", name.c_str());
    the_mesh.savePrefs();
  }
  the_mesh.startInterface(tcp_interface);
  sensors.begin();
  std::cout << "MeshCore companion using " << device << ", TCP port " << port << "\n";

  while (!stop_requested.load()) {
    the_mesh.loop();
    tcp_interface.loop();
    sensors.loop();
    rtc_clock.tick();
    radio_driver.waitForEvent(100);
  }
  return 0;
}
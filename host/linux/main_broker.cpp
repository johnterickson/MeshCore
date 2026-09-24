#include "KissBroker.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> stop_requested{false};

void handleSignal(int) {
  stop_requested.store(true);
}

void printUsage() {
  std::cout << "Usage: meshcore-kiss-broker [--device PATH] [--baud RATE] [--socket-dir DIR]"
               " [--endpoint NAME ...] [--rf-endpoint NAME]\n";
}

} // namespace

int main(int argc, char** argv) {
  std::string device = "/dev/ttyUSB0";
  std::string socket_dir = "/tmp/meshcore-kiss";
  std::string rf_endpoint = "repeater";
  std::vector<std::string> endpoint_names;
  int baud = 115200;
  for (int i = 1; i < argc; ++i) {
    const std::string argument(argv[i]);
    if (argument == "--device" && i + 1 < argc) {
      device = argv[++i];
    } else if (argument == "--baud" && i + 1 < argc) {
      baud = std::stoi(argv[++i]);
    } else if (argument == "--socket-dir" && i + 1 < argc) {
      socket_dir = argv[++i];
    } else if (argument == "--endpoint" && i + 1 < argc) {
      endpoint_names.emplace_back(argv[++i]);
    } else if (argument == "--rf-endpoint" && i + 1 < argc) {
      rf_endpoint = argv[++i];
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
  if (endpoint_names.empty()) endpoint_names = {"repeater", "room", "companion"};
  KissBroker broker(device, baud, socket_dir, endpoint_names, std::chrono::seconds(1), rf_endpoint);
  if (!broker.begin()) {
    std::cerr << "Failed to start KISS broker: " << broker.getLastError() << "\n";
    return 1;
  }
  std::cout << "KISS broker using " << device;
  if (!broker.isPhysicalConnected()) std::cout << " (waiting for device)";
  std::cout << "\n";
  for (const std::string& name : broker.getEndpointNames()) {
    std::cout << "  " << name << ": unix:" << broker.getSocketDir() << "/" << name << ".sock\n";
  }

  bool was_connected = broker.isPhysicalConnected();
  while (!stop_requested.load()) {
    broker.loop();
    broker.waitForEvent(100);
    const bool is_connected = broker.isPhysicalConnected();
    if (is_connected != was_connected) {
      if (is_connected) std::cout << "KISS device reconnected: " << device << "\n";
      else std::cerr << "KISS device disconnected: " << device << "\n";
      was_connected = is_connected;
    }
  }
  return 0;
}
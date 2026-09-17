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
               " [--endpoint NAME ...]\n";
}

} // namespace

int main(int argc, char** argv) {
  std::string device = "/dev/ttyUSB0";
  std::string socket_dir = "/tmp/meshcore-kiss";
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
  KissBroker broker(device, baud, socket_dir, endpoint_names);
  if (!broker.begin()) {
    std::cerr << "Failed to start KISS broker: " << broker.getLastError() << "\n";
    return 1;
  }
  std::cout << "KISS broker using " << device << "\n";
  for (const std::string& name : broker.getEndpointNames()) {
    std::cout << "  " << name << ": unix:" << broker.getSocketDir() << "/" << name << ".sock\n";
  }

  while (!stop_requested.load()) {
    broker.loop();
    broker.waitForEvent(100);
  }
  return 0;
}
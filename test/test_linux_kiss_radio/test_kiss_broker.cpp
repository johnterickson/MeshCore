#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "KissBroker.h"

namespace {

struct PseudoTerminal {
  int master = -1;
  std::string slave;
};

PseudoTerminal createPseudoTerminal() {
  PseudoTerminal terminal;
  terminal.master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (terminal.master < 0 || grantpt(terminal.master) != 0 || unlockpt(terminal.master) != 0) {
    return terminal;
  }
  const char* slave = ptsname(terminal.master);
  if (slave) terminal.slave = slave;
  return terminal;
}

void writeAll(int fd, const std::vector<uint8_t>& bytes) {
  size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t count = write(fd, bytes.data() + offset, bytes.size() - offset);
    ASSERT_GT(count, 0);
    offset += static_cast<size_t>(count);
  }
}

std::vector<uint8_t> readAvailable(int fd) {
  std::vector<uint8_t> bytes;
  std::array<uint8_t, 512> buffer{};
  while (true) {
    const ssize_t count = recv(fd, buffer.data(), buffer.size(), MSG_DONTWAIT);
    if (count > 0) {
      bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + count);
      continue;
    }
    EXPECT_TRUE(count == 0 || errno == EAGAIN || errno == EWOULDBLOCK);
    return bytes;
  }
}

std::vector<uint8_t> readPhysicalAvailable(int fd) {
  std::vector<uint8_t> bytes;
  std::array<uint8_t, 512> buffer{};
  while (true) {
    const ssize_t count = read(fd, buffer.data(), buffer.size());
    if (count > 0) {
      bytes.insert(bytes.end(), buffer.begin(), buffer.begin() + count);
      continue;
    }
    EXPECT_TRUE(count == 0 || errno == EAGAIN || errno == EWOULDBLOCK);
    return bytes;
  }
}

int connectEndpoint(const std::string& path) {
  const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) return fd;
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

void drainPseudoTerminal(int fd) {
  std::array<uint8_t, 512> buffer{};
  while (read(fd, buffer.data(), buffer.size()) > 0) {}
}

TEST(KissBrokerTest, ReconnectsPhysicalDeviceWithoutDisconnectingRole) {
  const std::string root = "/tmp/meshcore-kiss-broker-test-" + std::to_string(getpid());
  const std::string device = root + "/device";
  const std::string endpoint = root + "/role.sock";
  std::filesystem::remove_all(root);
  ASSERT_TRUE(std::filesystem::create_directories(root));

  PseudoTerminal first = createPseudoTerminal();
  ASSERT_GE(first.master, 0);
  ASSERT_FALSE(first.slave.empty());
  ASSERT_EQ(symlink(first.slave.c_str(), device.c_str()), 0);

  {
    KissBroker broker(device, 115200, root, {"role"}, std::chrono::milliseconds(0), "role");
    ASSERT_TRUE(broker.begin()) << broker.getLastError();
    ASSERT_TRUE(broker.isPhysicalConnected());
    broker.loop();
    drainPseudoTerminal(first.master);

    const int role = socket(AF_UNIX, SOCK_STREAM, 0);
    ASSERT_GE(role, 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, endpoint.c_str(), endpoint.size() + 1);
    ASSERT_EQ(connect(role, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
    broker.loop();

    const std::vector<uint8_t> first_packet = {KISS_FEND, KISS_CMD_DATA, 0x11, KISS_FEND};
    writeAll(first.master, first_packet);
    ASSERT_TRUE(broker.waitForEvent(100));
    broker.loop();
    EXPECT_EQ(readAvailable(role), first_packet);

    close(first.master);
    first.master = -1;
    ASSERT_TRUE(broker.waitForEvent(100));
    ASSERT_FALSE(broker.isPhysicalConnected());

    writeAll(role, {KISS_FEND, KISS_CMD_DATA, 0x22, KISS_FEND});
    broker.loop();
    EXPECT_EQ(readAvailable(role), (std::vector<uint8_t>{
        KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_TX_DONE, 0x00, KISS_FEND}));

    PseudoTerminal second = createPseudoTerminal();
    ASSERT_GE(second.master, 0);
    ASSERT_FALSE(second.slave.empty());
    ASSERT_EQ(unlink(device.c_str()), 0);
    ASSERT_EQ(symlink(second.slave.c_str(), device.c_str()), 0);

    broker.loop();
    ASSERT_TRUE(broker.isPhysicalConnected()) << broker.getLastError();
    drainPseudoTerminal(second.master);

    const std::vector<uint8_t> second_packet = {KISS_FEND, KISS_CMD_DATA, 0x33, KISS_FEND};
    writeAll(second.master, second_packet);
    ASSERT_TRUE(broker.waitForEvent(100));
    broker.loop();
    EXPECT_EQ(readAvailable(role), second_packet);

    close(second.master);
    close(role);
  }

  std::filesystem::remove_all(root);
}

TEST(KissBrokerTest, RoutesTrafficThroughRepeaterRfGateway) {
  const std::string root = "/tmp/meshcore-kiss-topology-test-" + std::to_string(getpid());
  std::filesystem::remove_all(root);
  ASSERT_TRUE(std::filesystem::create_directories(root));

  PseudoTerminal physical = createPseudoTerminal();
  ASSERT_GE(physical.master, 0);
  ASSERT_FALSE(physical.slave.empty());

  {
  KissBroker broker(physical.slave, 115200, root, {"repeater", "room-1", "companion-1"});
  ASSERT_TRUE(broker.begin()) << broker.getLastError();
  broker.loop();
  drainPseudoTerminal(physical.master);

  const int repeater = connectEndpoint(root + "/repeater.sock");
  const int room = connectEndpoint(root + "/room-1.sock");
  const int companion = connectEndpoint(root + "/companion-1.sock");
  ASSERT_GE(repeater, 0);
  ASSERT_GE(room, 0);
  ASSERT_GE(companion, 0);
  broker.loop();

  writeAll(room, {KISS_FEND, KISS_CMD_FULLDUPLEX, 0x00, KISS_FEND});
  broker.loop();
  EXPECT_TRUE(readPhysicalAvailable(physical.master).empty());
  EXPECT_TRUE(readAvailable(repeater).empty());
  EXPECT_TRUE(readAvailable(room).empty());
  EXPECT_TRUE(readAvailable(companion).empty());

  const std::vector<uint8_t> local_packet = {KISS_FEND, KISS_CMD_DATA, 0x11, KISS_FEND};
  const std::vector<uint8_t> local_delivery = {
    KISS_FEND, KISS_CMD_DATA, 0x11, KISS_FEND,
    KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_RX_META, 12,
    static_cast<uint8_t>(-30), KISS_FEND};
  writeAll(room, local_packet);
  broker.loop();
  EXPECT_TRUE(readPhysicalAvailable(physical.master).empty());
  EXPECT_EQ(readAvailable(room), (std::vector<uint8_t>{
    KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_TX_DONE, 0x01, KISS_FEND}));
  EXPECT_EQ(readAvailable(repeater), local_delivery);
  EXPECT_EQ(readAvailable(companion), local_delivery);

  const std::vector<uint8_t> rf_packet = {KISS_FEND, KISS_CMD_DATA, 0x22, KISS_FEND};
  const std::vector<uint8_t> rf_metadata = {
    KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_RX_META, 7,
    static_cast<uint8_t>(-90), KISS_FEND};
  writeAll(physical.master, rf_packet);
  writeAll(physical.master, rf_metadata);
  broker.loop();
  std::vector<uint8_t> repeater_rx = rf_packet;
  repeater_rx.insert(repeater_rx.end(), rf_metadata.begin(), rf_metadata.end());
  EXPECT_EQ(readAvailable(repeater), repeater_rx);
  EXPECT_TRUE(readAvailable(room).empty());
  EXPECT_TRUE(readAvailable(companion).empty());

  const std::vector<uint8_t> repeated_packet = {KISS_FEND, KISS_CMD_DATA, 0x33, KISS_FEND};
  writeAll(repeater, repeated_packet);
  broker.loop();
  EXPECT_EQ(readPhysicalAvailable(physical.master), repeated_packet);
  EXPECT_TRUE(readAvailable(room).empty());
  EXPECT_TRUE(readAvailable(companion).empty());

  const std::vector<uint8_t> tx_done = {
    KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_TX_DONE, 0x01, KISS_FEND};
  writeAll(physical.master, tx_done);
  broker.loop();
  EXPECT_EQ(readAvailable(repeater), tx_done);
  const std::vector<uint8_t> repeated_delivery = {
    KISS_FEND, KISS_CMD_DATA, 0x33, KISS_FEND,
    KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_RX_META, 12,
    static_cast<uint8_t>(-30), KISS_FEND};
  EXPECT_EQ(readAvailable(room), repeated_delivery);
  EXPECT_EQ(readAvailable(companion), repeated_delivery);

  close(companion);
  close(room);
  close(repeater);
  }

  close(physical.master);
  std::filesystem::remove_all(root);
}

} // namespace
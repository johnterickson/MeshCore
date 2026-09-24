#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "KissRadio.h"
#include "LinuxRTCClock.h"

namespace {

void writeBytes(int fd, const std::vector<uint8_t>& bytes) {
  size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t count = write(fd, bytes.data() + offset, bytes.size() - offset);
    ASSERT_GT(count, 0);
    offset += static_cast<size_t>(count);
  }
}

std::vector<uint8_t> readBytes(int fd) {
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

class KissRadioTest : public ::testing::Test {
protected:
  int sockets[2] = {-1, -1};

  void SetUp() override {
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets), 0);
  }

  void TearDown() override {
    close(sockets[0]);
    close(sockets[1]);
  }
};

TEST_F(KissRadioTest, DisablesModemTxSchedulingAtStartup) {
  KissRadio radio(sockets[0]);

  radio.begin();

  EXPECT_EQ(readBytes(sockets[1]), (std::vector<uint8_t>{
      KISS_FEND, KISS_CMD_TXDELAY, 0x00, KISS_FEND,
      KISS_FEND, KISS_CMD_FULLDUPLEX, 0x01, KISS_FEND}));
}

TEST_F(KissRadioTest, WaitsForSocketReadiness) {
  KissRadio radio(sockets[0]);

  EXPECT_FALSE(radio.waitForEvent(0));
  writeBytes(sockets[1], {KISS_FEND});
  EXPECT_TRUE(radio.waitForEvent(100));
}

TEST(LinuxRTCClockTest, StartsAtSystemTime) {
  const std::time_t before = std::time(nullptr);
  LinuxRTCClock clock;
  const std::time_t after = std::time(nullptr);

  EXPECT_GE(clock.getCurrentTime(), static_cast<uint32_t>(before));
  EXPECT_LE(clock.getCurrentTime(), static_cast<uint32_t>(after));
}

TEST(KissRadioUnixSocketTest, ConnectsToBrokerEndpoint) {
  const std::string path = "/tmp/meshcore-kiss-radio-test-" + std::to_string(getpid()) + ".sock";
  const int server = socket(AF_UNIX, SOCK_STREAM, 0);
  ASSERT_GE(server, 0);
  unlink(path.c_str());
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  ASSERT_EQ(bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
  ASSERT_EQ(listen(server, 1), 0);

  KissRadio radio("unix:" + path);
  radio.begin();
  ASSERT_TRUE(radio.isOpen()) << radio.getLastError();
  const int client = accept(server, nullptr, nullptr);
  ASSERT_GE(client, 0);
  EXPECT_EQ(readBytes(client), (std::vector<uint8_t>{
      KISS_FEND, KISS_CMD_TXDELAY, 0x00, KISS_FEND,
      KISS_FEND, KISS_CMD_FULLDUPLEX, 0x01, KISS_FEND}));

  close(client);
  close(server);
  unlink(path.c_str());
}

TEST_F(KissRadioTest, EscapesDataAndWaitsForTxDone) {
  KissRadio radio(sockets[0]);
  const uint8_t packet[] = {KISS_FEND, KISS_FESC, 0x42};

  ASSERT_TRUE(radio.startSendRaw(packet, sizeof(packet)));
  EXPECT_EQ(readBytes(sockets[1]), (std::vector<uint8_t>{
      KISS_FEND, KISS_CMD_DATA, KISS_FESC, KISS_TFEND,
      KISS_FESC, KISS_TFESC, 0x42, KISS_FEND}));
  EXPECT_FALSE(radio.isSendComplete());

  writeBytes(sockets[1], {KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_TX_DONE, 0x01, KISS_FEND});
  radio.loop();
  EXPECT_TRUE(radio.isSendComplete());

  radio.onSendFinished();
  EXPECT_TRUE(radio.isInRecvMode());
}

TEST_F(KissRadioTest, CanSendAgainAfterTxDoneTimeout) {
  KissRadio radio(sockets[0]);
  const uint8_t packet[] = {0x42};

  ASSERT_TRUE(radio.startSendRaw(packet, sizeof(packet)));
  EXPECT_FALSE(radio.isInRecvMode());
  readBytes(sockets[1]);

  radio.onSendFinished();
  EXPECT_TRUE(radio.isInRecvMode());
  EXPECT_TRUE(radio.startSendRaw(packet, sizeof(packet)));
}

TEST_F(KissRadioTest, WithholdsReceivedDataUntilMetadataArrives) {
  KissRadio radio(sockets[0]);
  std::array<uint8_t, KISS_MAX_PACKET_SIZE> output{};

  writeBytes(sockets[1], {KISS_FEND, KISS_CMD_DATA, 0x11, 0x22, KISS_FEND});
  radio.loop();
  EXPECT_EQ(radio.recvRaw(output.data(), output.size()), 0);

  writeBytes(sockets[1], {KISS_FEND, KISS_CMD_SETHARDWARE, HW_RESP_RX_META, 0xF8, 0xB0, KISS_FEND});
  radio.loop();
  ASSERT_EQ(radio.recvRaw(output.data(), output.size()), 2);
  EXPECT_EQ(output[0], 0x11);
  EXPECT_EQ(output[1], 0x22);
  EXPECT_FLOAT_EQ(radio.getLastSNR(), -2.0f);
  EXPECT_FLOAT_EQ(radio.getLastRSSI(), -80.0f);
}

TEST_F(KissRadioTest, ParsesFragmentedEscapedInput) {
  KissRadio radio(sockets[0]);
  std::array<uint8_t, KISS_MAX_PACKET_SIZE> output{};

  writeBytes(sockets[1], {KISS_FEND, KISS_CMD_DATA, 0x33, KISS_FESC});
  radio.loop();
  EXPECT_EQ(radio.recvRaw(output.data(), output.size()), 0);

  writeBytes(sockets[1], {KISS_TFEND, KISS_FEND, KISS_FEND, KISS_CMD_SETHARDWARE,
                          HW_RESP_RX_META, 0x04, 0xA6, KISS_FEND});
  radio.loop();
  ASSERT_EQ(radio.recvRaw(output.data(), output.size()), 2);
  EXPECT_EQ(output[0], 0x33);
  EXPECT_EQ(output[1], KISS_FEND);
  EXPECT_FLOAT_EQ(radio.getLastSNR(), 1.0f);
  EXPECT_FLOAT_EQ(radio.getLastRSSI(), -90.0f);
}

TEST_F(KissRadioTest, SendsRadioParametersAndTxPowerConsecutively) {
  KissRadio radio(sockets[0]);
  radio.setParams(915.0f, 250.0f, 9, 5);
  radio.setTxPower(20);

  const uint32_t frequency_hz = 915000000;
  const uint32_t bandwidth_hz = 250000;
  std::vector<uint8_t> expected = {KISS_FEND, KISS_CMD_SETHARDWARE, HW_CMD_SET_RADIO};
  const auto* frequency_bytes = reinterpret_cast<const uint8_t*>(&frequency_hz);
  expected.insert(expected.end(), {KISS_FESC, KISS_TFEND});
  expected.insert(expected.end(), frequency_bytes + 1, frequency_bytes + sizeof(frequency_hz));
  const auto* bandwidth_bytes = reinterpret_cast<const uint8_t*>(&bandwidth_hz);
  expected.insert(expected.end(), bandwidth_bytes, bandwidth_bytes + sizeof(bandwidth_hz));
  expected.insert(expected.end(), {9, 5, KISS_FEND,
                                   KISS_FEND, KISS_CMD_SETHARDWARE, HW_CMD_SET_TX_POWER, 20, KISS_FEND});

  EXPECT_EQ(readBytes(sockets[1]), expected);
  const uint8_t packet[] = {0x42};
  EXPECT_TRUE(radio.startSendRaw(packet, sizeof(packet)));
}

} // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>

#include <fcntl.h>
#include <unistd.h>

#include "LinuxPtyInterface.h"

namespace {

TEST(LinuxPtyInterfaceTest, ExchangesCompanionProtocolFrames) {
  const std::string root = "/tmp/meshcore-linux-pty-interface-test-" + std::to_string(getpid());
  const std::string link = root + "/companion";
  std::filesystem::remove_all(root);

  LinuxPtyInterface interface;
  ASSERT_TRUE(interface.begin(link, "")) << interface.getLastError();
  interface.enable();
  const int client = open(link.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  ASSERT_GE(client, 0);
  ASSERT_TRUE(interface.isConnected());

  const std::array<uint8_t, 7> request = {'<', 4, 0, 1, 2, 3, 4};
  ASSERT_EQ(write(client, request.data(), request.size()), request.size());
  std::array<uint8_t, MAX_FRAME_SIZE> payload{};
  ASSERT_EQ(interface.checkRecvFrame(payload.data()), 4U);
  EXPECT_EQ(payload[0], 1);
  EXPECT_EQ(payload[1], 2);
  EXPECT_EQ(payload[2], 3);
  EXPECT_EQ(payload[3], 4);

  const std::array<uint8_t, 3> response = {9, 8, 7};
  ASSERT_EQ(interface.writeFrame(response.data(), response.size()), response.size());
  std::array<uint8_t, 6> framed{};
  ASSERT_EQ(read(client, framed.data(), framed.size()), framed.size());
  EXPECT_EQ(framed, (std::array<uint8_t, 6>{'>', 3, 0, 9, 8, 7}));

  close(client);
  std::filesystem::remove_all(root);
}

} // namespace
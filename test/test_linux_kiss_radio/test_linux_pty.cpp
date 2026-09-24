#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <filesystem>
#include <string>

#include <fcntl.h>
#include <unistd.h>

#include "LinuxPty.h"

namespace {

TEST(LinuxPtyTest, PublishesStableBidirectionalTerminal) {
  const std::string root = "/tmp/meshcore-linux-pty-test-" + std::to_string(getpid());
  const std::string link = root + "/console";
  std::filesystem::remove_all(root);

  {
    LinuxPty pty;
    ASSERT_TRUE(pty.begin(link, "")) << pty.getLastError();
    EXPECT_TRUE(std::filesystem::is_symlink(link));
    EXPECT_FALSE(pty.isPeerConnected());

    const int client = open(link.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    ASSERT_GE(client, 0);
    EXPECT_TRUE(pty.isPeerConnected());

    const std::array<char, 4> request = {'p', 'i', 'n', 'g'};
    ASSERT_EQ(write(client, request.data(), request.size()), request.size());
    std::array<char, 4> received{};
    ASSERT_EQ(pty.read(received.data(), received.size()), received.size());
    EXPECT_EQ(received, request);

    const std::array<char, 4> response = {'p', 'o', 'n', 'g'};
    ASSERT_EQ(pty.write(response.data(), response.size()), response.size());
    received = {};
    ASSERT_EQ(read(client, received.data(), received.size()), received.size());
    EXPECT_EQ(received, response);

    close(client);
  }

  EXPECT_FALSE(std::filesystem::exists(link));
  std::filesystem::remove_all(root);
}

} // namespace
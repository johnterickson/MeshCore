#include "KissBroker.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <unordered_set>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

namespace {

bool setNonblocking(int fd) {
  const int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

speed_t baudFlag(int baud) {
  switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    default: return 0;
  }
}

} // namespace

KissBroker::KissBroker(std::string device, int baud, std::string socket_dir,
                       std::vector<std::string> endpoint_names)
    : device_(std::move(device)), baud_(baud), socket_dir_(std::move(socket_dir)) {
  endpoints_.reserve(endpoint_names.size());
  for (std::string& name : endpoint_names) {
    Endpoint endpoint;
    endpoint.name = std::move(name);
    endpoints_.push_back(std::move(endpoint));
  }
}

KissBroker::~KissBroker() {
  for (Endpoint& endpoint : endpoints_) {
    closeClient(endpoint);
    if (endpoint.server_fd >= 0) close(endpoint.server_fd);
    if (!endpoint.path.empty()) unlink(endpoint.path.c_str());
  }
  if (physical_fd_ >= 0) close(physical_fd_);
}

bool KissBroker::begin() {
  return openPhysical() && createEndpoints();
}

std::vector<std::string> KissBroker::getEndpointNames() const {
  std::vector<std::string> names;
  names.reserve(endpoints_.size());
  for (const Endpoint& endpoint : endpoints_) names.push_back(endpoint.name);
  return names;
}

bool KissBroker::openPhysical() {
  physical_fd_ = open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (physical_fd_ < 0) {
    last_error_ = "open " + device_ + ": " + std::strerror(errno);
    return false;
  }
  const speed_t speed = baudFlag(baud_);
  termios settings{};
  if (speed == 0 || tcgetattr(physical_fd_, &settings) != 0) {
    last_error_ = speed == 0 ? "unsupported baud rate" : std::strerror(errno);
    return false;
  }
  cfmakeraw(&settings);
  cfsetispeed(&settings, speed);
  cfsetospeed(&settings, speed);
  settings.c_cflag |= CLOCAL | CREAD;
  settings.c_cflag &= ~(CSTOPB | CRTSCTS | PARENB);
  settings.c_cflag = (settings.c_cflag & ~CSIZE) | CS8;
  settings.c_cc[VMIN] = 0;
  settings.c_cc[VTIME] = 0;
  if (tcsetattr(physical_fd_, TCSANOW, &settings) != 0) {
    last_error_ = std::strerror(errno);
    return false;
  }
  tcflush(physical_fd_, TCIOFLUSH);
  return true;
}

bool KissBroker::createEndpoints() {
  if (endpoints_.empty() || endpoints_.size() > 64) {
    last_error_ = "broker requires between 1 and 64 endpoints";
    return false;
  }
  std::error_code error;
  std::filesystem::create_directories(socket_dir_, error);
  if (error) {
    last_error_ = error.message();
    return false;
  }
  std::unordered_set<std::string> names;
  for (Endpoint& endpoint : endpoints_) {
    const bool valid_name = !endpoint.name.empty() &&
        std::all_of(endpoint.name.begin(), endpoint.name.end(), [](unsigned char character) {
          return std::isalnum(character) || character == '-' || character == '_';
        });
    if (!valid_name || !names.insert(endpoint.name).second) {
      last_error_ = "invalid or duplicate endpoint name: " + endpoint.name;
      return false;
    }
    endpoint.path = socket_dir_ + "/" + endpoint.name + ".sock";
    if (endpoint.path.size() >= sizeof(sockaddr_un::sun_path)) {
      last_error_ = "Unix socket path too long: " + endpoint.path;
      return false;
    }
    unlink(endpoint.path.c_str());
    endpoint.server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (endpoint.server_fd < 0) {
      last_error_ = std::strerror(errno);
      return false;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, endpoint.path.c_str(), endpoint.path.size() + 1);
    if (bind(endpoint.server_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(endpoint.server_fd, 1) != 0 || !setNonblocking(endpoint.server_fd)) {
      last_error_ = std::strerror(errno);
      return false;
    }
  }
  return true;
}

std::vector<uint8_t> KissBroker::encodeFrame(const std::vector<uint8_t>& frame) {
  std::vector<uint8_t> encoded;
  encoded.reserve(frame.size() * 2 + 2);
  encoded.push_back(KISS_FEND);
  for (uint8_t byte : frame) {
    if (byte == KISS_FEND) {
      encoded.push_back(KISS_FESC);
      encoded.push_back(KISS_TFEND);
    } else if (byte == KISS_FESC) {
      encoded.push_back(KISS_FESC);
      encoded.push_back(KISS_TFESC);
    } else {
      encoded.push_back(byte);
    }
  }
  encoded.push_back(KISS_FEND);
  return encoded;
}

bool KissBroker::consumeByte(FrameDecoder& decoder, uint8_t byte, std::vector<uint8_t>& frame) {
  if (byte == KISS_FEND) {
    const bool complete = decoder.active && !decoder.frame.empty();
    if (complete) frame.swap(decoder.frame);
    decoder.frame.clear();
    decoder.active = true;
    decoder.escaped = false;
    return complete;
  }
  if (!decoder.active) return false;
  if (decoder.escaped) {
    if (byte == KISS_TFEND) byte = KISS_FEND;
    else if (byte == KISS_TFESC) byte = KISS_FESC;
    else {
      decoder = {};
      return false;
    }
    decoder.escaped = false;
  } else if (byte == KISS_FESC) {
    decoder.escaped = true;
    return false;
  }
  if (decoder.frame.size() >= KISS_MAX_FRAME_SIZE + 2) {
    decoder = {};
    return false;
  }
  decoder.frame.push_back(byte);
  return false;
}

void KissBroker::loop() {
  acceptClients();
  readPhysical();
  readClients();
  expireActiveTx();
  flushPhysical();
  flushClients();
}

bool KissBroker::waitForEvent(int timeout_ms) {
  std::vector<pollfd> descriptors;
  descriptors.reserve(1 + endpoints_.size() * 2);
  descriptors.push_back({physical_fd_, static_cast<short>(POLLIN | (physical_output_.empty() ? 0 : POLLOUT)), 0});
  for (const Endpoint& endpoint : endpoints_) {
    descriptors.push_back({endpoint.server_fd, POLLIN, 0});
    if (endpoint.client_fd >= 0) {
      descriptors.push_back({endpoint.client_fd,
                             static_cast<short>(POLLIN | (endpoint.output.empty() ? 0 : POLLOUT)), 0});
    }
  }
  return poll(descriptors.data(), descriptors.size(), timeout_ms) > 0;
}

void KissBroker::acceptClients() {
  for (Endpoint& endpoint : endpoints_) {
    const int fd = accept(endpoint.server_fd, nullptr, nullptr);
    if (fd < 0) continue;
    if (!setNonblocking(fd)) {
      close(fd);
      continue;
    }
    closeClient(endpoint);
    endpoint.client_fd = fd;
  }
}

void KissBroker::readPhysical() {
  uint8_t buffer[512];
  while (true) {
    const ssize_t count = read(physical_fd_, buffer, sizeof(buffer));
    if (count <= 0) return;
    for (ssize_t i = 0; i < count; ++i) {
      std::vector<uint8_t> frame;
      if (consumeByte(physical_decoder_, buffer[i], frame)) handlePhysicalFrame(frame);
    }
  }
}

void KissBroker::readClients() {
  uint8_t buffer[512];
  for (size_t endpoint_index = 0; endpoint_index < endpoints_.size(); ++endpoint_index) {
    Endpoint& endpoint = endpoints_[endpoint_index];
    while (endpoint.client_fd >= 0) {
      const ssize_t count = recv(endpoint.client_fd, buffer, sizeof(buffer), 0);
      if (count > 0) {
        for (ssize_t i = 0; i < count; ++i) {
          std::vector<uint8_t> frame;
          if (consumeByte(endpoint.decoder, buffer[i], frame)) handleClientFrame(endpoint_index, frame);
        }
        continue;
      }
      if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) closeClient(endpoint);
      break;
    }
  }
}

void KissBroker::handlePhysicalFrame(const std::vector<uint8_t>& frame) {
  if (frame.empty()) return;
  const std::vector<uint8_t> encoded = encodeFrame(frame);
  const uint8_t command = frame[0] & 0x0F;
  if (command == KISS_CMD_DATA) {
    broadcast(encoded);
    return;
  }
  if (command == KISS_CMD_SETHARDWARE && frame.size() >= 2) {
    const uint8_t response = frame[1];
    if (response == HW_RESP_RX_META) {
      broadcast(encoded);
      return;
    }
    if (response == HW_RESP_TX_DONE) {
      if (active_tx_endpoint_ >= 0) queueClient(static_cast<size_t>(active_tx_endpoint_), encoded);
      if (frame.size() >= 3 && frame[2] != 0) echoActiveTxToPeers();
      active_tx_endpoint_ = -1;
      active_tx_frame_.clear();
      startNextTx();
      return;
    }
    if (response == HW_RESP_ERROR && active_tx_endpoint_ >= 0) {
      queueClient(static_cast<size_t>(active_tx_endpoint_), encoded);
      if (frame.size() >= 3 && frame[2] == HW_ERR_TX_BUSY) {
        active_tx_endpoint_ = -1;
        active_tx_frame_.clear();
        startNextTx();
      }
      return;
    }
  }
  broadcast(encoded);
}

void KissBroker::handleClientFrame(size_t endpoint, const std::vector<uint8_t>& frame) {
  if (frame.empty()) return;
  const std::vector<uint8_t> encoded = encodeFrame(frame);
  if ((frame[0] & 0x0F) != KISS_CMD_DATA) {
    queuePhysical(encoded);
    return;
  }
  if (active_tx_endpoint_ < 0) {
    active_tx_endpoint_ = static_cast<int>(endpoint);
    active_tx_frame_ = encoded;
    active_tx_started_ = std::chrono::steady_clock::now();
    queuePhysical(encoded);
  } else {
    pending_tx_.push_back({endpoint, encoded});
  }
}

void KissBroker::startNextTx() {
  if (active_tx_endpoint_ >= 0 || pending_tx_.empty()) return;
  PendingTx pending = std::move(pending_tx_.front());
  pending_tx_.pop_front();
  active_tx_endpoint_ = static_cast<int>(pending.endpoint);
  active_tx_frame_ = pending.frame;
  active_tx_started_ = std::chrono::steady_clock::now();
  queuePhysical(pending.frame);
}

void KissBroker::expireActiveTx() {
  if (active_tx_endpoint_ < 0 ||
      std::chrono::steady_clock::now() - active_tx_started_ < std::chrono::seconds(8)) return;
  const std::vector<uint8_t> failed = encodeFrame({KISS_CMD_SETHARDWARE, HW_RESP_TX_DONE, 0x00});
  queueClient(static_cast<size_t>(active_tx_endpoint_), failed);
  active_tx_endpoint_ = -1;
  active_tx_frame_.clear();
  startNextTx();
}

void KissBroker::queuePhysical(const std::vector<uint8_t>& encoded) {
  physical_output_.push_back(encoded);
}

void KissBroker::queueClient(size_t endpoint, const std::vector<uint8_t>& encoded) {
  if (endpoint < endpoints_.size() && endpoints_[endpoint].client_fd >= 0) {
    endpoints_[endpoint].output.push_back(encoded);
  }
}

void KissBroker::broadcast(const std::vector<uint8_t>& encoded) {
  for (size_t i = 0; i < endpoints_.size(); ++i) queueClient(i, encoded);
}

void KissBroker::echoActiveTxToPeers() {
  if (active_tx_endpoint_ < 0 || active_tx_frame_.empty()) return;
  const std::vector<uint8_t> metadata = encodeFrame({
      KISS_CMD_SETHARDWARE, HW_RESP_RX_META,
      static_cast<uint8_t>(12), static_cast<uint8_t>(-30)});
  for (size_t endpoint = 0; endpoint < endpoints_.size(); ++endpoint) {
    if (endpoint == static_cast<size_t>(active_tx_endpoint_)) continue;
    queueClient(endpoint, active_tx_frame_);
    queueClient(endpoint, metadata);
  }
}

void KissBroker::flushPhysical() {
  while (!physical_output_.empty()) {
    const std::vector<uint8_t>& frame = physical_output_.front();
    const ssize_t count = write(physical_fd_, frame.data() + physical_output_offset_,
                                frame.size() - physical_output_offset_);
    if (count > 0) {
      physical_output_offset_ += static_cast<size_t>(count);
      if (physical_output_offset_ == frame.size()) {
        physical_output_.pop_front();
        physical_output_offset_ = 0;
      }
      continue;
    }
    return;
  }
}

void KissBroker::flushClients() {
  for (Endpoint& endpoint : endpoints_) {
    while (endpoint.client_fd >= 0 && !endpoint.output.empty()) {
      const std::vector<uint8_t>& frame = endpoint.output.front();
      const ssize_t count = send(endpoint.client_fd, frame.data() + endpoint.output_offset,
                                 frame.size() - endpoint.output_offset, MSG_NOSIGNAL);
      if (count > 0) {
        endpoint.output_offset += static_cast<size_t>(count);
        if (endpoint.output_offset == frame.size()) {
          endpoint.output.pop_front();
          endpoint.output_offset = 0;
        }
        continue;
      }
      if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) closeClient(endpoint);
      break;
    }
  }
}

void KissBroker::closeClient(Endpoint& endpoint) {
  if (endpoint.client_fd >= 0) close(endpoint.client_fd);
  endpoint.client_fd = -1;
  endpoint.decoder = {};
  endpoint.output.clear();
  endpoint.output_offset = 0;
}
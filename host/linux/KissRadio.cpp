#include "KissRadio.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

namespace {

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

KissRadio::KissRadio(const std::string& device, int baud)
  : device_(device), baud_(baud), fd_(-1), owns_fd_(true) {}

KissRadio::KissRadio(int fd)
  : baud_(0), fd_(fd), owns_fd_(false) {
  int flags = fcntl(fd_, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
  }
}

KissRadio::~KissRadio() {
  if (owns_fd_ && fd_ >= 0) {
    close(fd_);
  }
}

void KissRadio::begin() {
  if (fd_ < 0) {
    if (!openSerial()) return;
  }
  const uint8_t tx_delay = 0;
  const uint8_t full_duplex = 1;
  sendCommand(KISS_CMD_TXDELAY, &tx_delay, 1);
  sendCommand(KISS_CMD_FULLDUPLEX, &full_duplex, 1);
}

void KissRadio::setDevice(const std::string& device, int baud) {
  if (fd_ >= 0) return;
  device_ = device;
  baud_ = baud;
}

bool KissRadio::openSerial() {
  static const std::string unix_prefix = "unix:";
  if (device_.compare(0, unix_prefix.size(), unix_prefix) == 0) {
    return openUnixSocket(device_.substr(unix_prefix.size()));
  }
  fd_ = open(device_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) {
    last_error_ = std::strerror(errno);
    return false;
  }
  if (!configureSerial(baud_)) {
    close(fd_);
    fd_ = -1;
    return false;
  }
  return true;
}

bool KissRadio::openUnixSocket(const std::string& path) {
  if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) {
    last_error_ = "invalid Unix socket path";
    return false;
  }
  fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd_ < 0) {
    last_error_ = std::strerror(errno);
    return false;
  }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  if (connect(fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    last_error_ = std::strerror(errno);
    close(fd_);
    fd_ = -1;
    return false;
  }
  const int flags = fcntl(fd_, F_GETFL, 0);
  if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) != 0) {
    last_error_ = std::strerror(errno);
    close(fd_);
    fd_ = -1;
    return false;
  }
  is_socket_ = true;
  return true;
}

bool KissRadio::configureSerial(int baud) {
  const speed_t speed = baudFlag(baud);
  if (speed == 0) {
    last_error_ = "unsupported baud rate";
    return false;
  }

  termios settings{};
  if (tcgetattr(fd_, &settings) != 0) {
    last_error_ = std::strerror(errno);
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
  if (tcsetattr(fd_, TCSANOW, &settings) != 0) {
    last_error_ = std::strerror(errno);
    return false;
  }
  tcflush(fd_, TCIOFLUSH);
  return true;
}

std::vector<uint8_t> KissRadio::encodeFrame(uint8_t type, const uint8_t* data, size_t len) {
  std::vector<uint8_t> encoded;
  encoded.reserve(2 + (len + 1) * 2);
  encoded.push_back(KISS_FEND);

  auto append = [&encoded](uint8_t byte) {
    if (byte == KISS_FEND) {
      encoded.push_back(KISS_FESC);
      encoded.push_back(KISS_TFEND);
    } else if (byte == KISS_FESC) {
      encoded.push_back(KISS_FESC);
      encoded.push_back(KISS_TFESC);
    } else {
      encoded.push_back(byte);
    }
  };

  append(type);
  for (size_t i = 0; i < len; ++i) {
    append(data[i]);
  }
  encoded.push_back(KISS_FEND);
  return encoded;
}

void KissRadio::loop() {
  if (fd_ < 0) return;

  flushOutput();
  uint8_t input[256];
  while (true) {
    const ssize_t count = read(fd_, input, sizeof(input));
    if (count > 0) {
      for (ssize_t i = 0; i < count; ++i) consumeByte(input[i]);
      continue;
    }
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      last_error_ = std::strerror(errno);
    }
    break;
  }
}

bool KissRadio::waitForEvent(int timeout_ms) {
  if (fd_ < 0) return false;
  pollfd descriptor{};
  descriptor.fd = fd_;
  descriptor.events = POLLIN;
  if (!output_.empty()) descriptor.events |= POLLOUT;
  return poll(&descriptor, 1, timeout_ms) > 0;
}

void KissRadio::consumeByte(uint8_t byte) {
  if (byte == KISS_FEND) {
    if (frame_active_ && frame_len_ > 0) processFrame();
    frame_active_ = true;
    frame_escaped_ = false;
    frame_len_ = 0;
    return;
  }
  if (!frame_active_) return;

  if (frame_escaped_) {
    if (byte == KISS_TFEND) byte = KISS_FEND;
    else if (byte == KISS_TFESC) byte = KISS_FESC;
    else {
      frame_active_ = false;
      frame_len_ = 0;
      frame_escaped_ = false;
      return;
    }
    frame_escaped_ = false;
  } else if (byte == KISS_FESC) {
    frame_escaped_ = true;
    return;
  }

  if (frame_len_ >= frame_.size()) {
    frame_active_ = false;
    frame_len_ = 0;
    return;
  }
  frame_[frame_len_++] = byte;
}

void KissRadio::processFrame() {
  if (frame_len_ < 1) return;
  const uint8_t type = frame_[0] & 0x0F;
  if (type == KISS_CMD_DATA) {
    if (frame_len_ > 1 && frame_len_ - 1 <= KISS_MAX_PACKET_SIZE) {
      pending_rx_.assign(frame_.begin() + 1, frame_.begin() + frame_len_);
    } else {
      ++packets_recv_errors_;
    }
    return;
  }
  if (type != KISS_CMD_SETHARDWARE || frame_len_ < 2) return;

  const uint8_t response = frame_[1];
  if (response == HW_RESP_RX_META && frame_len_ >= 4 && !pending_rx_.empty()) {
    received_.push_back({std::move(pending_rx_), static_cast<int8_t>(frame_[2]), static_cast<int8_t>(frame_[3])});
    pending_rx_.clear();
  } else if (response == HW_RESP_TX_DONE && frame_len_ >= 3 && tx_state_ == TxState::AwaitingDone) {
    tx_state_ = frame_[2] != 0 ? TxState::Complete : TxState::Failed;
    if (tx_state_ == TxState::Complete) ++packets_sent_;
    else ++packets_recv_errors_;
  } else if (response == HW_RESP_ERROR && frame_len_ >= 3 && frame_[2] == HW_ERR_TX_BUSY && tx_state_ != TxState::Idle) {
    tx_state_ = TxState::Failed;
    ++packets_recv_errors_;
  } else if (response == HW_RESP(HW_CMD_GET_NOISE_FLOOR) && frame_len_ >= 4) {
    noise_floor_ = static_cast<int16_t>(static_cast<uint16_t>(frame_[2]) |
                                        (static_cast<uint16_t>(frame_[3]) << 8));
  }
}

int KissRadio::recvRaw(uint8_t* bytes, int size) {
  if (received_.empty() || size <= 0) return 0;
  ReceivedPacket packet = std::move(received_.front());
  received_.pop_front();
  if (static_cast<int>(packet.bytes.size()) > size) return 0;

  std::copy(packet.bytes.begin(), packet.bytes.end(), bytes);
  last_snr_ = packet.snr;
  last_rssi_ = packet.rssi;
  ++packets_recv_;
  return static_cast<int>(packet.bytes.size());
}

bool KissRadio::startSendRaw(const uint8_t* bytes, int len) {
  if (fd_ < 0 || tx_state_ != TxState::Idle || !output_.empty() || len <= 0 || len > KISS_MAX_PACKET_SIZE) return false;
  output_ = encodeFrame(KISS_CMD_DATA, bytes, static_cast<size_t>(len));
  output_offset_ = 0;
  tx_state_ = TxState::Writing;
  flushOutput();
  return true;
}

void KissRadio::flushOutput() {
  while (output_offset_ < output_.size()) {
    const ssize_t count = write(fd_, output_.data() + output_offset_, output_.size() - output_offset_);
    if (count > 0) {
      output_offset_ += static_cast<size_t>(count);
      continue;
    }
    if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      last_error_ = std::strerror(errno);
      tx_state_ = TxState::Failed;
    }
    return;
  }
  output_.clear();
  output_offset_ = 0;
  if (tx_state_ == TxState::Writing) {
    tx_state_ = TxState::AwaitingDone;
  }
}

bool KissRadio::isSendComplete() {
  return tx_state_ == TxState::Complete;
}

void KissRadio::onSendFinished() {
  tx_state_ = TxState::Idle;
}

bool KissRadio::isInRecvMode() const {
  return tx_state_ == TxState::Idle || tx_state_ == TxState::Complete || tx_state_ == TxState::Failed;
}

bool KissRadio::sendHardwareCommand(uint8_t command, const uint8_t* data, size_t len) {
  std::vector<uint8_t> payload;
  payload.reserve(len + 1);
  payload.push_back(command);
  payload.insert(payload.end(), data, data + len);
  return sendCommand(KISS_CMD_SETHARDWARE, payload.data(), payload.size());
}

bool KissRadio::sendCommand(uint8_t command, const uint8_t* data, size_t len) {
  if (fd_ < 0 || tx_state_ != TxState::Idle) return false;
  const std::vector<uint8_t> frame = encodeFrame(command, data, len);
  output_.insert(output_.end(), frame.begin(), frame.end());
  flushOutput();
  return true;
}

void KissRadio::setParams(float freq, float bw, uint8_t sf, uint8_t cr) {
  config_.freq_hz = static_cast<uint32_t>(freq * 1000000.0f);
  config_.bw_hz = static_cast<uint32_t>(bw * 1000.0f);
  config_.sf = sf;
  config_.cr = cr;
  uint8_t data[10];
  std::memcpy(data, &config_.freq_hz, sizeof(config_.freq_hz));
  std::memcpy(data + 4, &config_.bw_hz, sizeof(config_.bw_hz));
  data[8] = sf;
  data[9] = cr;
  sendHardwareCommand(HW_CMD_SET_RADIO, data, sizeof(data));
}

void KissRadio::setTxPower(int8_t power_dbm) {
  config_.tx_power = static_cast<uint8_t>(power_dbm);
  sendHardwareCommand(HW_CMD_SET_TX_POWER, &config_.tx_power, 1);
}

void KissRadio::resetStats() {
  packets_recv_ = 0;
  packets_sent_ = 0;
  packets_recv_errors_ = 0;
}

uint32_t KissRadio::getEstAirtimeFor(int len_bytes) {
  const double symbol_ms = std::ldexp(1000.0, config_.sf) / config_.bw_hz;
  const int low_data_rate = symbol_ms >= 16.0 ? 1 : 0;
  const double numerator = 8.0 * len_bytes - 4.0 * config_.sf + 28.0 + 16.0;
  const double denominator = 4.0 * (config_.sf - 2 * low_data_rate);
  const double payload_symbols = 8.0 + std::max(0.0, std::ceil(numerator / denominator) * config_.cr);
  return static_cast<uint32_t>(std::ceil((17.0 + 4.25 + payload_symbols) * symbol_ms));
}

float KissRadio::packetScore(float snr, int packet_len) {
  static constexpr float thresholds[] = {-7.5f, -10.0f, -12.5f, -15.0f, -17.5f, -20.0f};
  if (config_.sf < 7 || config_.sf > 12) return 0.0f;
  const float threshold = thresholds[config_.sf - 7];
  if (snr < threshold) return 0.0f;
  const float score = ((snr - threshold) / 10.0f) * (1.0f - packet_len / 256.0f);
  return std::clamp(score, 0.0f, 1.0f);
}
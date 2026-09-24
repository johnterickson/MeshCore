#pragma once

#include <Dispatcher.h>
#include <helpers/kiss/KissProtocol.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class KissRadio : public mesh::Radio {
public:
  explicit KissRadio(const std::string& device, int baud = 115200);
  explicit KissRadio(int fd);
  ~KissRadio();

  KissRadio(const KissRadio&) = delete;
  KissRadio& operator=(const KissRadio&) = delete;

  void begin() override;
  void loop() override;
  bool waitForEvent(int timeout_ms);
  int recvRaw(uint8_t* bytes, int size) override;
  uint32_t getEstAirtimeFor(int len_bytes) override;
  float packetScore(float snr, int packet_len) override;
  bool startSendRaw(const uint8_t* bytes, int len) override;
  bool isSendComplete() override;
  void onSendFinished() override;
  bool isInRecvMode() const override;
  int getNoiseFloor() const override { return noise_floor_; }
  float getLastRSSI() const override { return last_rssi_; }
  float getLastSNR() const override { return last_snr_; }

  bool isOpen() const { return fd_ >= 0; }
  const std::string& getLastError() const { return last_error_; }
  void setRadioConfig(const KissRadioConfig& config) { config_ = config; }
  void setDevice(const std::string& device, int baud = 115200);
  void setParams(float freq, float bw, uint8_t sf, uint8_t cr);
  void setTxPower(int8_t power_dbm);
  bool setRxBoostedGainMode(bool enable);
  bool getRxBoostedGainMode() const { return (radio_gain_flags_ & RADIO_GAIN_RX_BOOSTED) != 0; }
  bool setFemRxGainEnabled(bool enable);
  bool isFemRxGainEnabled() const { return (radio_gain_flags_ & RADIO_GAIN_FEM_RX) != 0; }
  bool setFemTxGainEnabled(bool enable);
  bool isFemTxGainEnabled() const { return (radio_gain_flags_ & RADIO_GAIN_FEM_TX) != 0; }
  bool configSideDetectors(const uint8_t[], uint8_t, float) { return false; }
  uint32_t getPacketsRecv() const { return packets_recv_; }
  uint32_t getPacketsSent() const { return packets_sent_; }
  uint32_t getPacketsRecvErrors() const { return packets_recv_errors_; }
  void resetStats();

private:
  enum class TxState { Idle, Writing, AwaitingDone, Complete, Failed };

  struct ReceivedPacket {
    std::vector<uint8_t> bytes;
    int8_t snr;
    int8_t rssi;
  };

  static std::vector<uint8_t> encodeFrame(uint8_t type, const uint8_t* data, size_t len);
  bool openSerial();
  bool openUnixSocket(const std::string& path);
  bool configureSerial(int baud);
  void consumeByte(uint8_t byte);
  void processFrame();
  void flushOutput();
  bool sendCommand(uint8_t command, const uint8_t* data, size_t len);
  bool sendHardwareCommand(uint8_t command, const uint8_t* data, size_t len);
  bool setRadioGainFlag(uint8_t flag, bool enable);

  std::string device_;
  int baud_;
  int fd_;
  bool owns_fd_;
  bool is_socket_ = false;
  std::string last_error_;

  std::array<uint8_t, KISS_MAX_FRAME_SIZE + KISS_HW_SUBCMD_BYTES + KISS_TYPE_BYTES> frame_{};
  size_t frame_len_ = 0;
  bool frame_active_ = false;
  bool frame_escaped_ = false;

  std::vector<uint8_t> pending_rx_;
  std::deque<ReceivedPacket> received_;
  std::vector<uint8_t> output_;
  size_t output_offset_ = 0;
  TxState tx_state_ = TxState::Idle;

  KissRadioConfig config_{869618000, 62500, 8, 8, 22};
  uint8_t radio_gain_flags_ = 0;
  int noise_floor_ = -120;
  float last_rssi_ = 0.0f;
  float last_snr_ = 0.0f;
  uint32_t packets_recv_ = 0;
  uint32_t packets_sent_ = 0;
  uint32_t packets_recv_errors_ = 0;
};
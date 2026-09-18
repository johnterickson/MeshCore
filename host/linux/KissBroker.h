#pragma once

#include <helpers/kiss/KissProtocol.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

class KissBroker {
public:
  KissBroker(std::string device, int baud, std::string socket_dir,
             std::vector<std::string> endpoint_names,
             std::chrono::milliseconds reconnect_interval = std::chrono::seconds(1));
  ~KissBroker();

  KissBroker(const KissBroker&) = delete;
  KissBroker& operator=(const KissBroker&) = delete;

  bool begin();
  void loop();
  bool waitForEvent(int timeout_ms);
  bool isPhysicalConnected() const { return physical_fd_ >= 0; }
  const std::string& getLastError() const { return last_error_; }
  const std::string& getSocketDir() const { return socket_dir_; }
  std::vector<std::string> getEndpointNames() const;

private:
  struct FrameDecoder {
    bool active = false;
    bool escaped = false;
    std::vector<uint8_t> frame;
  };

  struct Endpoint {
    std::string name;
    std::string path;
    int server_fd = -1;
    int client_fd = -1;
    FrameDecoder decoder;
    std::deque<std::vector<uint8_t>> output;
    size_t output_offset = 0;
  };

  struct PendingTx {
    size_t endpoint;
    std::vector<uint8_t> frame;
  };

  static std::vector<uint8_t> encodeFrame(const std::vector<uint8_t>& frame);
  static bool consumeByte(FrameDecoder& decoder, uint8_t byte, std::vector<uint8_t>& frame);
  bool openPhysical();
  void disconnectPhysical(const std::string& error);
  void retryPhysical();
  void failPendingTx();
  void rememberPhysicalConfig(const std::vector<uint8_t>& frame);
  void replayPhysicalConfig();
  bool createEndpoints();
  void acceptClients();
  void readPhysical();
  void readClients();
  void handlePhysicalFrame(const std::vector<uint8_t>& frame);
  void handleClientFrame(size_t endpoint, const std::vector<uint8_t>& frame);
  void startNextTx();
  void expireActiveTx();
  void queuePhysical(const std::vector<uint8_t>& encoded);
  void queueClient(size_t endpoint, const std::vector<uint8_t>& encoded);
  void broadcast(const std::vector<uint8_t>& encoded);
  void echoActiveTxToPeers();
  void flushPhysical();
  void flushClients();
  void closeClient(Endpoint& endpoint);

  std::string device_;
  int baud_;
  std::string socket_dir_;
  std::string last_error_;
  std::chrono::milliseconds reconnect_interval_;
  std::chrono::steady_clock::time_point next_reconnect_at_;
  int physical_fd_ = -1;
  FrameDecoder physical_decoder_;
  std::deque<std::vector<uint8_t>> physical_output_;
  size_t physical_output_offset_ = 0;
  std::map<uint16_t, std::vector<uint8_t>> physical_config_;
  std::vector<Endpoint> endpoints_;
  std::deque<PendingTx> pending_tx_;
  int active_tx_endpoint_ = -1;
  std::vector<uint8_t> active_tx_frame_;
  std::chrono::steady_clock::time_point active_tx_started_;
};
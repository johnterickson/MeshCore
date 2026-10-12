#pragma once

#include <Arduino.h>
#include <Identity.h>
#include <Utils.h>
#include <Mesh.h>
#include <helpers/SensorManager.h>
#include <helpers/kiss/KissProtocol.h>

/* Matches the CLI reply buffer boards write into from handleCommand() */
#define KISS_BOARD_REPLY_SIZE    160

/* FEM requests are answered strictly in arrival order; this many may wait (e.g. during a TX) */
#define KISS_FEM_OP_QUEUE_DEPTH  4

#define KISS_AGC_RESET_DEFAULT_SEC 32  // a multiple of the 4 s step, so hosts can set it back
typedef void (*SetRadioCallback)(float freq, float bw, uint8_t sf, uint8_t cr);
typedef void (*SetTxPowerCallback)(uint8_t power);
typedef float (*GetCurrentRssiCallback)();
typedef void (*GetStatsCallback)(uint32_t* rx, uint32_t* tx, uint32_t* errors);
typedef bool (*SetRxBoostedGainCallback)(bool enable);
typedef bool (*GetRxBoostedGainCallback)();
typedef void (*PollRxCallback)();

struct RadioConfig {
  uint32_t freq_hz;
  uint32_t bw_hz;
  uint8_t sf;
  uint8_t cr;
  uint8_t tx_power;
};

enum FemOpKind : uint8_t {
  FEM_OP_SET,
  FEM_OP_GET,
  FEM_OP_ERROR
};

struct FemOp {
  FemOpKind kind;
  uint8_t apply_mask;  // FEM_OP_SET
  uint8_t value_mask;  // FEM_OP_SET; error code for FEM_OP_ERROR
  bool applied;        // FEM_OP_SET: hardware changed, reply still to be queued
};

enum TxState {
  TX_IDLE,
  TX_WAIT_CLEAR,
  TX_SLOT_WAIT,
  TX_DELAY,
  TX_SENDING,
  TX_DONE_PENDING
};

class KissModem {
  Stream& _serial;
  mesh::LocalIdentity& _identity;
  mesh::RNG& _rng;
  mesh::Radio& _radio;
  mesh::MainBoard& _board;
  SensorManager& _sensors;

  uint8_t _rx_buf[KISS_MAX_FRAME_SIZE];
  uint16_t _rx_len;
  bool _rx_escaped;
  bool _rx_active;

  uint8_t _pending_tx[KISS_MAX_PACKET_SIZE];
  uint16_t _pending_tx_len;
  bool _has_pending_tx;

  uint8_t _txdelay;
  uint8_t _persistence;
  uint8_t _slottime;
  uint8_t _txtail;
  uint8_t _fullduplex;

  TxState _tx_state;
  uint32_t _tx_timer;

  SetRadioCallback _setRadioCallback;
  SetTxPowerCallback _setTxPowerCallback;
  GetCurrentRssiCallback _getCurrentRssiCallback;
  GetStatsCallback _getStatsCallback;
  SetRxBoostedGainCallback _setRxBoostedGainCallback;
  GetRxBoostedGainCallback _getRxBoostedGainCallback;
  PollRxCallback _pollRxCallback;

  RadioConfig _config;
  bool _signal_report_enabled;
  uint16_t _agc_reset_interval_sec;
  uint32_t _next_agc_reset_ms;
  FemOp _fem_ops[KISS_FEM_OP_QUEUE_DEPTH];
  uint8_t _fem_op_head;
  uint8_t _fem_op_count;
  uint8_t _tx_frame_buf[KISS_TX_FRAME_QUEUE_DEPTH][KISS_MAX_ENCODED_FRAME_SIZE];
  uint16_t _tx_frame_len[KISS_TX_FRAME_QUEUE_DEPTH];
  uint16_t _tx_frame_written[KISS_TX_FRAME_QUEUE_DEPTH];
  uint8_t _tx_frame_head;
  uint8_t _tx_frame_tail;
  uint8_t _tx_frame_count;
  bool _tx_busy_error_pending;
  bool _tx_done_pending;
  uint8_t _tx_done_result;
  uint8_t _tx_hw_payload[KISS_HW_MAX_PAYLOAD_SIZE];

  static uint16_t appendEscapedByte(uint8_t* dest, uint16_t idx, uint16_t max_len, uint8_t b);
  static uint16_t encodeFrame(uint8_t type, const uint8_t* data, uint16_t len, uint8_t* dest, uint16_t max_len);
  void resetOutputQueue();
  void popTxFrame();
  bool tryFlushFrames();
  bool queueFrame(uint8_t type, const uint8_t* data, uint16_t len, bool mark_busy_error = true);
  bool queuePendingBusyError();
  bool queueHardwareFrame(uint8_t sub_cmd, const uint8_t* data, uint16_t len, bool mark_busy_error);
  bool queuePendingTxDone();
  void setTxDonePending(uint8_t result);
  bool writeHardwareFrame(uint8_t sub_cmd, const uint8_t* data, uint16_t len);
  void writeHardwareError(uint8_t error_code);
  void processFrame();
  void handleHardwareCommand(uint8_t sub_cmd, const uint8_t* data, uint16_t len);
  void processTx();
  void maybeResetAgc();
  bool queryFemGain(uint8_t bit, bool* enabled);
  bool setFemGain(uint8_t bit, bool enable);
  void readFemState(uint8_t* caps, uint8_t* values);
  bool queueFemReply();
  void applyFemState(uint8_t apply_mask, uint8_t value_mask);
  void enqueueFemOp(FemOpKind kind, uint8_t apply_mask, uint8_t value_mask);
  void processFemOps();

  void handleGetIdentity();
  void handleGetRandom(const uint8_t* data, uint16_t len);
  void handleVerifySignature(const uint8_t* data, uint16_t len);
  void handleSignData(const uint8_t* data, uint16_t len);
  void handleEncryptData(const uint8_t* data, uint16_t len);
  void handleDecryptData(const uint8_t* data, uint16_t len);
  void handleKeyExchange(const uint8_t* data, uint16_t len);
  void handleHash(const uint8_t* data, uint16_t len);
  void handleSetRadio(const uint8_t* data, uint16_t len);
  void handleSetTxPower(const uint8_t* data, uint16_t len);
  void handleGetRadio();
  void handleGetTxPower();
  void handleGetVersion();
  void handleGetCurrentRssi();
  void handleIsChannelBusy();
  void handleGetAirtime(const uint8_t* data, uint16_t len);
  void handleGetNoiseFloor();
  void handleGetStats();
  void handleGetBattery();
  void handlePing();
  void handleGetSensors(const uint8_t* data, uint16_t len);
  void handleGetMCUTemp();
  void handleReboot();
  void handleGetDeviceName();
  void handleSetSignalReport(const uint8_t* data, uint16_t len);
  void handleGetSignalReport();
  void handleGetCapabilities();
  void handleSetAgcResetInterval(const uint8_t* data, uint16_t len);
  void handleGetAgcResetInterval();
  void handleSetFemState(const uint8_t* data, uint16_t len);
  void handleGetFemState();
  void handleSetRxBoostedGain(const uint8_t* data, uint16_t len);
  void handleGetRxBoostedGain();

public:
  KissModem(Stream& serial, mesh::LocalIdentity& identity, mesh::RNG& rng,
            mesh::Radio& radio, mesh::MainBoard& board, SensorManager& sensors);

  void begin();
  void loop();

  void setRadioCallback(SetRadioCallback cb) { _setRadioCallback = cb; }
  void setTxPowerCallback(SetTxPowerCallback cb) { _setTxPowerCallback = cb; }
  void setGetCurrentRssiCallback(GetCurrentRssiCallback cb) { _getCurrentRssiCallback = cb; }
  void setGetStatsCallback(GetStatsCallback cb) { _getStatsCallback = cb; }
  // Only register these when the radio supports boosted RX gain; they define the capability.
  // Delivers any completed RX packet to the host; called before settings that restart the receiver.
  void setPollRxCallback(PollRxCallback cb) { _pollRxCallback = cb; }
  void setRxBoostedGainCallbacks(SetRxBoostedGainCallback set_cb, GetRxBoostedGainCallback get_cb) {
    _setRxBoostedGainCallback = set_cb;
    _getRxBoostedGainCallback = get_cb;
  }

  void onPacketReceived(int8_t snr, int8_t rssi, const uint8_t* packet, uint16_t len);
  bool isTxBusy() const { return _tx_state != TX_IDLE; }
  /** True only when radio is actually transmitting; use to skip recvRaw in main loop. */
  bool isActuallyTransmitting() const { return _tx_state == TX_SENDING; }
  bool isHostOutputBackedUp() const { return _tx_frame_count > 0 || _tx_busy_error_pending || _tx_done_pending; }
};

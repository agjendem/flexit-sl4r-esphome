#pragma once

// Pure byte-level protocol logic lives here, free of ESPHome so it can be
// unit-tested on a development machine. See tests/.
#include "protocol.h"

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/components/uart/uart.h"
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#ifdef USE_NUMBER
#include "esphome/components/number/number.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace esphome::flexit_sl4r {

#ifdef USE_CLIMATE
// Forward declaration: the climate platform includes this header, so we cannot
// include its header here without a circular dependency.
class FlexitClimate;
#endif

// See PROTOCOL.md for how every offset and length below was derived.
static constexpr uint32_t COMMUNICATION_TIMEOUT_MS = 5000;
// How long the bus must have been COMPLETELY quiet before we dare transmit.
//
// The first real transmit attempt collided: we sent 10 ms after a frame had
// been validated, but by then the CS50 had already started the next telegram.
// "Right after a frame" is therefore not a gap. We wait for measured silence
// instead. 5 ms is about 10 character times at 19200 baud, and observed gaps
// between telegrams are 20-55 ms, so there is ample margin.
static constexpr uint32_t BUS_IDLE_BEFORE_TX_MS = 5;
// Give up on a queued command that never finds a gap, rather than letting it
// sit in the queue forever.
static constexpr uint32_t COMMAND_GIVE_UP_MS = 5000;
// How long we may go unpolled before considering ourselves dropped from the
// bus. Our measured poll interval is ~0.2 s, so 30 s is very generous.
static constexpr uint32_t ENUMERATION_TIMEOUT_MS = 30000;
// --- General frame structure (measured; see PROTOCOL.md) ---
//   C3 b1 b2 b3 b4 TYPE b6 LEN [LEN data bytes] CK1 CK2
// The checksum window is [5 .. 8+LEN). Validated against 23,708 sniffed bytes:
// 766 frames, zero false C3 hits. Length + checksum is therefore a safe frame
// detector.


// Value the CS50 reports for a sensor input that is not connected.
static constexpr float SENSOR_DISCONNECTED = -55.0f;

// --- payload[6]: TWO independent bits, settled by controlled experiment ---
//   bit0 (0x01) = BOOST active
//   bit7 (0x80) = AFTERHEATER ENABLED
//
// Three misreadings preceded this, all because the two bits were observed
// together without varying one at a time:
//   1. "bit0 = the element is heating now" - disproved: bit0 was set during
//      boost while the afterheater was demonstrably off. A disabled element
//      cannot heat.
//   2. "bit7 = afterheater DISABLED (inverted)" - disproved: enabling the
//      afterheater sets the bit.
//   3. "bit7 is not the enable flag at all" - also wrong. It appeared in both
//      states only because OUR OWN writes were switching the afterheater off
//      behind our backs (see the mirroring bugs below).
// Verified both ways: enable -> 0x80, disable -> 0x00, boost -> 0x01.
// Vongraven's original "0=off, 128=on" was right all along.

// How long a boost we started ourselves is allowed to run before we cancel it.
// The CI50 times its OWN boost at 30 minutes for a single press (measured
// 2026-08-16: 30 min 24 s) and sends the cancel itself. It does not time a
// boost started from the bus, so we match its behaviour rather than leave the
// fans at maximum indefinitely.
static constexpr uint32_t BOOST_PERIOD_MS = 30UL * 60UL * 1000UL;

// How long to wait for a boost request to take effect before reporting that it
// did not. The CS50 normally acts within a second (0.8 s measured), so five is
// generous. It matters because a request made within roughly three minutes of a
// previous boost ending is discarded in silence - see PROTOCOL.md §5.5.
static constexpr uint32_t BOOST_CONFIRM_MS = 5000UL;

// The same flag in the PANEL's state frame (`data[4]` of the node 4 frame
// `20 0F`), which is the format we write in ourselves. Bit6 is a momentary
// button bit.
static constexpr uint8_t AFTERHEAT_ENABLED_BIT = 0x80;
static constexpr uint8_t PANEL_BUTTON_BIT = 0x40;

// --- payload[4]: alarm bit field (measured) ---
// Went from 2 to 0 the moment the filter alarm was reset on the panel, and
// stayed 0. No other bit has been observed set - the rotor alarm and the
// overheat thermostat are the likely candidates.
static constexpr uint8_t ALARM_FILTER = 0x02;

// --- payload[2] bit0: heat recovery running (measured) ---
// It is NOT set when heat demand starts, but when demand passes ~10 - that is,
// when the rotor actually starts turning, not when the demand appears.
// Verified on both edges: `[11]` 0->68 set the bit at 10, and when demand fell
// back to 0 the bit cleared.
static constexpr uint8_t HEAT_RECOVERY_RUNNING = 0x01;
// Bit1 of the same group. NEVER observed set across 837 status telegrams.
// Flexit uses the same output (J5 pin 11,12) for "rotor OR bypass motor"
// depending on unit type, so this reading is a QUALIFIED GUESS: on a
// plate-exchanger unit the group probably encodes bypass state. It cannot be
// settled on a rotary unit. If it ever flips, it is captured as an anomaly.
static constexpr uint8_t HEAT_RECOVERY_BYPASS = 0x02;

// --- data[4] of the state frame: button events (measured) ---
// The field we long carried as "unresolved". It reports which panel buttons
// are pressed:
//   0x01  - the boost button (seen right before a boost command)
//   0xC0  - both temperature buttons at once (= the filter-reset gesture)
// Normally 0x00.
static constexpr uint8_t BUTTON_NONE = 0x00;
// The setpoint the manual requires during a filter reset.
static constexpr uint8_t FILTER_RESET_SETPOINT = 20;

class FlexitSL4RComponent final : public Component, public uart::UARTDevice {
#ifdef USE_SELECT
  SUB_SELECT(fan_level)
#endif
#ifdef USE_NUMBER
  SUB_NUMBER(heat_exchanger_setpoint)
#endif
#ifdef USE_BINARY_SENSOR
  SUB_BINARY_SENSOR(afterheat_active)    // payload[6] bit0 - boost (independent cross-check against the [5] nibbles)
  SUB_BINARY_SENSOR(afterheat_enabled)   // payload[6] bit7 - afterheater enabled
  SUB_BINARY_SENSOR(filter_alarm)        // payload[4] bit1 - filter timer expired
  // Any bit in the alarm field other than the filter bit. Catches the red
  // alarm LED (rotor alarm / overheat thermostat - both undocumented bits) the
  // day it fires. WHICH bit it was can be read from the raw [4] sensor.
  SUB_BINARY_SENSOR(unknown_alarm)
  SUB_BINARY_SENSOR(heat_recovery_active)  // payload[2] bit0 - the rotor is turning
  SUB_BINARY_SENSOR(bypass_active)         // payload[2] bit1 - ASSUMED bypass, never observed
  SUB_BINARY_SENSOR(communication)
  SUB_BINARY_SENSOR(boost_active)
  // Are we actually being polled? Enumeration happens ONLY when the CS50 boots.
  // If our node is down at that moment we are dropped - and since writing is
  // done as a poll response, it then fails SILENTLY. Without this signal
  // everything looks normal while no command gets through.
  SUB_BINARY_SENSOR(enumerated)
#endif
#ifdef USE_SENSOR
  SUB_SENSOR(supply_air_temperature)       // 0xC2 reg 0 slot 1 - Flexit's B1
  SUB_SENSOR(heat_exchanger_setpoint_raw)  // 0xC2 reg 7 slot 1 - setpoint as a float
  SUB_SENSOR(fan_duty_supply)              // status payload[13], %
  SUB_SENSOR(fan_duty_extract)             // status payload[14], %
  SUB_SENSOR(fan_level_running)            // high nibble of payload[5]
  SUB_SENSOR(fan_level_return)             // low nibble - the level boost falls back to
  SUB_SENSOR(frames_discarded)             // frames failing checksum - makes bus corruption measurable
  SUB_SENSOR(status_interval)              // seconds between status telegrams
  SUB_SENSOR(heat_demand)                  // payload[11] - heat demand, drives the rotor
  SUB_SENSOR(anomalies)
#endif
#ifdef USE_TEXT_SENSOR
  // Firmware versions. The CS50 sends its own in bank 0x20 reg 0x00, the panel
  // its own in bank 0x22 reg 0x00 - both as 8 ASCII bytes. Corresponds to the
  // manual's "Test -> Information -> Main board / CS50 panel 1: Software rev."
  SUB_TEXT_SENSOR(controller_firmware)
  SUB_TEXT_SENSOR(panel_firmware)
#endif

 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_flow_control_pin(GPIOPin *pin) { this->flow_control_pin_ = pin; }
  // Which node we present ourselves as when transmitting. The CI50 is node 4
  // (panel 1). Node 5 is panel 2 - the identity dipswitch 3 selects on a
  // physical panel. The address field is computed by source_header_().
  void set_source_node(uint8_t node) { this->source_node_ = node; }
  // Answer polls addressed to our node. The bus is POLLED: the master sends
  // 5 bytes `C3 <node> 00 <cks>` and ONLY the addressed node replies - without
  // repeating the C3 header. Nodes that do not answer the enumeration scan at
  // startup are dropped.
  void set_respond_to_polls(bool v) { this->respond_to_polls_ = v; }

#ifdef USE_CLIMATE
  void set_ventilation_climate(FlexitClimate *c) { this->ventilation_climate_ = c; }
#endif

  // Called from the child entities' (select/switch/number) control() and
  // write_state() overrides.
  void set_fan_level(uint8_t level);                  // 1..3
  void set_heat_exchanger_setpoint(uint8_t celsius);  // 15..25

  // Boost ("max fan" - showering, cooking). Unlike the fields above this is
  // NOT a state write but a one-shot command: a short dedicated frame the CI50
  // sends once per button press. Captured and checksum-verified on a real
  // unit; see PROTOCOL.md. The unit goes to level 3 and falls back to the
  // previous level by itself when the period ends.
  void trigger_boost();

  // Cancels boost by writing the return level - the same mechanism as a manual
  // level change, only with the level the unit is going back to anyway.
  void cancel_boost();

  // Resets the filter timer, running the full procedure from the CI 50 manual:
  // setpoint to 20 degrees, then the reset flag, then back to the original
  // setpoint. The manual requires the 20-degree step, and a reset without it
  // turned out to acknowledge the alarm temporarily WITHOUT restarting the
  // timer - it came back on its own.
  void reset_filter_timer();
  // Switches the afterheater on or off by writing the field the panel uses.
  void set_afterheat_enabled(bool on);
  bool get_afterheat_enabled() const { return this->afterheat_enabled_; }

  // Boost is on when the unit is running a level other than the one it will
  // fall back to. Reading the state from the bus like this is what lets boost
  // be a switch rather than a pair of buttons: the period is timed by the
  // unit, so when it expires the switch follows by itself.
  bool get_boost_active() const {
    return (this->last_raw_fan_level_ >> 4) != (this->last_raw_fan_level_ & 0x0F);
  }

  // Dumps the boot capture to the log. Necessary because the most interesting
  // bytes - the CS50 registering its panels - arrive within the first seconds
  // after the bus is powered, long before WiFi and the API are up and the log
  // can be read live. The component starts receiving at ~2 s, so we buffer raw
  // bytes in RAM and retrieve them afterwards.
  void dump_boot_capture();

  // --- Anomaly capture ---
  // Logs the UNEXPECTED, not everything: new frame types, changes in fields we
  // believe to be constant, and any change in the alarm field. That makes it
  // nearly free to leave enabled permanently, and gives full context the day an
  // alarm fires or something else unforeseen happens.
  void dump_anomalies();
  // Raw hex logging of every validated frame, toggled at runtime without a
  // reflash.
  void set_raw_logging(bool on) {
    this->raw_logging_ = on;
    ESP_LOGI("flexit_sl4r", "Raw frame logging %s", on ? "ON" : "OFF");
  }
  bool get_raw_logging() const { return this->raw_logging_; }

#ifdef USE_SENSOR
  // Generic "exploration" sensors. The point is to expose any byte or float
  // slot in Home Assistant WITHOUT touching the C++ code, so the recorder
  // builds history we can correlate against later. That is cheaper than taking
  // a fresh uart-debug capture every time a hypothesis needs testing.
  void add_raw_status_sensor(uint8_t index, sensor::Sensor *sensor) {
    this->raw_status_sensors_.emplace_back(index, sensor);
  }
  void add_float_register_sensor(uint8_t type, uint8_t reg, uint8_t slot, sensor::Sensor *sensor) {
    this->float_register_sensors_.push_back({type, reg, slot, sensor});
  }
  // 16-bit words from the 0xC6 frames (the parameter tables). `index` is the
  // word index within the frame (0-13); `mode` selects the whole word or one
  // of its bytes - several parameters are stored as (min, max) BYTE PAIRS in a
  // single word. Publishes only on change: the frames repeat continuously in
  // the CS50's round, and the values are parameters/counters that almost never
  // move.
  void add_int_register_sensor(uint8_t bank, uint8_t reg, uint8_t index, uint8_t mode, sensor::Sensor *sensor) {
    this->int_register_sensors_.push_back({bank, reg, index, mode, sensor, -1});
  }
#endif

 protected:
  // --- Receive: general frame parser ---
  void handle_incoming_byte_(uint8_t byte);
  void dispatch_frame_();
  // Routes the frame to its handler. Returns false if nothing understood it -
  // which is what makes a frame worth reporting as an anomaly. Adding a decoder
  // therefore removes that frame from the anomaly log automatically, with no
  // whitelist to keep in step.
  bool decode_frame_();
  // The panel's boost command, register 0x14. Decoded so we can see the PANEL
  // start and stop boost, and so it stops being reported as an anomaly.
  void handle_boost_command_();
  void handle_float_frame_();
  void handle_int_frame_();
  void parse_and_publish_status_();

  // --- Transmit ---
  // Queues a complete frame (without checksum - that is computed at send time).
  void queue_state_frame_(uint8_t fan, uint8_t flag, uint8_t setpoint);
  // Builds the panel's own boost command (reg 0x14), mirroring the live duty
  // and status byte [15] and changing only [15]'s low nibble.
  void queue_boost_command_(bool on);
  void handle_panel_frame_();
  void publish_firmware_(bool controller);
  void queue_raw_frame_(std::vector<uint8_t> frame_without_checksum, uint8_t repeats = 1);
  void send_queued_frame_();

  static std::pair<uint8_t, uint8_t> checksum_(const uint8_t *data, size_t len);

  // --- Frame assembly ---
  // We collect from every 0xC3 and validate with length + checksum. If a frame
  // fails it is discarded and we look for the next 0xC3 - a 0xC3 inside a
  // payload then costs one wasted attempt rather than lasting desync.
  bool collecting_frame_{false};
  uint8_t frame_expected_{0};  // 0 = length not read yet
  std::vector<uint8_t> frame_;

  // Status reception (filled by the frame parser for TYPE_STATUS with LEN=22)
  std::array<uint8_t, STATUS_RAW_LENGTH> raw_status_{};
  uint8_t status_sync_193_{0};
  uint8_t status_sync_gap_{0};
  // The health signal rests on ANY valid frame, not just the status telegram.
  // The status telegram (`C1`/`len=22`) is one of many message types on the bus
  // and arrives less often than the rest - binding "communication OK" to that
  // one type made a perfectly healthy node report `off`. Frames are validated
  // with length + checksum, so a valid frame of any type is a strong sign of
  // life.
  uint32_t last_valid_frame_ms_{0};
  uint32_t last_valid_telegram_ms_{0};
  uint32_t last_rx_byte_ms_{0};    // for detecting a quiet bus before transmitting
  uint32_t command_queued_ms_{0};  // for giving up on a command that never fits
  // Counts frames that failed the checksum. These are discarded silently in the
  // parser (a 0xC3 inside a payload hits that branch quite normally), which
  // would also make real corruption invisible. Without this counter you cannot
  // measure whether OUR transmissions are damaging the CS50's traffic.
  uint32_t frames_discarded_{0};

  // Raw byte capture from boot. Filled once, stops when full.
  static constexpr size_t BOOT_CAPTURE_MAX = 6144;
  std::vector<uint8_t> boot_capture_;

  // --- Anomaly capture ---
  static constexpr size_t ANOMALY_MAX = 40;
  // Learning period: during the first seconds after boot the node learns the
  // installation's normal repertoire of frame types without reporting. After
  // that, any new signature is a real event. Better than counting a fixed
  // number - how many types a unit sends varies with its equipment.
  static constexpr uint32_t ANOMALY_LEARN_MS = 30000;
  struct Anomaly {
    uint32_t ms;
    const char *reason;
    std::vector<uint8_t> frame;
  };
  std::vector<Anomaly> anomalies_;
  uint32_t anomaly_count_{0};
  bool raw_logging_{false};

  // --- Frame census ---
  // Every distinct frame shape, with how many have arrived and when. This is
  // what the anomaly log cannot do on its own: it reports a signature the first
  // time only, so it can tell you something new appeared but never how often it
  // recurs. Reading "seen once, never again" out of it was a real mistake once.
  //
  // Bounded by construction - a unit emits a couple of dozen shapes - but
  // capped anyway so a stream of corrupted frames cannot grow it without limit.
  struct FrameSignature {
    uint64_t sig;
    uint32_t count;
    uint32_t first_ms;
    uint32_t last_ms;
    bool decoded;  // we have a handler for it, so it is normal traffic
  };
  static constexpr size_t SIGNATURE_MAX = 64;
  std::vector<FrameSignature> signatures_;
  // --- Parameter register watch ---
  // The status telegram has had per-field change detection from the start, but
  // the PARAMETER registers had none: only the handful wired to a sensor were
  // watched, and everything else could change unseen. Finding out whether a
  // parameter moved therefore meant capturing raw frames and diffing them in a
  // script afterwards - which is exactly how the boost investigation had to be
  // done, and it only works if you happen to be capturing at the time.
  //
  // A shadow copy per (bank, register) block closes that. Small and bounded:
  // a couple of banks times a few registers, 14 words each.
  struct ParamBlock {
    uint8_t bank;
    uint8_t reg;
    uint8_t words;
    uint16_t value[14];
  };
  static constexpr size_t PARAM_BLOCK_MAX = 16;
  std::vector<ParamBlock> param_shadow_;
  // Compares the current 0xC6 frame against the shadow and reports any word
  // that moved. Silent during the learning period.
  void watch_param_block_();

  // Computes the signature of the frame currently in frame_.
  uint64_t frame_signature_() const;
  // Records the current frame in the census. Returns true if this shape had
  // never been seen before.
  bool record_signature_(bool decoded);
  // Previous status telegram, for detecting changes in "constant" fields.
  std::array<uint8_t, STATUS_DATA_LENGTH> prev_status_{};
  bool have_prev_status_{false};
  void note_anomaly_(const char *reason);
  bool communication_ok_{false};

  // Last known raw values from the CS50 - these MUST be mirrored into outgoing
  // commands, or unsent fields get overwritten unintentionally.
  uint8_t last_raw_fan_level_{17};
  uint8_t last_raw_afterheat_{0};
  // Afterheater on/off. Read from the STATUS TELEGRAM ([6] bit7), which is
  // broadcast continuously - not from the panel frame, which is only sent on
  // change and can therefore be hours stale. That was a real bug: after a
  // restart the field sat at `false` until the panel happened to send
  // something, and since the value is mirrored into ALL our writes, the first
  // fan command after a restart switched the afterheater off behind the user's
  // back.
  bool afterheat_enabled_{false};
  bool have_afterheat_state_{false};
  // Set once a status telegram has been parsed, i.e. once raw_status_ holds
  // real bytes. Commands that mirror status fields must not run before this.
  bool have_status_{false};
  // millis() deadline for a boost WE started; 0 = no boost of ours pending.
  uint32_t boost_deadline_ms_{0};
  // millis() when we last asked for boost, while we wait to see it take effect;
  // 0 = nothing outstanding.
  uint32_t boost_request_ms_{0};
  // PRINCIPLE: everything we do not understand is mirrored from the panel's
  // last frame, so that a write changes only what we ACTUALLY mean to change.
  // We once hardcoded fields we believed constant and thereby switched off the
  // afterheater on every write.
  std::array<uint8_t, 8> panel_state_{{0x20, 0x0F, 0x00, 0x11, 0x00, 0x04, 0x00, 0x12}};
  bool have_panel_state_{false};
  uint8_t last_raw_heat_exchanger_temp_{20};
  // Supply air, mirrored from 0xC2 reg 0 slot 1 - the climate entity needs it
  // as "current temperature", and it arrives in a different frame than status.
  float last_supply_air_temp_{NAN};
#ifdef USE_CLIMATE
  FlexitClimate *ventilation_climate_{nullptr};
  void publish_climate_();
#endif

  // FIFO queue of complete frames (without checksum - applied at send time).
  // One frame is sent per quiet window, so a sequence of frames reaches the bus
  // in order with real arbitration between each. Necessary because the CI50
  // sends TWO frames on a boost press, not one.
  struct QueuedFrame {
    std::vector<uint8_t> bytes;
    uint8_t repeats;
  };
  std::vector<QueuedFrame> tx_queue_;


#ifdef USE_SENSOR
  std::vector<std::pair<uint8_t, sensor::Sensor *>> raw_status_sensors_;
  struct FloatRegisterSensor {
    uint8_t type;
    uint8_t reg;
    uint8_t slot;
    sensor::Sensor *sensor;
  };
  std::vector<FloatRegisterSensor> float_register_sensors_;
  struct IntRegisterSensor {
    uint8_t bank;
    uint8_t reg;
    uint8_t index;  // word index in the frame, 0-13
    uint8_t mode;   // 0 = whole word, 1 = high byte, 2 = low byte
    sensor::Sensor *sensor;
    int32_t last_value;  // -1 = never published (all real values are 0-65535)
  };
  std::vector<IntRegisterSensor> int_register_sensors_;
#endif

  // Builds the five header bytes of our source address. Bytes 3-4 are the
  // Fletcher checksum over [C3, node, 00] - verified exactly against node 1
  // (`01 00 C4 4B`) and node 4 (`04 00 C7 51`) in sniffed traffic.
  std::array<uint8_t, 5> source_header_() const {
    const uint8_t hdr[3] = {FRAME_START, this->source_node_, 0x00};
    const auto [s1, s2] = checksum_(hdr, 3);
    return {FRAME_START, this->source_node_, 0x00, s1, s2};
  }
  uint8_t source_node_{4};
  bool respond_to_polls_{false};
  uint32_t last_poll_to_us_ms_{0};
  bool enumerated_{false};
  std::array<uint8_t, 5> poll_window_{};
  void send_poll_response_();

  GPIOPin *flow_control_pin_{nullptr};
};

}  // namespace esphome::flexit_sl4r

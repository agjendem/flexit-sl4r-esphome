// SPDX-License-Identifier: MIT
// No ESPHome dependency - reusable on its own. See LICENSE.
#pragma once

// Pure protocol logic: no ESPHome, no hardware, no I/O.
//
// Everything here is arithmetic on bytes, which makes it the part of the
// integration that can be tested on a development machine against recorded
// captures - see tests/. It is also, not coincidentally, the part where the
// mistakes have been: a checksum window off by one byte, a "16-bit counter"
// that was two independent bytes, and a fan level read as a number when it is
// two nibbles. Keep this file free of dependencies so the tests stay cheap to
// run, and put anything with real logic in it rather than in the component.
//
// See PROTOCOL.md for how every offset and constant below was derived.

#include <cstddef>
#include <cstdint>
#include <utility>

namespace esphome::flexit_sl4r {

// --- Frame structure ---
//   C3 b1 b2 b3 b4 TYPE b6 LEN [LEN data bytes] CK1 CK2
// The checksum window is [5 .. 8+LEN). Validated against 23,708 sniffed bytes:
// 766 frames, zero false C3 hits. Length + checksum is a safe frame detector.
static constexpr uint8_t FRAME_START = 0xC3;
static constexpr uint8_t FRAME_HEADER_LENGTH = 8;   // C3 + 4 address bytes + TYPE + b6 + LEN
static constexpr uint8_t FRAME_LEN_OFFSET = 7;
static constexpr uint8_t FRAME_CHECKSUM_START = 5;  // the checksum covers from TYPE onwards
static constexpr uint8_t FRAME_MAX_PAYLOAD = 64;    // largest observed is 30

static constexpr uint8_t STATUS_DATA_LENGTH = 22;  // data bytes in the status telegram
static constexpr uint8_t STATUS_RAW_LENGTH = 25;   // data + 2 checksum + 1 unused

// --- Frame types ---
static constexpr uint8_t TYPE_IDLE = 0xC0;    // "nothing to report" - the reply we send ourselves
static constexpr uint8_t TYPE_STATUS = 0xC1;  // with LEN=22 this is the status telegram
static constexpr uint8_t TYPE_FLOAT = 0xC2;   // IEEE754 floats (live measurements)
static constexpr uint8_t TYPE_INT = 0xC6;     // 16-bit integers: parameter tables + clock storage
static constexpr uint8_t TYPE_PARAM = 0xC7;   // IEEE754 float parameters/limits

// --- Checksum ---
// Fletcher-style running pair, both bytes modulo 256. Reimplemented from
// Vongraven's Arduino sketch and verified numerically against captured frames
// rather than copied.
inline std::pair<uint8_t, uint8_t> checksum(const uint8_t *data, size_t len) {
  uint16_t sum1 = 0;
  uint16_t sum2 = 0;
  for (size_t i = 0; i < len; i++) {
    sum1 = static_cast<uint16_t>((sum1 + data[i]) & 0xFF);
    sum2 = static_cast<uint16_t>((sum2 + sum1) & 0xFF);
  }
  return {static_cast<uint8_t>(sum1), static_cast<uint8_t>(sum2)};
}

// True if `frame` is a complete, checksum-valid frame. `len` is the whole
// buffer including the C3 header and both checksum bytes.
//
// The off-by-one that cost the most: the window starts at TYPE (offset 5), not
// at the start of the frame. Reading it wrong still parses most frames, and
// fails every write.
inline bool frame_checksum_ok(const uint8_t *frame, size_t len) {
  if (len < FRAME_HEADER_LENGTH + 2u)
    return false;
  const size_t payload = frame[FRAME_LEN_OFFSET];
  const size_t data_end = FRAME_HEADER_LENGTH + payload;
  if (data_end + 2u > len)
    return false;
  const auto [s1, s2] = checksum(frame + FRAME_CHECKSUM_START, data_end - FRAME_CHECKSUM_START);
  return s1 == frame[data_end] && s2 == frame[data_end + 1];
}

// Total length of the frame starting at `frame`, or 0 if the buffer is too
// short to tell. Does not validate the checksum.
inline size_t frame_total_length(const uint8_t *frame, size_t len) {
  if (len < FRAME_LEN_OFFSET + 1u)
    return 0;
  return FRAME_HEADER_LENGTH + static_cast<size_t>(frame[FRAME_LEN_OFFSET]) + 2u;
}

// --- Status telegram field helpers ---

// payload[5] is TWO NIBBLES, not a number:
//   high = the level the unit is running now
//   low  = the level it returns to when boost ends
// 0x11/0x22/0x33 = normal, 0x31 = boost (running 3, will fall back to 1).
// The original reading divided by 17, which rejects 0x31 outright and would
// have given 2 for it had it not.
inline uint8_t fan_level_running(uint8_t raw) { return static_cast<uint8_t>(raw >> 4); }
inline uint8_t fan_level_return(uint8_t raw) { return static_cast<uint8_t>(raw & 0x0F); }
inline bool fan_level_valid(uint8_t raw) {
  const uint8_t r = fan_level_running(raw), b = fan_level_return(raw);
  return r >= 1 && r <= 3 && b >= 1 && b <= 3;
}

// The nibbles disagreeing is NOT the same thing as boost, and treating it as
// such was a bug this integration shipped. See PROTOCOL.md section 5.8: on
// 2026-08-21 the byte sat at 0x12 - a level BELOW the fallback level - for
// forty minutes while [6] bit0 said plainly that no boost was running. Use
// this to ask "are the two nibbles the same", nothing more.
inline bool fan_level_nibbles_disagree(uint8_t raw) { return fan_level_running(raw) != fan_level_return(raw); }

// high < low: the unit is holding a level change it accepted but has not
// carried out. In command form the byte is (from, to) - see PROTOCOL.md
// section 7.3 - and a stalled change leaves that shape sitting in the status
// telegram. Boost is the opposite ordering, high > low.
inline bool fan_level_change_pending(uint8_t raw) { return fan_level_running(raw) < fan_level_return(raw); }

// --- payload[2]: fan relay feedback ---
// Two one-hot groups, each naming the tap a fan is wired to right now:
//   bits 7/6/5 = SUPPLY fan on tap 1/2/3
//   bits 4/3/2 = EXTRACT fan on tap 1/2/3
// (bit1 = the afterheater is drawing power, bit0 = the rotor is turning.)
// Returns 0 for an empty group or one with several bits set - neither is a tap
// the unit can physically be on, and a transformer-tapped fan is on exactly
// one.
inline uint8_t relay_group_level(uint8_t group) {
  switch (group & 0x07) {
    case 0x04:
      return 1;
    case 0x02:
      return 2;
    case 0x01:
      return 3;
    default:
      return 0;
  }
}
inline uint8_t fan_relay_supply_level(uint8_t status2) { return relay_group_level(static_cast<uint8_t>(status2 >> 5)); }
inline uint8_t fan_relay_extract_level(uint8_t status2) { return relay_group_level(static_cast<uint8_t>(status2 >> 2)); }

// Both fans belong on the SAME tap: balanced ventilation is the whole point of
// the unit, and running the supply and extract fans at different speeds leaves
// the house permanently over- or under-pressurised.
//
// This is deliberately read from [2] and not from [5]. During a genuine boost
// the two relay groups AGREE - both read tap 3 - while [5]'s nibbles disagree
// (0x31). [2] is relay feedback: it reports where the fans actually are, so it
// separates "boosting" from "out of balance", which [5] cannot.
//
// Returns true only when both groups decode, so an all-zero telegram during
// start-up is not mistaken for a fault.
inline bool fan_relays_balanced(uint8_t status2) {
  const uint8_t s = fan_relay_supply_level(status2);
  return s != 0 && s == fan_relay_extract_level(status2);
}
inline bool fan_relays_known(uint8_t status2) {
  return fan_relay_supply_level(status2) != 0 && fan_relay_extract_level(status2) != 0;
}

// payload[6] carries two independent bits. Three earlier readings of this byte
// were wrong; these two are settled by controlled experiment.
static constexpr uint8_t STATUS_BOOST_ACTIVE = 0x01;
static constexpr uint8_t STATUS_AFTERHEAT_ENABLED = 0x80;

// The authority on whether boost is running. The unit says so itself here, and
// this bit has been settled by controlled experiment; the [5] nibbles have
// not, and disagree with it in at least one real state (PROTOCOL.md 5.8).
inline bool status_boost_active(uint8_t status6) { return (status6 & STATUS_BOOST_ACTIVE) != 0; }

// --- Boost command (register 0x14) ---
// data[1] carries status byte [15] with only its LOW NIBBLE changed: 3 asks for
// boost, 0 cancels. The high nibble is mirrored because we do not know what it
// means - writing a guess there is exactly the class of mistake that switched
// the afterheater off three times.
inline uint8_t boost_command_flags(uint8_t status15, bool on) {
  return static_cast<uint8_t>((status15 & 0xF0) | (on ? 0x03 : 0x00));
}

// --- 0xC6 parameter words ---
// Big endian, unlike the little-endian floats. `00 19` is 25, not 6400.
inline uint16_t param_word(const uint8_t *data, size_t index) {
  return static_cast<uint16_t>((data[index * 2] << 8) | data[index * 2 + 1]);
}
inline uint8_t param_word_high(const uint8_t *data, size_t index) { return data[index * 2]; }
inline uint8_t param_word_low(const uint8_t *data, size_t index) { return data[index * 2 + 1]; }
// Number of 16-bit words a 0xC6 frame carries, given its LEN byte (2 bytes of
// bank/register precede the words).
inline size_t param_word_count(uint8_t len_byte) { return len_byte < 2 ? 0 : (len_byte - 2u) / 2u; }

}  // namespace esphome::flexit_sl4r

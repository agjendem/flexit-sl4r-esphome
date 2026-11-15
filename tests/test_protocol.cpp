// SPDX-License-Identifier: MIT
// No ESPHome dependency - reusable on its own. See LICENSE.
// Host tests for the pure protocol logic in components/flexit_sl4r/protocol.h.
//
// No test framework on purpose: this needs to build with nothing but a C++
// compiler, so that "can I run the tests" never becomes a reason not to.
//
// Two kinds of test here. The first are fixed vectors - frames copied out of
// real bus captures, with their real checksums - which pin the arithmetic. The
// second replay whole recorded captures through the frame scanner and assert
// properties we have documented in PROTOCOL.md. The second kind is what would
// have caught the mistakes this project actually made; the first kind is what
// keeps them caught.

#include "../components/flexit_sl4r/protocol.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace esphome::flexit_sl4r;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, ...)                             \
  do {                                               \
    g_checks++;                                      \
    if (!(cond)) {                                   \
      g_failures++;                                  \
      std::printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
      std::printf(__VA_ARGS__);                      \
      std::printf("\n");                             \
    }                                                \
  } while (0)

static void section(const char *name) { std::printf("\n== %s ==\n", name); }

// ---------------------------------------------------------------------------
// Checksum, against frames captured from the bus with their real checksums.
// ---------------------------------------------------------------------------
static void test_checksum() {
  section("checksum");

  struct Vector {
    const char *what;
    std::vector<uint8_t> body;  // TYPE onwards, excluding the checksum
    uint8_t ck1, ck2;
  };
  // Every one of these was read off the wire, not computed by this code.
  const Vector vectors[] = {
      {"boost on (panel)", {0xC1, 0x04, 0x04, 0x20, 0x14, 0x31, 0x33}, 0x61, 0xC4},
      {"boost off (panel)", {0xC1, 0x04, 0x04, 0x20, 0x14, 0x64, 0x30}, 0x91, 0x27},
      {"panel state, level 1", {0xC1, 0x04, 0x08, 0x20, 0x0F, 0x00, 0x11, 0x80, 0x04, 0x00, 0x12}, 0xA3, 0x97},
      {"panel state, button bit", {0xC1, 0x04, 0x08, 0x20, 0x0F, 0x00, 0x11, 0x81, 0x04, 0x00, 0x12}, 0xA4, 0x9B},
      {"panel state, boosting", {0xC1, 0x04, 0x08, 0x20, 0x0F, 0x00, 0x31, 0x80, 0x04, 0x00, 0x12}, 0xC3, 0x37},
      // The CI50's idle reply, 414 occurrences in the 2026-08-13 capture. Note
      // this vector replaces one whose checksum I had written from memory
      // rather than read off the wire; these tests caught it on their first
      // run, which is a fair summary of why they exist.
      {"CI50 idle reply", {0xC0, 0x04, 0x02, 0x22, 0x00}, 0xE8, 0x1A},
      {"CS50 idle reply", {0xC0, 0x01, 0x02, 0x20, 0x00}, 0xE3, 0x0A},
  };
  for (const auto &v : vectors) {
    const auto [s1, s2] = checksum(v.body.data(), v.body.size());
    CHECK(s1 == v.ck1 && s2 == v.ck2, "%s: expected %02X %02X, got %02X %02X", v.what, v.ck1, v.ck2, s1, s2);
  }

  // Order matters: a Fletcher-style sum must not be a plain sum.
  const uint8_t ab[] = {0x12, 0x34};
  const uint8_t ba[] = {0x34, 0x12};
  CHECK(checksum(ab, 2).second != checksum(ba, 2).second, "checksum is order-independent - that cannot be right");

  // Empty input is defined, not undefined.
  const auto empty = checksum(nullptr, 0);
  CHECK(empty.first == 0 && empty.second == 0, "empty checksum should be 0,0");
}

// ---------------------------------------------------------------------------
// Frame validation, including the off-by-one that cost this project days.
// ---------------------------------------------------------------------------
static void test_frame_validation() {
  section("frame validation");

  // A complete captured frame: poll header + reply + checksum.
  std::vector<uint8_t> frame = {0xC3, 0x04, 0x00, 0xC7, 0x51, 0xC1, 0x04, 0x04, 0x20, 0x14, 0x31, 0x33, 0x61, 0xC4};
  CHECK(frame_checksum_ok(frame.data(), frame.size()), "a known-good frame failed validation");
  CHECK(frame_total_length(frame.data(), frame.size()) == frame.size(), "total length disagrees with the real frame");

  // Corrupt one payload byte: must fail.
  auto corrupt = frame;
  corrupt[10] ^= 0x01;
  CHECK(!frame_checksum_ok(corrupt.data(), corrupt.size()), "a corrupted payload byte passed validation");

  // Corrupt a byte INSIDE the checksum window but before the payload - this is
  // the region an off-by-one would wrongly exclude. If the window started at
  // the wrong offset, this mutation would go unnoticed.
  auto shifted = frame;
  shifted[FRAME_CHECKSUM_START] ^= 0x01;  // the TYPE byte
  CHECK(!frame_checksum_ok(shifted.data(), shifted.size()), "TYPE is outside the checksum window - window is wrong");

  // Bytes BEFORE the window are not covered, by design: they belong to the poll.
  auto header = frame;
  header[1] ^= 0xFF;
  CHECK(frame_checksum_ok(header.data(), header.size()), "the poll header should not be part of the checksum");

  // Truncated buffers must be rejected rather than read out of bounds.
  for (size_t n = 0; n < frame.size(); n++)
    CHECK(!frame_checksum_ok(frame.data(), n), "a %zu-byte truncation was accepted as a whole frame", n);
}

// ---------------------------------------------------------------------------
// The fan level nibbles. The original reading divided by 17 and rejected boost.
// ---------------------------------------------------------------------------
static void test_fan_level() {
  section("fan level nibbles");

  struct Case {
    uint8_t raw;
    uint8_t running, ret;
    bool boost;
  };
  const Case cases[] = {
      {0x11, 1, 1, false}, {0x22, 2, 2, false}, {0x33, 3, 3, false},
      {0x31, 3, 1, true},  // boost from level 1 - captured repeatedly
      {0x32, 3, 2, true},  // boost from level 2
  };
  for (const auto &c : cases) {
    CHECK(fan_level_running(c.raw) == c.running, "0x%02X running: expected %u, got %u", c.raw, c.running,
          fan_level_running(c.raw));
    CHECK(fan_level_return(c.raw) == c.ret, "0x%02X return: expected %u, got %u", c.raw, c.ret, fan_level_return(c.raw));
    CHECK(fan_level_nibbles_disagree(c.raw) == c.boost, "0x%02X nibbles disagree: expected %d", c.raw, c.boost);
    CHECK(fan_level_valid(c.raw), "0x%02X should be a valid fan level", c.raw);
  }

  // The specific regression: 0x31 must not be read as a number.
  CHECK(fan_level_running(0x31) != 0x31 / 17, "0x31 is being divided by 17 again");

  // Junk must be rejected rather than silently clamped.
  CHECK(!fan_level_valid(0x00), "0x00 accepted as a fan level");
  CHECK(!fan_level_valid(0x44), "0x44 accepted as a fan level");
  CHECK(!fan_level_valid(0x10), "0x10 accepted as a fan level");

  // 0x12, observed live on 2026-08-21: a level change accepted and never
  // carried out. The nibbles disagree, and it is emphatically NOT boost.
  CHECK(fan_level_valid(0x12), "0x12 is a legal pair of nibbles");
  CHECK(fan_level_nibbles_disagree(0x12), "0x12 nibbles do disagree");
  CHECK(fan_level_change_pending(0x12), "0x12 is a stalled level change");
  CHECK(!fan_level_change_pending(0x31), "boost must not read as a stalled change");
  CHECK(!fan_level_change_pending(0x22), "a settled level must not read as a stalled change");
}

// ---------------------------------------------------------------------------
// payload[2]: which tap each fan is on, and the imbalance that hid there.
// ---------------------------------------------------------------------------
static void test_fan_relays() {
  section("fan relay feedback");

  // Every value seen across both archived captures, plus the fault state.
  struct Case {
    uint8_t raw;
    uint8_t supply, extract;
    bool balanced;
  };
  const Case cases[] = {
      {0x90, 1, 1, true},   // level 1, rotor still
      {0x48, 2, 2, true},   // level 2
      {0x24, 3, 3, true},   // level 3 - and the value seen during boost
      {0x91, 1, 1, true},   // level 1 with the rotor turning (bit0)
      {0x26, 3, 3, true},   // level 3, rotor + afterheater drawing power
      {0x89, 1, 2, false},  // 2026-08-21: supply on tap 1, extract on tap 2
  };
  for (const auto &c : cases) {
    CHECK(fan_relay_supply_level(c.raw) == c.supply, "0x%02X supply tap: expected %u, got %u", c.raw, c.supply,
          fan_relay_supply_level(c.raw));
    CHECK(fan_relay_extract_level(c.raw) == c.extract, "0x%02X extract tap: expected %u, got %u", c.raw, c.extract,
          fan_relay_extract_level(c.raw));
    CHECK(fan_relays_known(c.raw), "0x%02X should decode both groups", c.raw);
    CHECK(fan_relays_balanced(c.raw) == c.balanced, "0x%02X balanced: expected %d", c.raw, c.balanced);
  }

  // The distinction the whole alarm rests on: during boost the relays AGREE
  // while the fan level nibbles disagree. Reading balance off [5] would have
  // called every boost a fault, and reading boost off [5] called this fault a
  // boost.
  CHECK(fan_relays_balanced(0x24) && fan_level_nibbles_disagree(0x31), "boost: relays agree, nibbles do not");

  // Nothing running yet, or bits we cannot make sense of: report "unknown"
  // rather than inventing a fault. A power cut must not raise an alarm.
  CHECK(!fan_relays_known(0x00), "an all-zero telegram must not decode as taps");
  CHECK(!fan_relays_balanced(0x00), "an all-zero telegram must not read as balanced");
  CHECK(fan_relay_supply_level(0xE0) == 0, "several bits in one group is not a tap");
}

// ---------------------------------------------------------------------------
// The boost command must mirror what it does not understand.
// ---------------------------------------------------------------------------
static void test_boost_command() {
  section("boost command");

  CHECK(boost_command_flags(0x30, true) == 0x33, "on from 0x30 should be 0x33");
  CHECK(boost_command_flags(0x33, false) == 0x30, "off from 0x33 should be 0x30");

  // The high nibble is not ours. Whatever it holds must survive untouched -
  // this is the mirroring rule, and breaking it is what switched the
  // afterheater off three separate times.
  for (uint8_t high = 0; high <= 0x0F; high++) {
    const uint8_t status15 = static_cast<uint8_t>((high << 4) | 0x03);
    CHECK((boost_command_flags(status15, true) & 0xF0) == (high << 4), "high nibble 0x%X was not mirrored (on)", high);
    CHECK((boost_command_flags(status15, false) & 0xF0) == (high << 4), "high nibble 0x%X was not mirrored (off)", high);
  }

  // Captured evidence: the panel sent 0x33 to start and 0x30 to cancel, with
  // the same high nibble on both.
  CHECK(boost_command_flags(0x33, true) == 0x33, "re-requesting boost should be idempotent");
}

// ---------------------------------------------------------------------------
// Boost timing: every visible boost has a deadline, and a cancel is not done
// until the status says so. The first case is 2026-11-14, when a boost we
// started ran for 7.5 hours.
// ---------------------------------------------------------------------------
static void test_boost_timer() {
  section("boost timer");
  using A = BoostTimer::Action;
  const uint32_t MIN = 60UL * 1000UL;

  {
    // Our request waits for our poll slot, so the telegram that arrives in the
    // meantime still shows no boost. That must not disarm the deadline.
    BoostTimer t;
    t.on_request(0);
    t.on_status(400, false);
    CHECK(t.armed(), "a no-boost telegram before our request took effect disarmed the deadline");
    t.on_status(1600, true);
    CHECK(t.poll(30 * MIN - 1, true) == A::NONE, "cancelled before the period was up");
    CHECK(t.poll(30 * MIN, true) == A::CANCEL, "no cancel at the end of our 30 minutes");
    CHECK(t.cancel_attempts() == 1, "first cancel should count as attempt 1, got %u", t.cancel_attempts());
    t.on_status(30 * MIN + 1200, false);
    CHECK(!t.armed(), "still armed after the status confirmed the boost ended");
    CHECK(t.poll(31 * MIN, false) == A::NONE, "acted after the boost had ended");
  }
  {
    // A cancel the unit ignores is sent again: 15 s apart at first, then 5 min.
    BoostTimer t;
    t.on_request(0);
    t.on_status(1000, true);
    uint32_t now = 30 * MIN;
    CHECK(t.poll(now, true) == A::CANCEL, "no first cancel");
    for (uint8_t attempt = 2; attempt <= BOOST_CANCEL_FAST_ATTEMPTS; attempt++) {
      t.on_status(now + 1000, true);  // still boosting
      CHECK(t.poll(now + BOOST_CANCEL_RETRY_MS - 1, true) == A::NONE, "retry %u came early", attempt);
      now += BOOST_CANCEL_RETRY_MS;
      CHECK(t.poll(now, true) == A::CANCEL, "no fast retry %u", attempt);
    }
    CHECK(t.poll(now + BOOST_CANCEL_RETRY_MS, true) == A::NONE, "fast retries did not stop after %u",
          BOOST_CANCEL_FAST_ATTEMPTS);
    now += BOOST_CANCEL_RETRY_SLOW_MS;
    CHECK(t.poll(now, true) == A::CANCEL, "no slow retry");
    now += BOOST_CANCEL_RETRY_SLOW_MS;
    CHECK(t.poll(now, true) == A::CANCEL, "slow retries must go on for as long as the boost does");
    t.on_status(now + 1000, false);
    CHECK(!t.armed() && t.cancel_attempts() == 0, "a confirmed end must reset the retries");
  }
  {
    // A request the unit discards is reported, and leaves nothing armed.
    BoostTimer t;
    t.on_request(0);
    t.on_status(1200, false);
    t.on_status(2400, false);
    CHECK(t.poll(BOOST_CONFIRM_MS, false) == A::NONE, "reported before the confirm window was over");
    CHECK(t.poll(BOOST_CONFIRM_MS + 1, false) == A::NOT_STARTED, "a discarded request was not reported");
    CHECK(!t.armed(), "armed after a discarded request");
    CHECK(t.poll(40 * MIN, false) == A::NONE, "acted on a boost that never started");
  }
  {
    // A boost we did not start - the panel's, or one we lost track of, e.g.
    // across our own reboot - gets the backstop, never nothing.
    BoostTimer t;
    t.on_status(0, true);
    CHECK(t.armed(), "a boost without a deadline was left without one");
    CHECK(t.poll(91 * MIN, true) == A::NONE, "backstop cut into the panel's 90 minutes");
    CHECK(t.poll(BOOST_BACKSTOP_MS, true) == A::CANCEL, "no cancel at the backstop");
  }
  {
    // A panel press during our boost hands it to the panel: no cancel at our
    // 30 minutes, but still the backstop.
    BoostTimer t;
    t.on_request(0);
    t.on_status(1000, true);
    t.on_panel_command(10 * MIN, true);
    CHECK(t.poll(30 * MIN, true) == A::NONE, "cut a boost the panel had taken over");
    CHECK(t.poll(10 * MIN + BOOST_BACKSTOP_MS, true) == A::CANCEL, "no backstop after a panel press");
  }
  {
    // A panel cancel the unit ignores: the boost carries on, so it gets the
    // backstop again rather than running free.
    BoostTimer t;
    t.on_status(0, true);
    t.on_panel_command(30 * MIN, false);
    t.on_status(30 * MIN + 1000, true);
    CHECK(t.armed(), "a boost that outlived the panel's cancel was left without a deadline");
  }
  {
    // Turning the switch off when nothing runs must not start a retry loop.
    BoostTimer t;
    t.on_cancel_sent(0);
    t.on_status(1000, false);
    CHECK(t.poll(BOOST_CANCEL_RETRY_MS, false) == A::NONE, "retried a cancel the status had confirmed");
    // ...and one the unit ignores is retried like any other.
    BoostTimer u;
    u.on_status(0, true);
    u.on_cancel_sent(1000);
    u.on_status(2000, true);
    CHECK(u.poll(1000 + BOOST_CANCEL_RETRY_MS, true) == A::CANCEL, "an ignored manual cancel was not retried");
    CHECK(u.cancel_attempts() == 2, "manual cancel + retry should be 2 attempts, got %u", u.cancel_attempts());
  }
  {
    // millis() wraps after 49 days; the deadline must not care.
    BoostTimer t;
    const uint32_t start = 0xFFFFFFFFUL - 10UL * MIN;
    t.on_request(start);
    t.on_status(start + 1000, true);
    CHECK(t.poll(start + 20 * MIN, true) == A::NONE, "cancelled early across the millis() wrap");
    CHECK(t.poll(start + 30 * MIN, true) == A::CANCEL, "missed the deadline across the millis() wrap");
  }
}

// ---------------------------------------------------------------------------
// 0xC6 words are BIG endian, and adjacent bytes are not always one number.
// ---------------------------------------------------------------------------
static void test_param_words() {
  section("parameter words");

  const uint8_t data[] = {0x00, 0x19, 0x82, 0xF2, 0x00, 0x0C};
  CHECK(param_word(data, 0) == 25, "00 19 should be 25 (big endian), got %u", param_word(data, 0));
  CHECK(param_word(data, 0) != 0x1900, "words are being read little endian");
  CHECK(param_word_high(data, 1) == 0x82, "high byte wrong");
  CHECK(param_word_low(data, 1) == 0xF2, "low byte wrong");

  // The "hour counter" that never was: 0x82F2 -> 0x000C is not a 16-bit
  // counter wrapping, it is two independent bytes. The helpers must let a
  // caller see them separately, which is the whole reason they exist.
  CHECK(param_word_high(data, 2) == 0x00 && param_word_low(data, 2) == 0x0C, "byte split wrong");

  // Word count from the LEN byte: two bytes of bank/register come first.
  CHECK(param_word_count(0x1E) == 14, "LEN 0x1E should give 14 words, got %zu", param_word_count(0x1E));
  CHECK(param_word_count(0x16) == 10, "LEN 0x16 should give 10 words, got %zu", param_word_count(0x16));
  CHECK(param_word_count(0x02) == 0, "LEN 2 carries no words");
  CHECK(param_word_count(0x00) == 0, "LEN 0 must not underflow");
  CHECK(param_word_count(0x01) == 0, "LEN 1 must not underflow");
}

// ---------------------------------------------------------------------------
// Replay real captures through the frame scanner.
// ---------------------------------------------------------------------------
struct CaptureStats {
  size_t bytes = 0;
  size_t frames = 0;
  size_t status_telegrams = 0;
  size_t resync_skips = 0;
  uint8_t setpoint_min = 0xFF, setpoint_max = 0;
  bool saw_nibbles_disagreeing = false;
  bool all_fan_levels_valid = true;
  // Both were true across every archived telegram before 2026-08-21. Asserting
  // them here is what makes "never seen before" a measurement rather than a
  // recollection - and what will catch it if a future capture disagrees.
  bool all_fan_duties_equal = true;
  bool all_relays_balanced = true;
};

static bool read_hex_file(const char *path, std::vector<uint8_t> &out) {
  std::ifstream in(path);
  if (!in)
    return false;
  std::string tok;
  while (in >> tok) {
    if (tok.size() != 2)
      continue;
    out.push_back(static_cast<uint8_t>(std::strtoul(tok.c_str(), nullptr, 16)));
  }
  return true;
}

// The same rule the firmware uses: find 0xC3, trust LEN, verify the checksum,
// and on failure advance a single byte rather than the whole claimed length.
// A 0xC3 inside a payload is normal, so resynchronisation has to be cheap.
static CaptureStats scan(const std::vector<uint8_t> &data) {
  CaptureStats st;
  st.bytes = data.size();
  size_t i = 0;
  while (i < data.size()) {
    if (data[i] != FRAME_START) {
      i++;
      continue;
    }
    const size_t avail = data.size() - i;
    const size_t total = frame_total_length(data.data() + i, avail);
    if (total == 0 || total > avail || !frame_checksum_ok(data.data() + i, avail)) {
      st.resync_skips++;
      i++;
      continue;
    }
    st.frames++;
    const uint8_t *f = data.data() + i;
    if (f[FRAME_CHECKSUM_START] == TYPE_STATUS && f[FRAME_LEN_OFFSET] == STATUS_DATA_LENGTH) {
      st.status_telegrams++;
      const uint8_t fan = f[FRAME_HEADER_LENGTH + 5];
      const uint8_t setpoint = f[FRAME_HEADER_LENGTH + 9];
      if (!fan_level_valid(fan))
        st.all_fan_levels_valid = false;
      if (fan_level_nibbles_disagree(fan))
        st.saw_nibbles_disagreeing = true;
      if (f[FRAME_HEADER_LENGTH + 13] != f[FRAME_HEADER_LENGTH + 14])
        st.all_fan_duties_equal = false;
      const uint8_t relays = f[FRAME_HEADER_LENGTH + 2];
      if (fan_relays_known(relays) && !fan_relays_balanced(relays))
        st.all_relays_balanced = false;
      if (setpoint >= 10 && setpoint <= 30) {
        if (setpoint < st.setpoint_min)
          st.setpoint_min = setpoint;
        if (setpoint > st.setpoint_max)
          st.setpoint_max = setpoint;
      }
    }
    i += total;
  }
  return st;
}

static void test_capture(const char *path, bool expect_boost, uint8_t expect_setpoint_low,
                         uint8_t expect_setpoint_high, bool expect_balanced = true) {
  section(path);
  std::vector<uint8_t> data;
  if (!read_hex_file(path, data)) {
    std::printf("  SKIP (cannot open)\n");
    return;
  }
  const CaptureStats st = scan(data);
  std::printf("  %zu bytes, %zu frames (%zu status), %zu resync skips\n", st.bytes, st.frames, st.status_telegrams,
              st.resync_skips);
  if (st.setpoint_max != 0)
    std::printf("  setpoint range %u..%u C\n", st.setpoint_min, st.setpoint_max);

  CHECK(st.frames > 100, "only %zu frames found - the scanner is not finding the structure", st.frames);
  CHECK(st.status_telegrams > 10, "only %zu status telegrams", st.status_telegrams);
  CHECK(st.all_fan_levels_valid, "a status telegram carried an invalid fan level");
  if (expect_balanced) {
    CHECK(st.all_fan_duties_equal, "the two fan duties differed - see PROTOCOL.md 5.8");
    CHECK(st.all_relays_balanced, "the fan relay groups disagreed - see PROTOCOL.md 5.8");
  } else {
    // The fault capture. Asserting that the decoder still SEES it guards the
    // detection logic against being quietly weakened later.
    CHECK(!st.all_fan_duties_equal, "expected unequal fan duties in the fault capture");
    CHECK(!st.all_relays_balanced, "expected the relay groups to disagree in the fault capture");
  }

  // Frames should dominate the stream. If resynchronisation were broken we
  // would be skipping far more bytes than we consume.
  const double consumed = static_cast<double>(st.bytes - st.resync_skips) / static_cast<double>(st.bytes);
  CHECK(consumed > 0.8, "only %.0f%% of the stream was consumed as frames", consumed * 100);

  if (expect_boost)
    CHECK(st.saw_nibbles_disagreeing, "expected the fan level nibbles to differ somewhere in this capture");
  if (expect_setpoint_low != 0) {
    CHECK(st.setpoint_min <= expect_setpoint_low, "setpoint never reached %u (min was %u)", expect_setpoint_low,
          st.setpoint_min);
    CHECK(st.setpoint_max >= expect_setpoint_high, "setpoint never reached %u (max was %u)", expect_setpoint_high,
          st.setpoint_max);
  }
}

int main(int argc, char **argv) {
  test_checksum();
  test_frame_validation();
  test_fan_level();
  test_fan_relays();
  test_boost_command();
  test_boost_timer();
  test_param_words();

  // The panel sequence capture documents a setpoint sweep 17->25->15 and a
  // boost press; the supply-air capture is a quieter stretch. Both are asserted
  // against what PROTOCOL.md claims they contain.
  if (argc > 1)
    test_capture(argv[1], true, 15, 25);
  if (argc > 2)
    test_capture(argv[2], false, 0, 0);
  // The 2026-08-21 fault: supply fan on tap 1, extract on tap 2, for the whole
  // capture. Setpoint held at 25. See PROTOCOL.md 5.8.
  if (argc > 3)
    test_capture(argv[3], true, 25, 25, false);

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

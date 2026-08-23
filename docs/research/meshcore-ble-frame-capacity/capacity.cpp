// The executable half of ../MESHCORE_BLE_FRAME_CAPACITY.md.
//
// Compiles upstream's real `BleFrameSizing.h`, `BaseSerialInterface.h` and
// `MultiSerialInterface.h` — unmodified, at whichever revision build.sh names —
// and drives them with the four numbers the issue asked to be told apart:
//
//   1. the protocol/buffer maximum      MAX_FRAME_SIZE, 176
//   2. the ATT notification payload     negotiated MTU - 3
//   3. the effective frame ceiling      min(the above two), across enabled sinks
//   4. the application chunk payload    (3) minus the builder's own header
//
// Nothing here is part of an Attadipa build, and nothing here is an Attadipa
// transport adapter. It is evidence that the arithmetic in the document was
// re-derived from upstream's own code rather than copied out of a commit
// message. See the README.
//
// Upstream code is MIT (OffbandMesh/meshcore-firmware, license.txt); this file
// is Attadipa's and is under the repository's own licence.

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "helpers/BleFrameSizing.h"
#include "helpers/MultiSerialInterface.h"

// ---------------------------------------------------------------- reporting

static int checks = 0;
static int failures = 0;
static int hazards = 0;

static void check(const char *what, long got, long want) {
  ++checks;
  const bool ok = (got == want);
  if (!ok) ++failures;
  printf("  %s %-56s got %ld, want %ld\n", ok ? "[ok]  " : "[FAIL]", what, got, want);
}

// An observation upstream's code makes that an Attadipa adapter must not
// inherit. Reported deliberately without failing the run: the harness asserts
// what upstream *does*, and the document argues about what we should do.
static void hazard(const char *what, long got, const char *why) {
  ++checks;
  ++hazards;
  printf("  [HAZ] %-56s got %ld\n        %s\n", what, got, why);
}

// ------------------------------------------------------------- fake sinks

// A transport that does NOT override maxFrameSize(), so it reports whatever
// BaseSerialInterface's default says — which is exactly what serial and TCP do.
// They never reach deliverableFrame() at all.
class CountingInterface : public BaseSerialInterface {
public:
  void enable() override { _enabled = true; }
  void disable() override { _enabled = false; }
  bool isEnabled() const override { return _enabled; }
  bool isConnected() const override { return _enabled; }
  bool isWriteBusy() const override { return false; }

  size_t writeFrame(const uint8_t src[], size_t len) override {
    (void)src;
    bytes_written += len;
    ++frames_written;
    return len;
  }
  size_t checkRecvFrame(uint8_t dest[]) override {
    (void)dest;
    return 0;
  }

  size_t bytes_written = 0;
  size_t frames_written = 0;

private:
  bool _enabled = false;
};

// A transport that reports a link-derived frame size, as the two BLE interfaces
// do. It can be disabled after the wrapper has enabled everything — which is the
// only reachable way to hold one disabled, because MultiSerialInterface::enable()
// enables every registered interface unconditionally.
class MtuInterface : public CountingInterface {
public:
  explicit MtuInterface(size_t frame_size) : _frame_size(frame_size) {}
  size_t maxFrameSize() const override { return _frame_size; }

private:
  size_t _frame_size;
};

// --------------------------------------------------------------- arithmetic

// A producer chunks `total` bytes at `payload` per chunk and announces frames of
// `announced_frame`; the link delivers at most `deliverable_frame` and silently
// drops the rest of each frame. Returns how many chunks it sends and how many
// bytes actually arrive.
struct Transfer {
  long chunks;
  long delivered;
};

static Transfer transfer(long total, long payload, long announced_frame, long deliverable_frame) {
  const long header = announced_frame - payload;
  Transfer t{0, 0};
  long left = total;
  while (left > 0) {
    const long in_this_chunk = left < payload ? left : payload;
    const long frame = in_this_chunk + header;
    const long on_wire = frame < deliverable_frame ? frame : deliverable_frame;
    const long arrived = on_wire - header;
    t.delivered += arrived > 0 ? arrived : 0;
    left -= in_this_chunk;
    ++t.chunks;
  }
  return t;
}

// ------------------------------------------------------------------- cases

static size_t case_att_arithmetic() {
  printf("\n1. ATT arithmetic — one notification carries one frame\n");

  // Bound 2 of deliverableFrame() differs between the two revisions this builds
  // against, so both answers are read out of upstream's own function rather than
  // asserted against a number this file picked.
  const size_t esp32 = ble_frame::deliverableFrame(ble_frame::effectiveMtu(176, 176), MAX_FRAME_SIZE);
  const size_t nrf52 = ble_frame::deliverableFrame(ble_frame::effectiveMtu(247, 247), MAX_FRAME_SIZE);

  printf("  ESP32, negotiated MTU 176 -> deliverable frame %zu, chunk payload %zu\n",
         esp32, esp32 - 2);
  printf("  nRF52, negotiated MTU 247 -> deliverable frame %zu, chunk payload %zu\n",
         nrf52, nrf52 - 2);

  check("ESP32 at MTU 176: deliverable frame", (long)esp32, 173);
  check("ESP32 at MTU 176: payload after a 2-byte header", (long)esp32 - 2, 171);

  // The peer's number does not raise what the link carries, in either direction.
  check("peer reports 517, we configured 176 -> effective MTU",
        ble_frame::effectiveMtu(517, 176), 176);
  check("peer reports 176, we configured 256 -> effective MTU",
        ble_frame::effectiveMtu(176, 256), 176);
  check("a reported MTU below the 23-byte spec floor is refused",
        ble_frame::effectiveMtu(0, 517), 23);

  return esp32;
}

static void case_field_transfers(size_t deliverable, long header) {
  printf("\n2. The three field reports and the bench capture\n");

  const long payload = (long)deliverable - header;
  const long buffer_payload = MAX_FRAME_SIZE - header;
  printf("  chunk payload after a %ld-byte header: %ld correct, %ld if sized at the buffer\n",
         header, payload, buffer_payload);

  struct Case {
    const char *who;
    long total;
    long want_chunks;         // correctly sized
    long want_buffer_chunks;  // sized at MAX_FRAME_SIZE, as the field reports show
    long want_short;          // bytes lost, as the field reports show; 0 = not reported
  };
  // The want_buffer_* columns are the numbers the field reports actually carried,
  // quoted in OffbandMesh/meshcore-firmware commits fda4cdd8 and 4f5e8b7a.
  const Case cases[] = {
      {"madmax_2069        8608 B", 8608, 51, 50, 147},
      {"schill            12973 B", 12973, 76, 75, 222},
      {"hv4-bench-1       14495 B", 14495, 85, 84, 249},
      {"near-full caplog  16384 B", 16384, 96, 95, 0},
  };

  for (const auto &c : cases) {
    char label[96];

    const Transfer fixed = transfer(c.total, payload, (long)deliverable, (long)deliverable);
    snprintf(label, sizeof(label), "%s -> chunks, sized to the link", c.who);
    check(label, fixed.chunks, c.want_chunks);
    snprintf(label, sizeof(label), "%s -> bytes arrived, sized to the link", c.who);
    check(label, fixed.delivered, c.total);

    // The defect: the producer chunks at MAX_FRAME_SIZE while the link clips to
    // `deliverable`, so every FULL frame loses the difference and the short last
    // one does not.
    const Transfer clipped =
        transfer(c.total, buffer_payload, MAX_FRAME_SIZE, (long)deliverable);
    snprintf(label, sizeof(label), "%s -> chunks, sized to the buffer", c.who);
    check(label, clipped.chunks, c.want_buffer_chunks);
    if (c.want_short > 0) {
      snprintf(label, sizeof(label), "%s -> bytes LOST, sized to the buffer", c.who);
      check(label, c.total - clipped.delivered, c.want_short);
    }
  }
}

static void case_wrapper_delegation() {
  printf("\n3. The wrapper — capacity must follow the sinks writeFrame uses\n");

  {
    MultiSerialInterface multi;
    MtuInterface ble(173);
    CountingInterface usb;
    multi.addInterface(InterfaceType::Bluetooth, &ble);
    multi.addInterface(InterfaceType::USB, &usb);
    multi.enable();
    check("two enabled sinks (173 and the default) -> the minimum",
          (long)multi.maxFrameSize(), 173);
    check("...and its payload after a 2-byte header", (long)multi.maxFramePayload(2), 171);
  }

  {
    MultiSerialInterface multi;
    MtuInterface ble(173);
    CountingInterface usb;
    multi.addInterface(InterfaceType::Bluetooth, &ble);
    multi.addInterface(InterfaceType::USB, &usb);
    multi.enable();
    ble.disable();  // disabled AFTER enable() — the only reachable way

    check("the constrained sink is disabled -> capacity is the other one's",
          (long)multi.maxFrameSize(), MAX_FRAME_SIZE);

    uint8_t frame[MAX_FRAME_SIZE] = {0};
    multi.writeFrame(frame, MAX_FRAME_SIZE);
    check("...the disabled sink received nothing", (long)ble.frames_written, 0);
    check("...the enabled sink received the frame", (long)usb.frames_written, 1);
  }

  {
    MultiSerialInterface multi;
    MtuInterface generous(4096);
    multi.addInterface(InterfaceType::WiFi, &generous);
    multi.enable();
    check("a sink claiming 4096 cannot raise capacity above the frame buffer",
          (long)multi.maxFrameSize(), MAX_FRAME_SIZE);
  }

  {
    MultiSerialInterface multi;
    MtuInterface tiny(23 - 3);  // the BLE spec floor: a 20-byte deliverable
    multi.addInterface(InterfaceType::Bluetooth, &tiny);
    multi.enable();
    check("a pathologically small link -> capacity follows it down",
          (long)multi.maxFrameSize(), 20);
    check("...and a 32-byte header floors the payload at 0 rather than wrapping",
          (long)multi.maxFramePayload(32), 0);
  }
}

static void case_no_active_sink() {
  printf("\n4. No active sink — what a caller is told\n");

  MultiSerialInterface multi;
  MtuInterface ble(173);
  multi.addInterface(InterfaceType::Bluetooth, &ble);
  multi.enable();
  ble.disable();

  hazard("nothing enabled -> maxFrameSize() reports", (long)multi.maxFrameSize(),
         "the frame buffer, not 0. Harmless upstream — writeFrame sends nothing — "
         "but it means capacity alone can never say 'there is no sink'.");

  uint8_t frame[64] = {0};
  const size_t written = multi.writeFrame(frame, sizeof(frame));

  hazard("nothing enabled -> writeFrame() returns", (long)written,
         "a FULL SUCCESS for a frame no sink received: the loop body never runs, "
         "so `allSuccessful` stays true. An Attadipa adapter must not inherit this.");

  check("...and no sink saw a byte", (long)ble.bytes_written, 0);
}

// ---------------------------------------------------------------------- main

int main(int argc, char **argv) {
  const char *rev = argc > 1 ? argv[1] : "(unnamed revision)";
  printf("MeshCore BLE frame-capacity harness — upstream tree: %s\n", rev);
  printf("MAX_FRAME_SIZE, as upstream defines it: %d\n", MAX_FRAME_SIZE);

  const size_t deliverable = case_att_arithmetic();
  case_field_transfers(deliverable, 2);
  case_wrapper_delegation();
  case_no_active_sink();

  printf("\n%d checks, %d failed, %d hazards recorded\n", checks, failures, hazards);
  return failures == 0 ? 0 : 1;
}

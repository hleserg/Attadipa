#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

#include "attadipa/debug/bridge.h"
#include "attadipa/link/frame_codec.h"

namespace attadipa::sim {

// The simulator's transport for the debug channel.
//
// ### Why a Unix domain socket and not a TCP port
//
// Section 10 of the request says not to open a network port without a separate
// need. There is no such need: the host tool and the simulator run on the same
// machine by construction. A path in the filesystem carries the operating
// system's own permissions, cannot be reached from another host, and does not
// appear in a port scan. On a device this whole file is replaced by
// USB-Serial/JTAG and nothing above it changes -- which is the point of the
// bridge not owning its transport.
//
// ### Why it never blocks
//
// One `poll()` per rendered frame, non-blocking throughout, with a bounded
// amount of work per call. A screenshot of the Waveshare panel is about 600 kB
// and roughly 3400 frames on the wire; sending it inside one call would stop
// the interface for as long as that took, which on a device is a watchdog reset
// and here is a simulator that appears to have hung. Instead the transfer is
// pumped only while the outgoing buffer is below a watermark, so the socket's
// own backpressure sets the pace and the interface keeps running.
//
// ### One client
//
// Two clients injecting input into one interface is not a use case, it is a
// race. A second connection is accepted and immediately closed, which tells the
// second tool something definite instead of letting it wait.
//
// ### One simulator per path, and the file that enforces it
//
// `listen` takes an exclusive `flock` on `<socket path>.lock` -- created 0600,
// never written to, and deliberately **left behind on exit** -- for as long as
// it takes to inspect the path, remove a stale socket from it and bind. Two
// simulators started on one path otherwise both classify the same stale entry
// and the second one's `unlink` removes the first one's live socket. The lock
// is per path, so two simulators on two paths never wait for each other, and
// `docs/testing/WATCH_CONTROL.md` tells the operator the file is there.
// `close()` takes it too, and unlinks only a socket whose device and inode are
// still the ones this server bound -- best effort, since a reused inode number
// matches; see there.
class DebugServer {
public:
    ~DebugServer();

    // Creates and binds the socket. Returns false with a message on stderr.
    bool listen(const std::string& path);

    // Accepts, reads, dispatches and pumps. Called once per frame.
    void poll(std::uint32_t now_ms, debug::Bridge& bridge);

    void close();

    bool listening() const { return listen_fd_ >= 0; }
    bool has_client() const { return client_fd_ >= 0; }

    const std::string& path() const { return path_; }

private:
    // Dispatches every complete frame the decoder holds; it is `feed`'s drain.
    void dispatch_ready(std::uint32_t now_ms, debug::Bridge& bridge);

    void drop_client(std::uint32_t now_ms, debug::Bridge& bridge, const char* why);
    void flush();
    void queue(const std::uint8_t* payload, std::size_t length);

    static void emit(void* ctx, const std::uint8_t* payload, std::size_t length);

    // `close` for a caller that already holds the path's claim -- `listen`,
    // failing after its bind. Taking the claim again there would wait on
    // itself: `flock` locks conflict between two opens in one process too.
    void close_claimed(bool may_unlink);

    int         listen_fd_ = -1;
    int         client_fd_ = -1;
    std::string path_;
    // The inode `bind` created, so that `close` unlinks the socket it made and
    // not whatever occupies that path by then. Sharing a path is **refused**
    // now (docs/testing/WATCH_CONTROL.md, *"One path per simulator"*) -- this
    // comment used to cite that file for the opposite, which the commit that
    // added the refusal made stale. The check survives on the reason `close`
    // gives: a path can be re-bound by a *later* simulator after this one
    // started, and without the inode a normal exit deletes that server's socket
    // out from under it.
    dev_t       path_dev_ = 0;
    ino_t       path_ino_ = 0;

    // Set by `queue` when the outgoing buffer passes its watermark, acted on by
    // `poll`. Deferred rather than immediate so the bridge is never re-entered
    // from inside its own `emit`.
    bool                      overflowed_ = false;

    link::Decoder             decoder_;
    std::vector<std::uint8_t> out_;
    std::size_t               out_sent_ = 0;
};

}  // namespace attadipa::sim

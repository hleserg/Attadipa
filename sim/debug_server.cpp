#include "debug_server.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace attadipa::sim {
namespace {

// Stop pumping the screenshot while this much is already waiting to go out.
// Chosen as a few dozen frames' worth: enough that the socket stays busy, small
// enough that abandoning a transfer costs almost nothing.
constexpr std::size_t kOutputWatermark = 16 * 1024;

// A ceiling on the outgoing buffer. If the client stops reading entirely, the
// buffer must not grow until the simulator is out of memory -- the connection
// is dropped instead, which the bridge turns into a release of everything that
// client was holding.
constexpr std::size_t kOutputMax = 4 * 1024 * 1024;

// A declared bound on how many chunks one poll hands to the socket, so the
// loop's iteration count is a number somebody chose rather than a consequence.
//
// **It is not the cost bound, and the first spelling of it was 16, which cost a
// factor of five.** The work here is a bitwise CRC and its cost is proportional
// to *bytes*, which `kOutputWatermark` already bounds at 16 KiB. A chunk count
// binds only when it is tighter than the watermark, and then what it bounds is
// throughput. Measured on this desktop: a Waveshare screenshot takes ~0.50 s
// with the watermark alone and took **1.05 s** at 16 chunks a poll, because
// 617,460 bytes is 3,469 chunks of 178 and `sim/main.cpp` caps the loop at 5 ms
// while a client is connected -- so 217 polls cannot happen faster. Thirty-two
// already costs 0.54 s; sixty-four sits at the watermark's own effective limit,
// 0.48-0.50 s against 0.50 s unbounded, so the count is declared without the
// transfer being slowed to declare it. `docs/testing/WATCH_CONTROL.md` carries
// the figures and says to re-measure them when this constant moves.
//
// This file is the model for the firmware transport tracked by issue #117.
// What carries over is which bound matters: if 16 KiB of bitwise CRC in
// one pass is too much for the task servicing a device's interface, the numbers
// to change are the watermark and the CRC implementation, not this one.
constexpr int kMaxChunksPerPoll = 64;

// How long the stale-socket probe waits for a connect to resolve. Short,
// because it runs before anything is on screen and the fallback answer is a
// refusal the operator can read.
constexpr int kProbeTimeoutMs = 250;

bool set_non_blocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// Is something serving this socket right now?
//
// A stale socket file left by a killed simulator and a live one belonging to a
// running simulator are the same directory entry; only a connection tells them
// apart. `ECONNREFUSED` means the inode outlived its server and may be removed.
// Anything else -- a successful connect, a permission error, a timeout -- is
// answered "live", because the cost of being wrong in that direction is a
// refusal the operator can read, and in the other direction it is deleting a
// running simulator's socket.
//
// The connect is seen by the other server as a client that attaches and leaves
// immediately, which its one-client rule handles the same way it handles any
// disconnect.
bool socket_is_served(const std::string& path)
{
    const int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (probe < 0) {
        return true;
    }
    // Non-blocking, with a bounded wait. A blocking connect to a listening
    // socket whose accept backlog is full does not return, and this runs on the
    // start-up path with nothing printed yet -- so pointing `--debug-socket` at
    // another program's busy socket would hang the simulator before it had said
    // anything. The answer for a timeout is the same as for every other
    // non-ECONNREFUSED result: live, and therefore refused.
    (void)set_non_blocking(probe);

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

    int rc  = ::connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    int err = errno;
    if (rc != 0 && (err == EINPROGRESS || err == EAGAIN || err == EALREADY)) {
        pollfd waiting{};
        waiting.fd     = probe;
        waiting.events = POLLOUT;
        const int ready = ::poll(&waiting, 1, kProbeTimeoutMs);
        if (ready == 1) {
            int       so_error = 0;
            socklen_t len      = sizeof(so_error);
            if (::getsockopt(probe, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0) {
                rc  = so_error == 0 ? 0 : -1;
                err = so_error;
            }
        }
    }
    const bool refused = rc != 0 && err == ECONNREFUSED;
    ::close(probe);
    return !refused;
}

}  // namespace

DebugServer::~DebugServer()
{
    close();
}

bool DebugServer::listen(const std::string& path)
{
    if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) {
        std::fprintf(stderr, "debug: socket path is empty or longer than %zu bytes\n",
                     sizeof(sockaddr_un::sun_path) - 1);
        return false;
    }

    listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::fprintf(stderr, "debug: socket(): %s\n", std::strerror(errno));
        return false;
    }

    // A stale socket file from a simulator that was killed rather than closed
    // would make bind() fail with EADDRINUSE forever, so one has to be removed.
    // What must not happen is removing anything else.
    //
    // The previous spelling was an unconditional `unlink`, and its stated
    // reason -- "a live server holds the path open and a second one is refused
    // at the accept" -- is not true: two servers on one path do not know about
    // each other. The second unlinks the first's inode and binds its own, and
    // the first goes on printing that it is listening while nothing can reach
    // it. That is the *documented* usage, not a corner case:
    // docs/testing/WATCH_CONTROL.md names one path for both boards and then
    // says to check both boards every time. And with a non-socket path it is
    // worse still -- `--debug-socket /tmp/keepme` deleted the file, silently,
    // which is the one thing CLAUDE.md says never to do without looking first.
    struct stat existing {};
    if (::stat(path.c_str(), &existing) == 0) {
        if (!S_ISSOCK(existing.st_mode)) {
            std::fprintf(stderr,
                         "debug: %s exists and is not a socket -- refusing to remove it\n",
                         path.c_str());
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
        if (socket_is_served(path)) {
            std::fprintf(stderr,
                         "debug: %s is already served by a running simulator; "
                         "give this one a different --debug-socket path\n",
                         path.c_str());
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
        ::unlink(path.c_str());
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

    // The permissions are set here, not left to whatever umask the process
    // inherited. On Linux connect() needs write permission on the socket
    // inode, so the mode is the access control -- and a umask of 000, which
    // containers, CI runners and daemons routinely have, would otherwise
    // publish a 0777 socket that any local user can drive the interface
    // through. The header two files up rests its whole security argument on
    // filesystem permissions; this is that argument being true.
    const mode_t previous_umask = ::umask(0177);
    const int    bind_result =
        ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address));
    const int bind_errno = errno;
    ::umask(previous_umask);

    if (bind_result != 0) {
        std::fprintf(stderr, "debug: bind(%s): %s\n", path.c_str(), std::strerror(bind_errno));
        close();
        return false;
    }

    // Claimed here, immediately after the bind that created it, and not after
    // the three checks below. Those call `close()` on failure, and `close()`
    // removes the socket by matching `path_` against the inode -- so recording
    // it at the end of a successful `listen()` meant a failed `chmod`, `listen`
    // or `fcntl` left the file behind with nothing owning it. The next
    // simulator recovers through the staleness probe, which is why this was a
    // leak rather than a lock-out, but the cleanup should not depend on
    // somebody else's rescue.
    path_ = path;
    struct stat bound {};
    if (::stat(path_.c_str(), &bound) == 0) {
        path_dev_ = bound.st_dev;
        path_ino_ = bound.st_ino;
    }
    // Belt and braces: POSIX does not require a socket file's mode to be
    // honoured at all, and some filesystems ignore the umask. Failing loudly
    // beats listening on something more open than advertised.
    if (::chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) {
        std::fprintf(stderr, "debug: chmod(%s, 0600): %s\n", path.c_str(), std::strerror(errno));
        close();
        return false;
    }
    if (::listen(listen_fd_, 1) != 0) {
        std::fprintf(stderr, "debug: listen(): %s\n", std::strerror(errno));
        close();
        return false;
    }
    if (!set_non_blocking(listen_fd_)) {
        std::fprintf(stderr, "debug: could not make the listening socket non-blocking\n");
        close();
        return false;
    }

    std::printf("debug: listening on %s\n", path_.c_str());
    std::fflush(stdout);
    return true;
}

void DebugServer::close()
{
    if (client_fd_ >= 0) {
        ::close(client_fd_);
        client_fd_ = -1;
    }
    if (listen_fd_ >= 0) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!path_.empty()) {
        // Only if the path still names the inode this server created. Another
        // simulator may have taken the name in the meantime -- refused now, but
        // an older build in the same working tree does not refuse -- and a
        // normal exit must not delete a live server's socket.
        //
        // Best effort, and the residual case is worth naming rather than
        // implying it is closed: a filesystem may reuse a freed inode number,
        // so if somebody unlinks this socket by hand and a second simulator
        // binds the same path, that new socket can land on the same number and
        // be removed here. There is no portable way to ask a bound AF_UNIX
        // descriptor which inode it holds, so a stat is the strongest check
        // available.
        struct stat current {};
        if (::stat(path_.c_str(), &current) == 0 && current.st_dev == path_dev_ &&
            current.st_ino == path_ino_) {
            ::unlink(path_.c_str());
        }
        path_.clear();
        path_dev_ = 0;
        path_ino_ = 0;
    }
}

void DebugServer::emit(void* ctx, const std::uint8_t* payload, std::size_t length)
{
    static_cast<DebugServer*>(ctx)->queue(payload, length);
}

void DebugServer::queue(const std::uint8_t* payload, std::size_t length)
{
    std::uint8_t       frame[link::kMaxFrame] = {};
    const std::size_t  n = link::encode(payload, length, frame, sizeof(frame));
    if (n == 0) {
        // encode() refuses rather than truncating. A message that will not fit
        // is a bug in the message, and dropping it silently would leave the
        // host waiting for a reply that was never framed.
        std::fprintf(stderr, "debug: a %zu-byte message could not be framed\n", length);
        return;
    }
    if (out_.size() - out_sent_ + n > kOutputMax) {
        // Marked, not closed here. The old spelling called `::close()` behind
        // `poll`'s back: `out_`, `out_sent_` and the decoder kept a dead
        // connection's state, the bridge was never told, and the next loop
        // iteration `recv`'d on `-1` -- so the disconnect was reported as
        // `Bad file descriptor`. The cleanup was right and the diagnosis was a
        // lie.
        //
        // `drop_client` is the one path that resets all of it, but calling it
        // from here would re-enter `Bridge::on_disconnect` from inside the
        // bridge's own `emit` callback, while it is mid-`handle`. That is a
        // worse bug than the one being fixed, so the overflow is recorded and
        // `poll` acts on it at a point where the bridge is not on the stack.
        overflowed_ = true;
        return;
    }
    out_.insert(out_.end(), frame, frame + n);
}

void DebugServer::flush()
{
    while (client_fd_ >= 0 && out_sent_ < out_.size()) {
        const ssize_t written =
            ::send(client_fd_, out_.data() + out_sent_, out_.size() - out_sent_, MSG_NOSIGNAL);
        if (written > 0) {
            out_sent_ += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;  // the socket is full; try again next frame
        }
        // The client is gone. Left for poll() to notice on the read side, so
        // that the disconnect is handled in exactly one place.
        break;
    }

    // Compact once everything queued has gone out, rather than erasing from the
    // front on every write: a screenshot is thousands of small appends and a
    // front-erase each time is quadratic.
    if (out_sent_ == out_.size()) {
        out_.clear();
        out_sent_ = 0;
    }
}

void DebugServer::drop_client(std::uint32_t now_ms, debug::Bridge& bridge, const char* why)
{
    if (client_fd_ >= 0) {
        ::close(client_fd_);
        client_fd_ = -1;
    }
    decoder_.reset();
    out_.clear();
    out_sent_   = 0;
    overflowed_ = false;

    // Everything that client was holding is lifted, and nothing a person is
    // holding is touched. This is the whole reason input events carry an origin.
    bridge.on_disconnect(now_ms);
    std::printf("debug: client disconnected (%s)\n", why);
    std::fflush(stdout);
}

// Dispatches every complete frame the decoder currently holds. Returns true if
// it dispatched at least one, which is how the read loop above tells "the
// decoder is full and stuck" from "the decoder is full and about to empty".
bool DebugServer::dispatch_ready(std::uint32_t now_ms, debug::Bridge& bridge)
{
    bool any = false;
    std::uint8_t payload[link::kMaxPayload];
    for (;;) {
        const link::FrameResult frame = decoder_.next(payload, sizeof(payload));
        if (frame.exhausted()) {
            break;
        }
        if (frame.status == link::FrameStatus::OutputTooSmall) {
            return any;
        }
        bridge.handle(payload, frame.length, now_ms, &DebugServer::emit, this);
        any = true;
    }
    return any;
}

void DebugServer::poll(std::uint32_t now_ms, debug::Bridge& bridge)
{
    if (listen_fd_ < 0) {
        return;
    }

    // Accept. A second client is accepted and closed at once rather than left
    // waiting: two tools injecting input into one interface is a race, and a
    // definite refusal is more useful than a hang.
    const int incoming = ::accept(listen_fd_, nullptr, nullptr);
    if (incoming >= 0) {
        if (client_fd_ >= 0) {
            ::close(incoming);
        } else if (!set_non_blocking(incoming)) {
            ::close(incoming);
        } else {
            client_fd_ = incoming;
            decoder_.reset();
            out_.clear();
            out_sent_  = 0;
            // The one piece of per-client state `accept` used to leave behind.
            // Unreachable only while `Bridge::tick` emits nothing -- and the
            // no-client branch calls it every poll with a live `emit`, so the
            // day `tick` gains a reply, `queue` can pass `kOutputMax` with
            // nobody reading and the *next* client is dropped on its first poll
            // for "the client stopped reading", having been given nothing to
            // read. Reset beside the buffer it describes.
            overflowed_ = false;
            std::printf("debug: client connected\n");
            std::fflush(stdout);
        }
    }

    if (client_fd_ < 0) {
        // `Bridge::tick` is the retry that `core/input.h` promises for a
        // release which could not be queued -- and the case the promise exists
        // for is precisely this one: a client that left with a button held,
        // whose `release_all` found the queue full and *deliberately* left the
        // input held rather than lose the release. Returning above the tick
        // meant nothing ever tried again. The hold outlived the connection, a
        // reconnecting client pressing that button got `BadInput`, and only
        // `input reset` cleared it -- while `WATCH_CONTROL.md` sold the
        // 30-second expiry as the backstop for exactly this.
        //
        // The two `drop_client` paths below still return without ticking, and
        // may: this branch is reached on every subsequent poll, so the retry
        // continues for as long as the hold does. No `flush` -- `tick` only
        // pushes into the input queue and ignores its `emit`.
        bridge.tick(now_ms, &DebugServer::emit, this);
        return;
    }

    // A write that overflowed the watermark during the last frame's dispatch.
    // Handled here, before any reading, because the client is not going to be
    // read from again -- and handled *outside* the bridge's callback, which is
    // the whole reason `queue` only set a flag.
    if (overflowed_) {
        drop_client(now_ms, bridge, "the client stopped reading");
        return;
    }

    // Read whatever is there, bounded -- and **drain as we feed**. The decoder
    // holds a couple of hundred bytes; this loop can read 8 x 4096 = 32 KiB in
    // one poll. Pushing all of that before dispatching anything meant a client
    // that pipelined its commands had them silently refused by a full decoder,
    // which is the one thing this transport is not allowed to do quietly. The
    // inner drain always makes progress: the buffer either holds a complete
    // frame, which `next` consumes, or a bad header, which it resynchronises
    // past one byte at a time.
    std::uint8_t chunk[4096];
    for (int reads = 0; reads < 8; ++reads) {
        const ssize_t got = ::recv(client_fd_, chunk, sizeof(chunk), 0);
        if (got > 0) {
            const std::size_t total = static_cast<std::size_t>(got);
            std::size_t       at    = 0;
            while (at < total) {
                // Drain *first*, then offer. The other order -- push, and on a
                // refusal drain and push the same bytes again -- counted them
                // into `input_dropped` on every attempt, because
                // `Decoder::push` adds `length - accepted` itself
                // (`frame_codec.cpp:89-91`). So the bytes were never lost
                // silently; they were reported several times over, which is the
                // same statistic being wrong in the friendlier direction.
                // Draining first means a zero take is a real refusal, counted
                // once, and there is nothing left to try.
                (void)dispatch_ready(now_ms, bridge);
                const std::size_t taken = decoder_.push(chunk + at, total - at);
                if (taken == 0) {
                    break;
                }
                at += taken;
            }
            (void)dispatch_ready(now_ms, bridge);
            continue;
        }
        if (got == 0) {
            drop_client(now_ms, bridge, "closed");
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        drop_client(now_ms, bridge, std::strerror(errno));
        return;
    }

    // Anything left over from an earlier poll.
    (void)dispatch_ready(now_ms, bridge);

    // Pump the screenshot while there is room. The watermark is what keeps a
    // 600 kB transfer from stopping the interface: the socket sets the pace.
    for (int chunk = 0; chunk < kMaxChunksPerPoll; ++chunk) {
        if (out_.size() - out_sent_ >= kOutputWatermark) {
            break;
        }
        if (!bridge.pump(&DebugServer::emit, this)) {
            break;
        }
    }

    bridge.tick(now_ms, &DebugServer::emit, this);
    flush();

    // No write-failure branch here. `flush` never clears `client_fd_` -- it
    // says so where it breaks out -- because the disconnect is handled in
    // exactly one place, the read side, and a failed write surfaces there as
    // an EOF on the next poll. A second branch here looked like belt and
    // braces and was unreachable code claiming to be a safety net.
}

}  // namespace attadipa::sim

#include "debug_server.h"

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
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

bool set_non_blocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
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
    // would make bind() fail with EADDRINUSE forever. Removing it is safe
    // because a live server holds the path open and a second one is refused at
    // the accept, not here.
    ::unlink(path.c_str());

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::fprintf(stderr, "debug: bind(%s): %s\n", path.c_str(), std::strerror(errno));
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

    path_ = path;
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
        ::unlink(path_.c_str());
        path_.clear();
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
        std::fprintf(stderr, "debug: the client stopped reading; dropping the connection\n");
        if (client_fd_ >= 0) {
            ::close(client_fd_);
            client_fd_ = -1;
        }
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
    out_sent_ = 0;

    // Everything that client was holding is lifted, and nothing a person is
    // holding is touched. This is the whole reason input events carry an origin.
    bridge.on_disconnect(now_ms);
    std::printf("debug: client disconnected (%s)\n", why);
    std::fflush(stdout);
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
            out_sent_ = 0;
            std::printf("debug: client connected\n");
            std::fflush(stdout);
        }
    }

    if (client_fd_ < 0) {
        return;
    }

    // Read whatever is there, bounded. The decoder is fragment-agnostic by
    // construction, so it does not matter where the reads happen to split.
    std::uint8_t chunk[4096];
    for (int reads = 0; reads < 8; ++reads) {
        const ssize_t got = ::recv(client_fd_, chunk, sizeof(chunk), 0);
        if (got > 0) {
            decoder_.push(chunk, static_cast<std::size_t>(got));
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

    // Dispatch every complete frame. One push may complete several.
    std::uint8_t payload[link::kMaxPayload];
    for (;;) {
        const std::size_t length = decoder_.next(payload, sizeof(payload));
        if (length == 0) {
            break;
        }
        bridge.handle(payload, length, now_ms, &DebugServer::emit, this);
    }

    // Pump the screenshot while there is room. The watermark is what keeps a
    // 600 kB transfer from stopping the interface: the socket sets the pace.
    while (out_.size() - out_sent_ < kOutputWatermark) {
        if (!bridge.pump(&DebugServer::emit, this)) {
            break;
        }
    }

    bridge.tick(now_ms, &DebugServer::emit, this);
    flush();

    if (client_fd_ < 0) {
        drop_client(now_ms, bridge, "write failed");
    }
}

}  // namespace attadipa::sim

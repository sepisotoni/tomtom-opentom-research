#include "../src/display/frame_transport.h"

#include <array>
#include <cstdint>
#include <iostream>

int main() {
    using tt::display::FrameTransport;
    using tt::display::TransportState;
    using tt::display::kFrameBytes;

    FrameTransport transport;
    if (transport.state() != TransportState::stopped) {
        std::cerr << "transport must start stopped\n";
        return 1;
    }

    std::array<std::uint8_t, kFrameBytes> frame{};
    if (transport.submit_frame(frame.data(), frame.size()) ||
        transport.submit_frame(nullptr, frame.size()) ||
        transport.submit_frame(frame.data(), frame.size() - 1)) {
        std::cerr << "transport accepted a frame outside its running state or size\n";
        return 1;
    }
    if (!transport.start() || !transport.start()) {
        std::cerr << "transport failed to start idempotently\n";
        return 1;
    }
    if (!transport.submit_frame(frame.data(), frame.size())) {
        std::cerr << "transport rejected a fixed-size frame while running\n";
        return 1;
    }
    transport.stop();
    if (transport.state() != TransportState::stopped ||
        transport.submit_frame(frame.data(), frame.size())) {
        std::cerr << "transport failed to stop cleanly\n";
        return 1;
    }

    std::cout << "frame transport lifecycle: 5 checks passed\n";
    return 0;
}

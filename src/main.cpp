#include "mcrs/protocol/packet_codec.hpp"

#include <array>
#include <cstddef>
#include <iostream>

int main()
{
    using namespace mcrs::protocol;

    constexpr std::array payload{std::byte{0xCA}, std::byte{0xFE}};
    const auto encoded = encode_packet(PacketType::ping, 1, payload);
    if (!encoded)
    {
        std::cerr << "encode failed: " << to_string(encoded.error()) << '\n';
        return 1;
    }

    const auto decoded = decode_one(*encoded);
    if (!decoded)
    {
        std::cerr << "decode failed: " << to_string(decoded.error()) << '\n';
        return 1;
    }

    std::cout << "decoded packet sequence=" << decoded->header.sequence
              << " payload=" << decoded->payload.size() << " bytes\n";
    return 0;
}

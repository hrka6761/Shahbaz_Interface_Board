#include "shahbaz/protocol/cobs.hpp"

namespace shahbaz::protocol {

CobsResult cobsEncode(ByteView input, MutableByteView output) noexcept {
    if (!isValid(input) || !isValid(output)) {
        return {CobsStatus::InvalidArgument, 0U};
    }
    if (output.size == 0U) {
        return {CobsStatus::OutputTooSmall, 0U};
    }

    std::size_t read_index = 0U;
    std::size_t write_index = 1U;
    std::size_t code_index = 0U;
    std::uint8_t code = 0x01U;

    while (read_index < input.size) {
        if (input.data[read_index] == 0U) {
            output.data[code_index] = code;
            code_index = write_index;
            if (write_index >= output.size) {
                return {CobsStatus::OutputTooSmall, 0U};
            }
            ++write_index;
            code = 0x01U;
            ++read_index;
            continue;
        }

        if (write_index >= output.size) {
            return {CobsStatus::OutputTooSmall, 0U};
        }
        output.data[write_index++] = input.data[read_index++];
        ++code;

        if (code == 0xFFU) {
            output.data[code_index] = code;
            code_index = write_index;
            if (write_index >= output.size) {
                return {CobsStatus::OutputTooSmall, 0U};
            }
            ++write_index;
            code = 0x01U;
        }
    }

    output.data[code_index] = code;

    return {CobsStatus::Ok, write_index};
}

CobsResult cobsDecode(ByteView input, MutableByteView output) noexcept {
    if (!isValid(input) || !isValid(output)) {
        return {CobsStatus::InvalidArgument, 0U};
    }
    if (input.size == 0U) {
        return {CobsStatus::EmptyEncoding, 0U};
    }

    std::size_t read_index = 0U;
    std::size_t write_index = 0U;

    while (read_index < input.size) {
        const std::uint8_t code = input.data[read_index++];
        if (code == 0U) {
            return {CobsStatus::MalformedEncoding, 0U};
        }

        const std::size_t block_length = static_cast<std::size_t>(code - 1U);
        if (block_length > input.size - read_index) {
            return {CobsStatus::MalformedEncoding, 0U};
        }
        if (block_length > output.size - write_index) {
            return {CobsStatus::OutputTooSmall, 0U};
        }

        for (std::size_t i = 0U; i < block_length; ++i) {
            const std::uint8_t byte = input.data[read_index++];
            if (byte == 0U) {
                return {CobsStatus::MalformedEncoding, 0U};
            }
            output.data[write_index++] = byte;
        }

        if (code != 0xFFU && read_index < input.size) {
            if (write_index >= output.size) {
                return {CobsStatus::OutputTooSmall, 0U};
            }
            output.data[write_index++] = 0U;
        }
    }

    return {CobsStatus::Ok, write_index};
}

}  // namespace shahbaz::protocol

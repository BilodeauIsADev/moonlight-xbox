#include "PyroWaveFraming.h"
#include <pyrowave_bitstream.hpp>

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<uint8_t>;

void check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T>
void append(Bytes& bytes, const T& value)
{
    const auto* data = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), data, data + sizeof(value));
}

Bytes sequence(const PyroWave::BlockLayout& layout, uint32_t blocks, uint32_t number = 3)
{
    PyroWave::BitstreamSequenceHeader header = {};
    header.width_minus_1 = layout.width - 1;
    header.height_minus_1 = layout.height - 1;
    header.sequence = number;
    header.extended = 1;
    header.total_blocks = blocks;
    header.chroma_resolution = static_cast<uint32_t>(layout.chroma);
    Bytes bytes;
    append(bytes, header);
    return bytes;
}

void block(Bytes& bytes, uint32_t index, uint32_t number = 3, uint16_t words = 2)
{
    PyroWave::BitstreamHeader header = {};
    header.payload_words = words;
    header.sequence = number;
    header.block_index = index;
    append(bytes, header);
}

void submitToCodec(const Bytes& bytes, const PyroWaveFraming::Frame& frame,
                   const PyroWave::BlockLayout& layout, int expectedBlocks)
{
    PyroWave::BitstreamParser parser;
    parser.init(&layout);
    for (const auto& span : frame.spans) {
        check(span.offset <= bytes.size() && span.size <= bytes.size() - span.offset,
              "parser returned a span outside the frame");
        // The real decoder stores frames in a word-aligned buffer too.
        std::vector<uint32_t> words(span.size / sizeof(uint32_t));
        std::memcpy(words.data(), bytes.data() + span.offset, span.size);
        check(parser.push_packet(words.data(), span.size), "codec rejected an emitted span");
    }
    check(parser.get_decoded_blocks() == expectedBlocks, "codec received the wrong blocks");
}

void testFramings(const PyroWave::BlockLayout& layout)
{
    PyroWaveFraming::Frame frame;
    std::string error;
    Bytes records = sequence(layout, 2);
    block(records, 0);
    append(records, PyroWaveFraming::k_PaddingMagic);
    append(records, uint32_t(2));
    append(records, uint32_t(0));
    append(records, uint32_t(0));
    block(records, 1);
    check(PyroWaveFraming::parse(records.data(), records.size(), layout, frame, error),
          "valid record framing was rejected");
    check(frame.framing == PyroWaveFraming::Framing::Records && frame.paddingBytes == 16 &&
          frame.spans.size() == 2 && frame.blockRecords == 2 && !frame.partial,
          "record spans or padding were parsed incorrectly");
    submitToCodec(records, frame, layout, 2);

    Bytes first = sequence(layout, 2);
    block(first, 0);
    Bytes second;
    block(second, 1);
    Bytes prefixed;
    append(prefixed, uint32_t(2));
    append(prefixed, uint32_t(first.size()));
    prefixed.insert(prefixed.end(), first.begin(), first.end());
    append(prefixed, uint32_t(second.size()));
    prefixed.insert(prefixed.end(), second.begin(), second.end());
    check(PyroWaveFraming::parse(prefixed.data(), prefixed.size(), layout, frame, error),
          "valid length-prefixed framing was rejected");
    check(frame.framing == PyroWaveFraming::Framing::LengthPrefixed && frame.spans.size() == 2,
          "length-prefixed packet boundaries were lost");
    submitToCodec(prefixed, frame, layout, 2);

    std::vector<PyroWaveFraming::Segment> lost = {{0, 20, false}, {20, 16, true}};
    check(!PyroWaveFraming::parse(prefixed.data(), prefixed.size(), lost, layout, frame, error),
          "length-prefixed frame with lost packets was accepted");
}

void testLoss(const PyroWave::BlockLayout& layout)
{
    const uint32_t fineBlock = uint32_t(PyroWave::get_num_active_blocks(layout, 2));
    Bytes records = sequence(layout, 3);
    block(records, 0);
    block(records, 1);
    block(records, fineBlock);
    std::vector<PyroWaveFraming::Segment> segments = {
        {0, 16, false, true}, {16, 8, true}, {24, 8, false, true}
    };
    PyroWaveFraming::Frame frame;
    std::string error;
    check(PyroWaveFraming::parse(records.data(), records.size(), segments, 1, layout, frame, error),
          "recoverable packet loss was rejected");
    check(frame.partial && frame.coarseLevelIntact && frame.blockRecords == 2,
          "critical packet count or recovery was ignored");
    submitToCodec(records, frame, layout, 2);

    check(PyroWaveFraming::parse(records.data(), records.size(), segments, 2, layout, frame, error),
          "frame with coarse packet loss could not be inspected");
    check(frame.partial && !frame.coarseLevelIntact, "coarse packet loss was not reported");
    check(PyroWaveFraming::parse(records.data(), records.size(), segments, layout, frame, error),
          "frame without critical packet count could not be inspected");
    check(!frame.coarseLevelIntact, "unknown coarse prefix was assumed intact after loss");

    segments[0].lost = true;
    check(!PyroWaveFraming::parse(records.data(), records.size(), segments, layout, frame, error),
          "missing sequence header was accepted");
    segments[0].lost = false;
    check(!PyroWaveFraming::parse(records.data(), records.size(), segments, 4, layout, frame, error),
          "critical packet count beyond the frame was accepted");

    Bytes empty = sequence(layout, 0, 7);
    check(PyroWaveFraming::parse(empty.data(), empty.size(), layout, frame, error),
          "parser could not recover after a rejected frame");
    check(!frame.partial && frame.coarseLevelIntact && frame.sequence == 7 && frame.spans.size() == 1,
          "parser retained state from an earlier partial frame");
    submitToCodec(empty, frame, layout, 0);
}

void testMalformed(const PyroWave::BlockLayout& layout)
{
    PyroWaveFraming::Frame frame;
    std::string error;
    auto reject = [&](const Bytes& bytes) {
        check(!PyroWaveFraming::parse(bytes.data(), bytes.size(), layout, frame, error),
              "malformed frame was accepted");
        check(!error.empty(), "rejected frame has no diagnostic");
    };
    reject({});
    reject(Bytes(7));
    reject(sequence(layout, uint32_t(layout.block_count_32x32) + 1));
    reject(sequence(layout, 1));

    Bytes bytes = sequence(layout, 1);
    block(bytes, 0, 2);
    reject(bytes); // Block from a different sequence.
    bytes = sequence(layout, 1);
    block(bytes, 0, 3, 0);
    reject(bytes); // Zero length must not loop forever.
    bytes = sequence(layout, 1);
    block(bytes, 0, 3, 3);
    reject(bytes); // Payload runs beyond the frame.
    bytes = sequence(layout, 0);
    Bytes duplicate = bytes;
    bytes.insert(bytes.end(), duplicate.begin(), duplicate.end());
    reject(bytes);
    bytes = sequence(layout, 0);
    append(bytes, PyroWaveFraming::k_PaddingMagic);
    append(bytes, uint32_t(0xFFFFFFFFu));
    reject(bytes); // Oversized padding must not overflow.

    bytes = sequence(layout, 0);
    std::vector<PyroWaveFraming::Segment> gap = {{1, bytes.size() - 1, false}};
    check(!PyroWaveFraming::parse(bytes.data(), bytes.size(), gap, layout, frame, error),
          "packet map with a gap was accepted");
    PyroWave::BlockLayout invalidLayout;
    check(!PyroWaveFraming::parse(bytes.data(), bytes.size(), invalidLayout, frame, error),
          "uninitialized layout was accepted");
}
} // namespace

int main()
{
    try {
        for (auto chroma : {PyroWave::ChromaSubsampling::Chroma420, PyroWave::ChromaSubsampling::Chroma444}) {
            PyroWave::BlockLayout layout;
            check(layout.init(1920, 1080, chroma), "test layout could not be initialized");
            testFramings(layout);
            testLoss(layout);
            testMalformed(layout);
        }
        std::cout << "PyroWave framing tests passed for 4:2:0 and 4:4:4\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

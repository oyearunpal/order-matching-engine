#pragma once

#include <charconv>
#include <cstdint>
#include <ostream>

#include "event_sink.hpp"

namespace matching {

// EventSink that writes events in the wire CSV format, one per line:
//   TradeEvent:           2,quantity,price
//   OrderFullyFilled:     3,orderid
//   OrderPartiallyFilled: 4,orderid,remaining
// Each line is formatted into a stack buffer and written with a single call;
// flushing is left to the caller.
class CsvEventWriter {
public:
    // `out` must outlive the writer.
    explicit CsvEventWriter(std::ostream& out) noexcept : out_(out) {}

    // Writes "2,quantity,price".
    void onTrade(Quantity quantity, Price price) {
        Line line(MessageType::Trade);
        line.appendNumber(quantity);
        line.appendPrice(price);
        line.writeTo(out_);
    }

    // Writes "3,orderid".
    void onFullyFilled(OrderId id) {
        Line line(MessageType::OrderFullyFilled);
        line.appendNumber(id);
        line.writeTo(out_);
    }

    // Writes "4,orderid,remaining".
    void onPartiallyFilled(OrderId id, Quantity remaining) {
        Line line(MessageType::OrderPartiallyFilled);
        line.appendNumber(id);
        line.appendNumber(remaining);
        line.writeTo(out_);
    }

private:
    // Builds one output line in a fixed-size stack buffer.
    class Line {
    public:
        // Starts the line with the message type digit.
        explicit Line(MessageType type) noexcept {
            *end_++ = static_cast<char>('0' + static_cast<int>(type));
        }

        // Appends ',' and an unsigned integer.
        void appendNumber(std::uint64_t value) noexcept {
            *end_++ = ',';
            end_ = std::to_chars(end_, buffer_ + sizeof(buffer_), value).ptr;
        }

        // Appends ',' and a price in its shortest exact form.
        void appendPrice(Price price) noexcept {
            *end_++ = ',';
            end_ = writePrice(end_, price);
        }

        // Ends the line with '\n' and writes it with a single call.
        void writeTo(std::ostream& out) {
            *end_++ = '\n';
            out.write(buffer_, end_ - buffer_);
        }

    private:
        // Type digit + 2 x (comma + 20-digit uint64) + comma + price + newline.
        char buffer_[1 + 2 * 21 + 1 + kMaxPriceChars + 1];
        char* end_ = buffer_;
    };

    std::ostream& out_;
};

static_assert(EventSink<CsvEventWriter>);

}  // namespace matching

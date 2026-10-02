#include "session.hpp"

#include <array>
#include <exception>
#include <istream>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>

#include "csv_event_writer.hpp"
#include "order_book.hpp"
#include "parser.hpp"

namespace matching {
namespace {

// A valid request is under 60 characters; anything far longer is rejected
// without buffering it, so a huge line cannot exhaust memory.
constexpr std::size_t kMaxLineLength = 1024;

// Initial capacity of the order index, to avoid early rehashing.
constexpr std::size_t kExpectedOrders = 1 << 16;

enum class ReadStatus { Line, TooLong, End };

// Reads one '\n'-terminated line into `buffer` (embedded NUL bytes are kept).
// A line longer than the buffer is consumed and discarded.
ReadStatus readLine(std::istream& in, std::array<char, kMaxLineLength + 1>& buffer, std::string_view& line) {
    in.getline(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto extracted = static_cast<std::size_t>(in.gcount());

    if (in.bad()) {
        return ReadStatus::End;
    }
    if (in.fail()) {
        if (extracted == 0) {
            return ReadStatus::End;  // nothing left to read
        }
        // Buffer filled before the delimiter: skip the rest of the line.
        in.clear();
        in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        return ReadStatus::TooLong;
    }
    // gcount() counts the extracted '\n' unless the line ended at EOF.
    const bool sawDelimiter = !in.eof();
    line = std::string_view(buffer.data(), sawDelimiter ? extracted - 1 : extracted);
    return ReadStatus::Line;
}

// One engine run: an order book wired to a CSV writer, plus line-level error reporting.
class Session {
public:
    // Connects the CSV writer to `out` and gives the book a pre-sized order index.
    Session(std::ostream& out, std::ostream& err) : writer_(out), book_(writer_, kExpectedOrders), err_(err) {}

    // Skips blank lines; otherwise parses the line and calls the handle() overload for the result.
    void process(std::string_view rawLine) {
        if (trim(rawLine).empty()) {
            return;
        }
        std::visit([this](const auto& parsed) { handle(parsed); }, parseLine(rawLine));
    }

    // Writes one diagnostic for the current line to `err` and counts it.
    void reportError(std::string_view message) {
        ++stats_.errors;
        err_ << "error: line " << stats_.lines << ": " << message << '\n';
    }

    // Counters for the session so far.
    SessionStats& stats() noexcept { return stats_; }

private:
    // Applies an add to the book; reports a duplicate live id or an invalid order.
    void handle(const AddOrderRequest& request) {
        switch (book_.add(request)) {
            case AddResult::Accepted:
                ++stats_.addsAccepted;
                break;
            case AddResult::DuplicateId:
                reportError("duplicate order id " + std::to_string(request.id) + " (order is still live)");
                break;
            case AddResult::InvalidOrder:
                reportError("invalid order " + std::to_string(request.id));
                break;
        }
    }

    // Applies a cancel to the book; reports an id that is not live.
    void handle(const CancelOrderRequest& request) {
        switch (book_.cancel(request.id)) {
            case CancelResult::Cancelled:
                ++stats_.cancelsApplied;
                break;
            case CancelResult::UnknownId:
                reportError("cancel for unknown order id " + std::to_string(request.id));
                break;
        }
    }

    // Reports a line the parser rejected.
    void handle(const ParseError& error) { reportError(error.message); }

    CsvEventWriter writer_;
    OrderBook<CsvEventWriter> book_;
    std::ostream& err_;
    SessionStats stats_;
};

}  // namespace

// Reads lines until EOF and processes each one inside a try/catch, so no line can end the run.
// Flushes `out` whenever no more input is buffered.
SessionStats runSession(std::istream& in, std::ostream& out, std::ostream& err) {
    Session session(out, err);
    std::array<char, kMaxLineLength + 1> buffer{};
    std::string_view line;

    while (true) {
        const ReadStatus status = readLine(in, buffer, line);
        if (status == ReadStatus::End) {
            break;
        }
        ++session.stats().lines;

        try {
            if (status == ReadStatus::TooLong) {
                session.reportError("line exceeds " + std::to_string(kMaxLineLength) + " characters");
            } else {
                session.process(line);
            }
        } catch (const std::exception& e) {
            // Only allocation failure can realistically get here; keep going.
            session.reportError(std::string("internal error: ") + e.what());
        }

        if (in.rdbuf()->in_avail() <= 0) {
            out.flush();
        }
    }
    out.flush();
    return session.stats();
}

}  // namespace matching

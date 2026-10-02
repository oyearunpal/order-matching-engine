#pragma once

#include <cstdint>
#include <iosfwd>

namespace matching {

struct SessionStats {
    std::uint64_t lines = 0;        // lines read, including blank and invalid ones
    std::uint64_t addsAccepted = 0;
    std::uint64_t cancelsApplied = 0;
    std::uint64_t errors = 0;       // lines rejected with a message on `err`
};

// Runs the matching engine over a stream of CSV requests.
//
// Reads `in` line by line, applies each request to a fresh order book, writes
// output events to `out` and one diagnostic per rejected line to `err`.
// Blank lines are ignored. No input can terminate the session early: malformed
// lines, unknown ids, over-long lines and binary junk are reported and skipped.
//
// `out` is flushed whenever `in` has no more buffered input, so piped batches
// stay fast while interactive use still sees results immediately.
SessionStats runSession(std::istream& in, std::ostream& out, std::ostream& err);

}  // namespace matching

#include <sstream>
#include <string>

#include "session.hpp"
#include "test_harness.hpp"

using matching::runSession;
using matching::SessionStats;

namespace {

struct SessionResult {
    std::string out;
    std::string err;
    SessionStats stats;
};

SessionResult run(const std::string& input) {
    std::istringstream in(input);
    std::ostringstream out;
    std::ostringstream err;
    const SessionStats stats = runSession(in, out, err);
    return {out.str(), err.str(), stats};
}

}  // namespace

TEST(SessionReproducesAssignmentExample) {
    const SessionResult result = run(
        "0,1000000,1,1,1075\n"
        "0,1000001,0,9,1000\n"
        "0,1000002,0,30,975\n"
        "0,1000003,1,10,1050\n"
        "0,1000004,0,10,950\n"
        "BADMESSAGE\n"
        "0,1000005,1,2,1025\n"
        "0,1000006,0,1,1000\n"
        "1,1000004\n"
        "0,1000007,1,5,1025\n"
        "0,1000008,0,3,1050\n");

    EXPECT_EQ(result.out, std::string("2,2,1025\n"
                                      "4,1000008,1\n"
                                      "3,1000005\n"
                                      "2,1,1025\n"
                                      "3,1000008\n"
                                      "4,1000007,4\n"));
    EXPECT_EQ(result.err, std::string("error: line 6: Unknown message type: BADMESSAGE\n"));
    EXPECT_EQ(result.stats.lines, std::uint64_t{11});
    EXPECT_EQ(result.stats.addsAccepted, std::uint64_t{9});
    EXPECT_EQ(result.stats.cancelsApplied, std::uint64_t{1});
    EXPECT_EQ(result.stats.errors, std::uint64_t{1});
}

TEST(SessionReportsBookRejectionsWithLineNumbers) {
    const SessionResult result = run(
        "0,1,1,5,100\n"
        "0,1,0,5,90\n"
        "1,77\n");
    EXPECT_EQ(result.out, std::string());
    EXPECT_EQ(result.err, std::string("error: line 2: duplicate order id 1 (order is still live)\n"
                                      "error: line 3: cancel for unknown order id 77\n"));
}

TEST(SessionSkipsBlankLinesAndHandlesCrlf) {
    const SessionResult result = run("\n   \n0,1,1,5,100\r\n\r\n0,2,0,5,100\r\n");
    EXPECT_EQ(result.out, std::string("2,5,100\n3,2\n3,1\n"));
    EXPECT_EQ(result.err, std::string());
    EXPECT_EQ(result.stats.lines, std::uint64_t{5});
}

TEST(SessionProcessesFinalLineWithoutNewline) {
    const SessionResult result = run("0,1,1,5,100\n0,2,0,5,100");
    EXPECT_EQ(result.out, std::string("2,5,100\n3,2\n3,1\n"));
}

TEST(SessionRejectsOverlongLineAndContinues) {
    const std::string huge(100000, '9');
    const SessionResult result = run("0,1,1,5,100\n" + huge + "\n0,2,0,5,100\n");
    EXPECT_EQ(result.out, std::string("2,5,100\n3,2\n3,1\n"));
    EXPECT_EQ(result.err, std::string("error: line 2: line exceeds 1024 characters\n"));
}

TEST(SessionAcceptsLineOfExactlyMaxLength) {
    // Pad a valid cancel with spaces up to exactly 1024 characters.
    std::string line = "1,5";
    line.resize(1024, ' ');
    const SessionResult result = run(line + "\n");
    EXPECT_EQ(result.err, std::string("error: line 1: cancel for unknown order id 5\n"));
}

TEST(SessionSurvivesBinaryGarbage) {
    std::string garbage;
    for (int i = 0; i < 256; ++i) {
        garbage += static_cast<char>(i);
    }
    const SessionResult result = run(garbage + "\n0,1,1,5,100\n0,2,0,5,100\n");
    EXPECT_EQ(result.out, std::string("2,5,100\n3,2\n3,1\n"));
    EXPECT_EQ(result.stats.errors, std::uint64_t{2});  // the garbage splits into two lines at byte 0x0a
}

TEST(SessionHandlesEmptyInput) {
    const SessionResult result = run("");
    EXPECT_EQ(result.out, std::string());
    EXPECT_EQ(result.err, std::string());
    EXPECT_EQ(result.stats.lines, std::uint64_t{0});
}

#pragma once
#include <stdexcept>
#include <cstdint>
namespace arli {
inline constexpr int kMaxActiveTasks=64;
inline constexpr int64_t kMaxActiveTaskBytes=8*1024*1024;
inline constexpr size_t kMaxTaskTextBytes=256*1024,kMaxTaskSnapshotBytes=256*1024;
inline constexpr int kMaxTurnStreams=16;
inline constexpr int kMaxToolCallsPerTurn=64;
inline constexpr int kMaxReadCallsPerTurn=40;
inline constexpr int kMaxWorkspaceMutationsPerTurn=16;
struct TaskLimitError:std::runtime_error {using std::runtime_error::runtime_error;};
}

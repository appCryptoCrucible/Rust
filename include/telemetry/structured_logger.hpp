#pragma once
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>

class StructuredLogger {
public:
  static StructuredLogger& Instance();
  // Enqueue a pre-built JSON line (one object, no trailing newline needed)
  void LogJsonLine(const std::string& json_line);
  // Graceful shutdown
  void Shutdown();
  // Initialize file path (default logs/metrics-YYYYMMDD.jsonl)
  void Initialize(const std::string& file_path);
private:
  StructuredLogger();
  ~StructuredLogger();
  void Worker();
  std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<std::string> queue_;
  std::thread worker_;
  bool running_ = false;
  std::string file_path_;
};



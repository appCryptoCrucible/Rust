#pragma once
#include <string>
#include <memory>
#include <fstream>
#include <mutex>
#include <chrono>
#include <queue>
#include <thread>
#include <condition_variable>

enum class LogLevel { DEBUG, INFO, WARNING, ERROR, CRITICAL };

struct LogEntry {
  std::chrono::system_clock::time_point timestamp;
  LogLevel level;
  std::string message;
  std::string file;
  int line;
  std::thread::id thread_id;
};

class Logger {
  static std::unique_ptr<Logger> instance_;
  static std::mutex instance_mutex_;
  std::ofstream log_file_;
  std::mutex log_mutex_;
  std::queue<LogEntry> log_queue_;
  std::thread worker_thread_;
  std::condition_variable cv_;
  bool running_ = false;
  LogLevel min_level_ = LogLevel::INFO;
  Logger() = default;
  void WorkerFunction();
  void WriteLogEntry(const LogEntry&);
  std::string FormatLogEntry(const LogEntry&);
  std::string LevelToString(LogLevel);
public:
  static void Initialize(const std::string& path, LogLevel min_level = LogLevel::INFO);
  static void Shutdown();
  static void Log(LogLevel level, const std::string& message, const std::string& file = __FILE__, int line = __LINE__);
  static void Debug(const std::string& m, const std::string& f = __FILE__, int l = __LINE__);
  static void Info(const std::string& m, const std::string& f = __FILE__, int l = __LINE__);
  static void Warning(const std::string& m, const std::string& f = __FILE__, int l = __LINE__);
  static void Error(const std::string& m, const std::string& f = __FILE__, int l = __LINE__);
  static void Critical(const std::string& m, const std::string& f = __FILE__, int l = __LINE__);
  ~Logger();
};

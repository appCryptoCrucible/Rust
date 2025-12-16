#include "telemetry/structured_logger.hpp"
#include <fstream>
#include <chrono>

StructuredLogger& StructuredLogger::Instance() {
  static StructuredLogger inst;
  return inst;
}

StructuredLogger::StructuredLogger() {}
StructuredLogger::~StructuredLogger() { Shutdown(); }

void StructuredLogger::Initialize(const std::string& file_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) {
    file_path_ = file_path;
    running_ = true;
    worker_ = std::thread(&StructuredLogger::Worker, this);
  }
}

void StructuredLogger::Shutdown() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void StructuredLogger::LogJsonLine(const std::string& json_line) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_) return;
    queue_.push(json_line);
  }
  cv_.notify_one();
}

void StructuredLogger::Worker() {
  std::ofstream out(file_path_, std::ios::app | std::ios::out);
  std::string batch;
  batch.reserve(8192);
  while (true) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(80), [&]{ return !queue_.empty() || !running_; });
    if (!running_ && queue_.empty()) break;
    while (!queue_.empty()) {
      batch.append(queue_.front());
      batch.push_back('\n');
      queue_.pop();
      if (batch.size() > 4096) break;
    }
    lock.unlock();
    if (!batch.empty()) {
      out << batch;
      out.flush();
      batch.clear();
    }
  }
}



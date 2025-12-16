#include "common/config_manager.hpp"
#include "common/logger.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

std::unordered_map<std::string, std::string> ConfigManager::cache_;

static inline std::string TrimWhitespace(const std::string& input) {
  auto start = input.begin();
  while (start != input.end() && std::isspace(static_cast<unsigned char>(*start))) ++start;
  auto end = input.end();
  do { --end; } while (end >= start && std::isspace(static_cast<unsigned char>(*end)));
  return std::string(start, end + 1);
}

void ConfigManager::Initialize(const std::string& env_path) {
  cache_.clear();
  LoadEnvFile(env_path);
}

void ConfigManager::LoadEnvFile(const std::string& env_path) {
  std::ifstream file(env_path);
  if (!file.is_open()) {
    Logger::Warning(".env file not found: " + env_path);
    return;
  }
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#') continue;
    auto pos = line.find('=');
    if (pos == std::string::npos) continue;
    std::string key = TrimWhitespace(line.substr(0, pos));
    std::string value = TrimWhitespace(line.substr(pos + 1));
    if (!key.empty()) cache_[key] = value;
  }
}

std::optional<std::string> ConfigManager::Get(const std::string& key) {
  auto it = cache_.find(key);
  if (it == cache_.end()) return std::nullopt;
  return it->second;
}

std::string ConfigManager::GetOrThrow(const std::string& key) {
  auto v = Get(key);
  if (!v) throw std::runtime_error("Missing required config: " + key);
  return *v;
}

int ConfigManager::GetIntOr(const std::string& key, int default_value) {
  auto v = Get(key);
  if (!v) return default_value;
  try { return std::stoi(*v); } catch (...) { return default_value; }
}

double ConfigManager::GetDoubleOr(const std::string& key, double default_value) {
  auto v = Get(key);
  if (!v) return default_value;
  try { return std::stod(*v); } catch (...) { return default_value; }
}

bool ConfigManager::GetBoolOr(const std::string& key, bool default_value) {
  auto v = Get(key);
  if (!v) return default_value;
  std::string s = *v;
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
  if (s == "1" || s == "true" || s == "yes") return true;
  if (s == "0" || s == "false" || s == "no") return false;
  return default_value;
}


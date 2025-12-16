#include "utils/json_rpc.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace JsonRpcUtil {
  std::string ExtractResult(const std::string& body) {
    auto j = json::parse(body);
    if (j.contains("error")) throw std::runtime_error(j["error"].dump());
    if (!j.contains("result")) throw std::runtime_error("missing result");
    if (j["result"].is_string()) return j["result"].get<std::string>();
    return j["result"].dump();
  }
  std::string ExtractFieldHex(const std::string& body, const std::string& field) {
    auto j = json::parse(body);
    if (j.contains("error")) return std::string();
    if (!j.contains("result")) return std::string();
    auto& r = j["result"];
    if (r.contains(field) && r[field].is_string()) return r[field].get<std::string>();
    return std::string();
  }
  std::string ExtractError(const std::string& body) {
    auto j = json::parse(body);
    if (j.contains("error")) return j["error"].dump();
    return std::string();
  }
}



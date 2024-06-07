#include <algorithm>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>

#define DELIMITER '_'
#define SEPARATOR '`'

std::string mapToString(
    const std::shared_ptr<std::map<std::string, std::string>> &mapPtr) {
  std::ostringstream oss;
  for (const auto &pair : *mapPtr) {
    oss << pair.first << DELIMITER << pair.second << SEPARATOR;
  }
  return oss.str();
}

std::shared_ptr<std::map<std::string, std::string>> stringToMap(
    const std::string &str) {
  auto mapPtr = std::make_shared<std::map<std::string, std::string>>();
  std::istringstream iss(str);
  std::string item;
  while (std::getline(iss, item, SEPARATOR)) {
    auto delimiterPos = item.find(DELIMITER);
    if (delimiterPos != std::string::npos) {
      std::string key = item.substr(0, delimiterPos);
      std::string value = item.substr(delimiterPos + 1);
      (*mapPtr)[key] = value;
    }
  }
  return mapPtr;
}

std::string listToString(
    const std::shared_ptr<std::list<std::string>> &listPtr) {
  std::ostringstream oss;
  for (const auto &item : *listPtr) {
    oss << item << SEPARATOR;
  }
  return oss.str();
}

std::shared_ptr<std::list<std::string>> stringToList(const std::string &str) {
  auto listPtr = std::make_shared<std::list<std::string>>();
  std::istringstream iss(str);
  std::string item;
  while (std::getline(iss, item, SEPARATOR)) {
    listPtr->push_back(item);
  }
  return listPtr;
}

std::string setToString(const std::shared_ptr<std::set<std::string>> &setPtr) {
  std::ostringstream oss;
  for (const auto &item : *setPtr) {
    oss << item << SEPARATOR;
  }
  return oss.str();
}

std::shared_ptr<std::set<std::string>> stringToSet(const std::string &str) {
  auto setPtr = std::make_shared<std::set<std::string>>();
  std::istringstream iss(str);
  std::string item;
  while (std::getline(iss, item, SEPARATOR)) {
    setPtr->insert(item);
  }
  return setPtr;
}

std::shared_ptr<std::set<std::pair<std::string, int>>> stringToZSet(
    const std::string &str) {
  auto zsetPtr = std::make_shared<std::set<std::pair<std::string, int>>>();
  std::istringstream iss(str);
  std::string item;
  while (std::getline(iss, item, SEPARATOR)) {
    auto delimiterPos = item.find(DELIMITER);
    if (delimiterPos != std::string::npos) {
      std::string key = item.substr(0, delimiterPos);
      int value = std::stoi(item.substr(delimiterPos + 1));
      zsetPtr->insert(std::make_pair(key, value));
    }
  }
  return zsetPtr;
}

std::string zSetToString(
    const std::shared_ptr<std::set<std::pair<std::string, int>>> &zsetPtr) {
  std::ostringstream oss;
  for (const auto &pair : *zsetPtr) {
    oss << pair.first << DELIMITER << pair.second << SEPARATOR;
  }
  return oss.str();
}

#undef DELIMITER
#undef SEPARATOR
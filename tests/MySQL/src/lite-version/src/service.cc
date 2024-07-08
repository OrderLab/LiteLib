#include "service.hpp"

#include <pg_query.h>

#include "mysql-server/protocol_classic.hpp"

std::pair<std::vector<std::shared_ptr<Packet>>, bool> MySQL::Match(
    const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
    lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>>
        &pending_requests) {
  // TODO: Find out the EOF packet of the response
  std::vector<std::shared_ptr<Packet>> related_reqs;
  bool forward_response = true;
  if (!conn.response_dissector.Digest(resp, conn.responses)) {
    return {related_reqs, forward_response};
  }
  if (!pending_requests.empty()) {
    do {
      auto [req, tmp_forward_response] = pending_requests.pop_front();
      forward_response = tmp_forward_response;
      related_reqs.emplace_back(std::move(req));
    } while (!pending_requests.empty() &&
             pending_requests.front().first->payload_length_ == 0xffffff);
  }
  if (related_reqs.empty()) {
    LOG(INFO) << "No related requests found" << std::endl;
  }
  return {related_reqs, forward_response};
}

void MySQL::NormalUpdate(const std::shared_ptr<Packet> &resp,
                         std::vector<std::shared_ptr<Packet>> requests,
                         ConnectionInfo &conn, Cache *cache) {
  if (requests.empty()) {
    return;
  }
  // LOG(INFO) << "req: " << requests.size() << std::endl;
  // for (auto &req : requests) {
  //   LOG(INFO) << "  req len: " << req->buffer->size() - 4 << std::endl;
  // }
  // LOG(INFO) << "resp: " << conn.responses.size() << std::endl;
  // for (auto &resp : conn.responses) {
  //   LOG(INFO) << "  resp len: " << resp->buffer->size() - 4 << std::endl;
  // }
  for (size_t i = 1; i < requests.size(); i++) {
    requests[0]->payload_length_ += requests[i]->payload_length_;
    requests[0]->buffer->insert(requests[0]->buffer->end(),
                                requests[i]->buffer->begin() + 4,
                                requests[i]->buffer->end());
    // TODO: use https://en.cppreference.com/w/cpp/ranges/join_with_view
  }
  requests[0]->buffer->push_back(0);

  COM_DATA req_com_data;
  enum_server_command req_cmd;
  if (!get_command_and_parse_packet(&req_com_data, &req_cmd,
                                    requests[0]->buffer->data() + 4,
                                    requests[0]->buffer->size() - 4)) {
    LOG(WARNING) << "Unable to parse the request packet, type: "
                 << (int)requests[0]->buffer->data()[4] << std::endl;
  }

  switch (req_cmd) {
    case COM_STMT_PREPARE: {
      if (!(*conn.responses[0]->buffer)[4]) {  // response code == Ok
        auto statement_id = uint4korr(&((*conn.responses[0]->buffer)[5]));
        LOG(INFO) << "COM_STMT_PREPARE: " << req_com_data.com_stmt_prepare.query
                  << " -> " << statement_id << std::endl;
        PreparedStatement statement;
        statement.query = std::string{req_com_data.com_stmt_prepare.query};
        statement.param_num = uint2korr(&((*conn.responses[0]->buffer)[11]));
        conn.prepared_statements[statement_id] = statement;
      }
      break;
    }
    case COM_STMT_EXECUTE: {
      auto [stmt_it, values] =
          DissectExecuteStatement(requests[0]->buffer->data() + 5, conn);
      // std::string query = stmt_it->second.query;
      // for (size_t i = 0; i < values.size(); i++) {
      //   query.replace(query.find("?"), 1, SerializeValue(values[i],
      //   stmt_it->second.types[i]));
      // }
      // LOG(INFO) << "COM_STMT_EXECUTE: " << query << std::endl;
    }
    default:
      break;
  }

  return;
}

void MySQL::HandleReplayResponse(const std::shared_ptr<Packet> &resp,
                                 std::vector<std::shared_ptr<Packet>> requests,
                                 ConnectionInfo &conn, Cache *cache) {
  return;
}

std::pair<Packet, bool> MySQL::EmergencyServe(std::shared_ptr<Packet> req,
                                              ConnectionInfo &conn,
                                              Cache *cache, Logger *logger,
                                              bool flow_control) {
  Packet resp;
  conn.request_payload.insert(conn.request_payload.end(),
                              req->buffer->begin() + 4, req->buffer->end());
  if (req->payload_length_ == 0xffffff) {  // incomplete payload
    return {resp, false};
  }
  conn.request_payload.push_back(0);

  COM_DATA req_com_data;
  enum_server_command req_cmd;
  if (!get_command_and_parse_packet(&req_com_data, &req_cmd,
                                    conn.request_payload.data(),
                                    conn.request_payload.size())) {
    LOG(WARNING) << "Unable to parse the request packet, type: "
                 << (int)conn.request_payload.data()[4] << std::endl;
  }

  switch (req_cmd) {
    case COM_QUERY: {
      std::string query{req_com_data.com_query.query};
      if (query == "BEGIN") {
        resp.buffer =
            std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{
                0x7, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x3, 0x0, 0x0, 0x0});
        break;  // TODO: handle it
      } else {
        LOG(WARNING) << "Unsupported query: " << query << std::endl;
        return {resp, true};
      }
    }
    case COM_STMT_EXECUTE: {
      auto [stmt_it, values] =
          DissectExecuteStatement(conn.request_payload.data() + 1, conn);
      std::string query = stmt_it->second.query;
      for (size_t i = 0; i < values.size(); i++) {
        query.replace(query.find("?"), 1,
                      SerializeValue(values[i], stmt_it->second.types[i]));
      }

      if (query == "COMMIT") {
        resp.buffer =
            std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{
                0x7, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0});
        break;  // TODO: handle it
      } else {
        auto cache_it = query_cache_.find(query);
        if (cache_it != query_cache_.end()) {
          resp.buffer = cache_it->second;
        } else {
          LOG(WARNING) << "Query not found in the cache: " << query
                       << std::endl;
          return {resp, true};
        }
      }

      break;
    }
    default: {
      LOG(WARNING) << "Unsupported command: " << req_cmd << std::endl;
      return {resp, true};
    }
  }

  conn.request_payload.clear();
  return {resp, false};
}

void MySQL::NormalToEmergencyHook() {
  if (!ParseQueryCache()) {
    LOG(ERROR) << "Unable to parse query cache";
  }
}
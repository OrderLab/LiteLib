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
      if (!(*conn.responses[0]->buffer)[4]) {
        auto statement_id = uint4korr(&((*conn.responses[0]->buffer)[5]));
        LOG(INFO) << "COM_STMT_PREPARE: " << req_com_data.com_stmt_prepare.query
                  << " -> " << statement_id << std::endl;
        conn.prepared_statements[statement_id] =
            std::string{req_com_data.com_stmt_prepare.query};

        PgQueryParseResult result;
        std::string s(req_com_data.com_stmt_prepare.query);
        std::replace(s.begin(), s.end(), '?', '1');
        result = pg_query_parse(s.c_str());
        printf("%s\n", result.parse_tree);
        pg_query_free_parse_result(result);
      }
      break;
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
  return {Packet{}, true};  // close the connection directly
}

void MySQL::NormalToEmergencyHook() {
  if (!ParseQueryCache()) {
    LOG(ERROR) << "Unable to parse query cache";
  }
}
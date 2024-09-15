#include "service.hpp"

#include <hsql/SQLParser.h>
#include <hsql/util/sqlhelper.h>

#include "mysql-server/protocol_classic.hpp"

MySQL::MySQL() : query_cache_(*this) {
  server_greeting_.buffer =
      std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{
          0x4a, 0x0,  0x0,  0x0,  0xa,  0x35, 0x2e, 0x37, 0x2e, 0x34,
          0x34, 0x0,  0x2,  0x0,  0x0,  0x0,  0x3d, 0x7d, 0x30, 0x52,
          0x19, 0x28, 0x47, 0x6c, 0x0,  0xff, 0xff, 0x8,  0x2,  0x0,
          0xff, 0xc1, 0x15, 0x0,  0x0,  0x0,  0x0,  0x0,  0x0,  0x0,
          0x0,  0x0,  0x0,  0x17, 0x8,  0x56, 0x44, 0x76, 0x19, 0x55,
          0x14, 0x30, 0x69, 0x7c, 0x5b, 0x0,  0x6d, 0x79, 0x73, 0x71,
          0x6c, 0x5f, 0x6e, 0x61, 0x74, 0x69, 0x76, 0x65, 0x5f, 0x70,
          0x61, 0x73, 0x73, 0x77, 0x6f, 0x72, 0x64, 0x0});

  PCHECK(notify_event_fd_ = eventfd(0, EFD_NONBLOCK))
      << "failed creating eventfd for mysql thread";

  struct event_config *ev_config;
  ev_config = event_config_new();
  event_config_set_flag(ev_config, EVENT_BASE_FLAG_NOLOCK);
  base_ = event_base_new_with_config(ev_config);
  event_config_free(ev_config);

  event_set(&notify_event_, notify_event_fd_, EV_READ | EV_PERSIST,
            NotifyHandler, this);

  event_base_set(base_, &notify_event_);

  LOG_IF(FATAL, event_add(&notify_event_, 0) == -1)
      << "Can't monitor libevent notify pipe\n";

  pthread_attr_t attr;

  pthread_attr_init(&attr);

  PCHECK(!pthread_create(&thread_id_, &attr, ThreadBody, this))
      << "Can't create thread: mysql_task_queue_worker" << std::endl;

  pthread_setname_np(thread_id_, "MySQL-worker");
  pthread_attr_destroy(&attr);
}

MySQL::~MySQL() {
  event_del(&notify_event_);
  event_base_free(base_);
  close(notify_event_fd_);
}

std::pair<std::vector<std::shared_ptr<Packet>>, bool> MySQL::Match(
    const std::shared_ptr<Packet> &resp, ConnectionInfo &conn,
    lite::ThreadSafeQueue<std::pair<std::shared_ptr<Packet>, bool>>
        &pending_requests) {
  pending_requests.clear();
  return {std::vector<std::shared_ptr<Packet>>{}, true};
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
  return;
  if (requests.empty()) {
    if (conn.state == ConnectionInfo::State::Init) {
      // server greeting
      server_greeting_.buffer = resp->buffer;
      conn.state = ConnectionInfo::State::ServerGreeted;
    }
    return;
  }

  for (size_t i = 1; i < requests.size(); i++) {
    requests[0]->payload_length_ += requests[i]->payload_length_;
    requests[0]->buffer->insert(requests[0]->buffer->end(),
                                requests[i]->buffer->begin() + 4,
                                requests[i]->buffer->end());
    // TODO: use https://en.cppreference.com/w/cpp/ranges/join_with_view
  }
  requests[0]->buffer->push_back(0);

  if (conn.state == ConnectionInfo::State::ServerGreeted) {
    conn.state = ConnectionInfo::State::LoggedIn;
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

  COM_DATA req_com_data;
  enum_server_command req_cmd;
  if (!get_command_and_parse_packet(&req_com_data, &req_cmd,
                                    requests[0]->buffer->data() + 4,
                                    requests[0]->buffer->size() - 4)) {
    LOG(WARNING) << "Unable to parse the request packet, type: "
                 << (int)requests[0]->buffer->data()[4] << std::endl;
  }

  // TODO: multiple queries in one request
  std::string query;
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
      query = stmt_it->second.query;
      for (size_t i = 0; i < values.size(); i++)
        query.replace(query.find("?"), 1, ValueToString(values[i]));
      break;
    }
    case COM_QUERY: {
      query = req_com_data.com_query.query;
      break;
    }
    default:
      break;
  }

  if (query.size()) {
    // notify_queue_.push_back({.type = MySQL::NormalTask::Type::kUpdateQuery,
    //                          .query = query,
    //                          .conn = &conn,
    //                          .cache = cache});
    // uint64_t buf = 1;
    // PLOG_IF(ERROR,
    //         write(notify_event_fd_, &buf, sizeof(uint64_t)) !=
    //         sizeof(uint64_t))
    //     << "failed writing to mysql eventfd";
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
  // TODO: flow_control
  Packet resp;
  conn.request_payload.insert(conn.request_payload.end(),
                              req->buffer->begin() + 4, req->buffer->end());
  if (req->payload_length_ == 0xffffff) {  // incomplete payload
    return {resp, false};
  }
  conn.request_payload.push_back(0);

  if (conn.state == ConnectionInfo::State::ServerGreeted) {
    resp.buffer = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{
        0x7, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0});
    conn.state = ConnectionInfo::State::LoggedIn;
    // TODO: handle login request
    conn.request_payload.clear();
    return {resp, false};
  }

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
      return EmergencyServeQuery(query, conn, cache, logger, flow_control);
    }
    case COM_STMT_EXECUTE: {
      auto [stmt_it, values] =
          DissectExecuteStatement(conn.request_payload.data() + 1, conn);
      std::string query = stmt_it->second.query;
      for (size_t i = 0; i < values.size(); i++)
        query.replace(query.find("?"), 1, ValueToString(values[i]));
      return EmergencyServeQuery(query, conn, cache, logger, flow_control);
    }
    case COM_PING: {
      resp.buffer = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{
          0x7, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0});
      break;
    }
    case COM_QUIT: {
      return {resp, false};
    }
    default: {
      LOG(WARNING) << "Unsupported command: " << req_cmd << std::endl;
      exit(1);
      return {resp, true};
    }
  }

  conn.request_payload.clear();
  return {resp, false};
}

#include <fstream>

void MySQL::NormalToEmergencyHook() {
  query_cache_.SendQueryToFull(nullptr);
  sleep(5);
  std::ofstream ofs("/root/lite.log");
  for (auto record : profiles) {
    ofs << record.timestamp.time_since_epoch().count() << " " << record.message
        << std::endl;
  }
  exit(0);
  if (!query_cache_.NormalToEmergencyHook(table_cache_, dangling_cache_)) {
    LOG(ERROR) << "Unable to parse query cache";
  }
}

void MySQL::EmergencyToNormalHook() { query_cache_.EmergencyToNormalHook(); }

Packet MySQL::EmergencyConnectionEstablishHook(ConnectionInfo &conn) {
  conn.state = ConnectionInfo::ServerGreeted;
  return server_greeting_;
}

void MySQL::NormalUpdateQuery(std::string &query, ConnectionInfo *conn,
                              Cache *cache) {
  hsql::SQLParserResult result;
  hsql::SQLParser::parse(query, &result);

  if (!result.isValid()) {
    LOG(ERROR) << "Unable to parse query: " << query << std::endl
               << "\t" << result.errorMsg() << " L" << result.errorLine() << ':'
               << result.errorColumn() << std::endl;
    return;
  }

  for (unsigned i = 0; i < result.size(); ++i) {
    auto stmt = result.getStatement(i);
    // hsql::printStatementInfo(stmt);
    switch (stmt->type()) {
      case hsql::StatementType::kStmtSelect: {
        break;
      }
      case hsql::StatementType::kStmtInsert: {
        auto insert_stmt = dynamic_cast<const hsql::InsertStatement *>(stmt);
        table_cache_.HandleInsert(*insert_stmt, cache, &query_cache_, false);
        break;
      }
      case hsql::StatementType::kStmtUpdate: {
        auto update_stmt = dynamic_cast<const hsql::UpdateStatement *>(stmt);
        if (!table_cache_.HandleUpdate(*update_stmt, cache, &query_cache_,
                                       false)) {
          query_cache_.InvalidateUnprocessableUpdateDuringNormal(update_stmt,
                                                                 table_cache_);
        }
        break;
      }
      case hsql::StatementType::kStmtDelete: {
        auto delete_stmt = dynamic_cast<const hsql::DeleteStatement *>(stmt);
        if (!table_cache_.HandleDelete(*delete_stmt, cache, &query_cache_,
                                       false)) {
          query_cache_.InvalidateUnprocessableDeleteDuringNormal(delete_stmt,
                                                                 table_cache_);
        }
        break;
      }
      case hsql::StatementType::kStmtTransaction: {
        // TODO
        break;
      }
      default: {
        // kStmtImport, kStmtCreate, kStmtDrop, kStmtPrepare, kStmtExecute,
        // kStmtExport, kStmtRename, kStmtAlter, kStmtShow, kStmtTransaction
        LOG(WARNING) << "Unsupported statement type: " << stmt->type()
                     << std::endl
                     << "\t" << query << std::endl;
      }
    }
  }
}

void *MySQL::ThreadBody(void *arg_self) {
  MySQL *self = static_cast<MySQL *>(arg_self);

  event_base_loop(self->base_, 0);
  event_base_free(self->base_);

  return NULL;
}

void MySQL::NotifyHandler(evutil_socket_t fd, short which, void *arg_self) {
  MySQL *self = static_cast<MySQL *>(arg_self);

  if (fd == self->notify_event_fd_) {
    uint64_t counter = 0;
    if (read(fd, &counter, sizeof(uint64_t)) != sizeof(uint64_t)) {
      LOG(ERROR) << "MySQL can't read from libevent pipe\n";
      return;
    }
    while (counter--) {
      NormalTask tsk = self->notify_queue_.pop_front();
      if (tsk.type == NormalTask::Type::kInsertCache) {
        self->query_cache_.HandleInvalidatedQueryBlockFromFull(
            tsk.query_cache_block_full_ptr, self->table_cache_,
            self->dangling_cache_);
      } else if (tsk.type == NormalTask::Type::kUpdateQuery) {
        self->NormalUpdateQuery(tsk.query, tsk.conn, tsk.cache);
      }
    }
  } else {
  }
}

std::pair<Packet, bool> MySQL::EmergencyServeQuery(std::string &query,
                                                   ConnectionInfo &conn,
                                                   Cache *cache, Logger *logger,
                                                   bool flow_control) {
  Packet resp;

  hsql::SQLParserResult result;
  hsql::SQLParser::parse(query, &result);

  if (!result.isValid()) {
    LOG(ERROR) << "Unable to parse query: " << query << std::endl
               << "\t" << result.errorMsg() << " L" << result.errorLine() << ':'
               << result.errorColumn() << std::endl;
    return {resp, true};
  }

  if (result.size() != 1) {
    LOG(WARNING) << "Multiple queries in one query string: " << query
                 << std::endl;
    return {resp, true};
  }

  auto stmt = result.getStatement(0);

  switch (stmt->type()) {
    case hsql::kStmtTransaction: {
      auto transaction_stmt =
          dynamic_cast<const hsql::TransactionStatement *>(stmt);
      switch (transaction_stmt->command) {
        case hsql::kBeginTransaction: {
          resp.buffer =
              std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{
                  0x7, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x3, 0x0, 0x0, 0x0});
          // TODO: handle it
          break;
        }
        case hsql::kCommitTransaction: {
          resp.buffer =
              std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{
                  0x7, 0x0, 0x0, 0x1, 0x0, 0x0, 0x0, 0x2, 0x0, 0x0, 0x0});
          // TODO: handle it
          break;
        }
        case hsql::kRollbackTransaction: {
          // TODO: handle it
          break;
        }
        default: {
          LOG(WARNING) << "Unsupported transaction command: "
                       << transaction_stmt->command << std::endl;
          return {resp, true};
        }
      }
      break;
    }
    case hsql::kStmtSelect: {
      auto result = query_cache_.ServeSelect(query);
      if (result.has_value()) {
        resp = std::move(result.value());
        break;
      }
      result = table_cache_.ServePointSelect(
          dynamic_cast<const hsql::SelectStatement &>(*stmt), cache);
      if (result.has_value()) {
        resp = std::move(result.value());
        break;
      }
      LOG(WARNING) << "Query not found in the cache: " << query << std::endl;
      return {resp, true};
    }
    case hsql::kStmtInsert: {
      auto insert_stmt = dynamic_cast<const hsql::InsertStatement *>(stmt);
      if (table_cache_.HandleInsert(*insert_stmt, cache, &query_cache_)) {
        resp.buffer = std::make_shared<std::vector<uint8_t>>(
            std::vector<uint8_t>{0xa, 0x0, 0x0, 0x1, 0x0, 0x1, 0xfd, 0x7b, 0xb0,
                                 0x7, 0x3, 0x0, 0x0, 0x0});
        // TODO: handle it
      } else {
        LOG(WARNING) << "Unable to handle insert statement: " << query
                     << std::endl;
        return {resp, true};
      }
      break;
    }
    case hsql::kStmtUpdate: {
      auto update_stmt = dynamic_cast<const hsql::UpdateStatement *>(stmt);
      if (table_cache_.HandleUpdate(*update_stmt, cache, &query_cache_)) {
        resp.buffer =
            std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{
                0x30, 0x0,  0x0,  0x1,  0x0,  0x1,  0x0,  0x3,  0x0,
                0x0,  0x0,  0x28, 0x52, 0x6f, 0x77, 0x73, 0x20, 0x6d,
                0x61, 0x74, 0x63, 0x68, 0x65, 0x64, 0x3a, 0x20, 0x31,
                0x20, 0x20, 0x43, 0x68, 0x61, 0x6e, 0x67, 0x65, 0x64,
                0x3a, 0x20, 0x31, 0x20, 0x20, 0x57, 0x61, 0x72, 0x6e,
                0x69, 0x6e, 0x67, 0x73, 0x3a, 0x20, 0x30});
        // TODO: handle it
      } else {
        LOG(WARNING) << "Unable to handle update statement: " << query
                     << std::endl;
        return {resp, true};
      }
      break;
    }
    case hsql::kStmtDelete: {
      // TODO
      auto delete_stmt = dynamic_cast<const hsql::DeleteStatement *>(stmt);
      if (table_cache_.HandleDelete(*delete_stmt, cache, &query_cache_)) {
        resp.buffer =
            std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{
                0x7, 0x0, 0x0, 0x1, 0x0, 0x1, 0x0, 0x3, 0x0, 0x0, 0x0});
        // TODO: handle it
      } else {
        LOG(WARNING) << "Unable to handle delete statement: " << query
                     << std::endl;
        return {resp, true};
      }
      break;
    }
  }

  conn.request_payload.clear();
  return {resp, false};
}

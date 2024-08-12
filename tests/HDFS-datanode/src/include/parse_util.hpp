#include <arpa/inet.h>
#include <google/protobuf/message.h>

#include <cstdint>
#include <memory>
#include <vector>

void WriteFixedInt32ToVector(uint32_t value,
                             std::shared_ptr<std::vector<uint8_t>> &buffer);
bool ReadDelimitedFrom(google::protobuf::io::CodedInputStream *coded_input,
                       google::protobuf::MessageLite *message);
bool WriteRpc(std::shared_ptr<std::vector<uint8_t>> &buffer,
              const std::vector<google::protobuf::MessageLite *> &messages);

bool WriteDelimitedTo(std::shared_ptr<std::vector<uint8_t>> &buffer,
                      const google::protobuf::MessageLite *messages);
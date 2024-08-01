#include "parse_util.hpp"

void WriteFixedInt32ToVector(uint32_t value,
                             std::shared_ptr<std::vector<uint8_t>> &buffer) {
  uint32_t nvalue = htonl(value);
  buffer->push_back(static_cast<uint8_t>(nvalue & 0xFF));
  buffer->push_back(static_cast<uint8_t>((nvalue >> 8) & 0xFF));
  buffer->push_back(static_cast<uint8_t>((nvalue >> 16) & 0xFF));
  buffer->push_back(static_cast<uint8_t>((nvalue >> 24) & 0xFF));
}

bool ReadDelimitedFrom(google::protobuf::io::CodedInputStream *coded_input,
                       google::protobuf::MessageLite *message) {
  // Read the size.
  uint32_t size;
  if (!coded_input->ReadVarint32(&size)) {
    return false;  // Failed to read size.
  }

  // Tell the stream not to read beyond that size.
  google::protobuf::io::CodedInputStream::Limit limit =
      coded_input->PushLimit(size);

  // Parse the message.
  if (!message->MergeFromCodedStream(coded_input)) {
    coded_input->PopLimit(limit);
    return false;  // Failed to parse message.
  }
  if (!coded_input->ConsumedEntireMessage()) {
    coded_input->PopLimit(limit);
    return false;  // Input is ill-formed.
  }

  // Release the limit.
  coded_input->PopLimit(limit);

  return true;
}

bool WriteDelimitedTo(
    std::shared_ptr<std::vector<uint8_t>> &buffer,
    const std::vector<google::protobuf::MessageLite *> &messages) {
  // write in the total size
  uint32_t total_size = 0;
  for (google::protobuf::MessageLite *message : messages) {
    uint32_t message_size = message->ByteSizeLong();
    total_size +=
        message_size +
        google::protobuf::io::CodedOutputStream::VarintSize32(message_size);
  }
  WriteFixedInt32ToVector(total_size, buffer);
  // write in each message
  for (google::protobuf::MessageLite *message : messages) {
    // Calculate the size of the message
    size_t message_size = message->ByteSizeLong();

    // Resize the vector to hold the additional data (message size + varint
    // size)
    size_t old_size = buffer->size();
    size_t varint_size = google::protobuf::io::CodedOutputStream::VarintSize32(
        static_cast<uint32_t>(message_size));
    buffer->resize(old_size + message_size + varint_size);

    // Use ArrayOutputStream with the new part of the buffer
    google::protobuf::io::ArrayOutputStream array_output_stream(
        buffer->data() + old_size, message_size + varint_size);
    google::protobuf::io::CodedOutputStream coded_output_stream(
        &array_output_stream);

    // Write the size of the message as a varint
    coded_output_stream.WriteVarint32(static_cast<uint32_t>(message_size));

    // Serialize the message
    message->SerializeWithCachedSizes(&coded_output_stream);
  }

  return true;  // The message was written successfully.
}
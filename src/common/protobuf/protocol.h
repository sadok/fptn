#pragma once

#include <ctime>
#include <random>
#include <string>
#include <stdexcept>

#include <protocol.pb.h>

#include <common/utils/utils.h>
#include <common/network/ip_packet.h>


namespace fptn::common::protobuf::protocol
{
    class ProcessingError : public std::runtime_error
    {
    public:
        explicit ProcessingError(const std::string& message)
            : std::runtime_error(message) {}
    };

    class MessageError : public std::runtime_error
    {
    public:
        explicit MessageError(const std::string& message)
            : std::runtime_error(message) {}
    };

    class UnsoportedProtocolVersion : public std::runtime_error
    {
    public:
        explicit UnsoportedProtocolVersion(const std::string& message)
            : std::runtime_error(message) {}
    };

    inline std::string getPayload(const std::string& raw)
    {
        fptn::protocol::Message message;
        if (!message.ParseFromString(raw)) {
            throw ProcessingError("Failed to parse Protobuf message.");
        } else if (message.protocol_version() != FPTN_PROTOBUF_PROTOCOL_VERSION) {
            throw UnsoportedProtocolVersion("Unsupported protocol version.");
        } else if (message.msg_type() == fptn::protocol::MSG_ERROR) {
            if (message.has_error()) {
                const auto& error = message.error();
                throw MessageError("Message error  "  + error.error_msg());
            }
            throw MessageError("Malformed error message.");
        } else if (message.msg_type() == fptn::protocol::MSG_IP_PACKET) {
            if (message.has_packet()) {
                const auto& packet = message.packet();
                return std::string(packet.payload().data(), packet.payload().size());
            } else {
                throw ProcessingError("Malformed IP packet.");
            }
        }
        throw ProcessingError("Unknown message type.");
    }

    inline std::string createPacket(fptn::common::network::IPPacketPtr packet)
    {
        fptn::protocol::Message message;

        message.set_protocol_version(FPTN_PROTOBUF_PROTOCOL_VERSION);
        message.set_msg_type(fptn::protocol::MSG_IP_PACKET);

        fptn::protocol::IPPacket* ipPacketPayload = message.mutable_packet();
        ipPacketPayload->set_payload(packet->toString());
#ifdef FPTN_ENABLE_PACKET_PADDING
        /**
         * Fill with random data to prevent issues related to TLS-inside-TLS.
         */
        static const std::string randomdata = fptn::common::utils::generateRandomString(FPTN_IP_PACKET_MAX_SIZE);
        const std::size_t currentPayloadSize = ipPacketPayload->payload().size();
        if (currentPayloadSize < FPTN_IP_PACKET_MAX_SIZE) {
            static std::mt19937 gen{std::random_device{}()};
            std::uniform_int_distribution<std::size_t> dist(currentPayloadSize, FPTN_IP_PACKET_MAX_SIZE);

            const std::size_t randomLength = dist(gen);
            const std::size_t paddingSize = randomLength - currentPayloadSize;

            ipPacketPayload->set_padding_data(randomdata.substr(currentPayloadSize, paddingSize-1));
        }
#endif
        std::string serializedData;
        if (!message.SerializeToString(&serializedData)) {
            throw std::runtime_error("Failed to serialize Message.");
        }
        return serializedData;
    }

    inline std::string createError(const std::string& errorMsg,
                            fptn::protocol::ErrorType errorType = fptn::protocol::ERROR_WRONG_VERSION)
    {
        fptn::protocol::Message message;

        message.set_protocol_version(FPTN_PROTOBUF_PROTOCOL_VERSION);
        message.set_msg_type(fptn::protocol::MSG_ERROR);

        auto* error_message = message.mutable_error();
        error_message->set_error_type(errorType);
        error_message->set_error_msg(errorMsg);

        std::string serializedData;
        if (!message.SerializeToString(&serializedData)) {
            throw std::runtime_error("Failed to serialize Error Message.");
        }
        return serializedData;
    }
}

#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <openssl/types.h>
//---------------------------------------------------------------------------
// AnyBlob - Universal Cloud Object Storage Library
// Dominik Durner, 2023
//
// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0
//---------------------------------------------------------------------------
namespace anyblob::network {
//---------------------------------------------------------------------------
struct HTTPSMessage;
class TLSContext;
class ConnectionManager;
class Socket;
enum class MessageFailureCode : uint16_t;
//---------------------------------------------------------------------------
/// The TLS interface
//---------------------------------------------------------------------------
/* anyblob |   OpenSSL
 *   |     |
 *    -------> SSL_read / SSL_write / SSL_connect
 *         |     /\    ||
 *         |     ||    \/
 *         |   socket BIO
 *   |     |     ||    /\
 *   |     |     \/    ||
 *    -------<  recv / send
 *   |     |
 *  socket backend
 * Adopted from https://www.openssl.org/docs/man3.1/man3/BIO_new_bio_pair.html
*/
//---------------------------------------------------------------------------
class TLSConnection {
    public:
    /// The progress of the TLS
    enum class Progress : uint16_t {
        Pending,
        Finished,
        Aborted
    };

    private:
    /// The corresponding message
    HTTPSMessage* _message;
    /// The SSL context
    TLSContext& _context;
    /// The SSL connection
    SSL* _ssl;
    /// The decoding buffer
    std::vector<uint8_t> _buffer;
    /// The buffer offset
    uint64_t _bufferOffset;
    /// The buffer length
    uint64_t _bufferLength;
    /// The number of bytes sent
    uint64_t _sent;
    /// The record OpenSSL wants sent
    std::span<const uint8_t> _record;
    /// Is a transfer in flight
    bool _pending;
    /// The hostname
    std::string _hostname;
    /// The port
    uint32_t _port;
    /// Verify the peer certificate
    bool _verifyPeer;

    public:
    /// The constructor
    TLSConnection(TLSContext& context);

    /// The destructor
    ~TLSConnection();

    /// Initialize SSL
    [[nodiscard]] bool init(HTTPSMessage* message);
    /// Destroy SSL state
    void destroy();
    /// Get the SSL/TLS context
    [[nodiscard]] inline TLSContext& getContext() const { return _context; }
    /// Is the peer certificate verified
    [[nodiscard]] inline bool verifiesPeer() const { return _verifyPeer; }
    /// Get the hostname
    [[nodiscard]] inline const std::string& getHostname() const { return _hostname; }
    /// Get the port
    [[nodiscard]] inline uint32_t getPort() const { return _port; }

    /// Recv a TLS encrypted message
    [[nodiscard]] Progress recv(ConnectionManager& connectionManager, char* buffer, int64_t bufferLength, int64_t& resultLength);
    /// Send a TLS encrypted message
    [[nodiscard]] Progress send(ConnectionManager& connectionManager, const char* buffer, int64_t bufferLength, int64_t& resultLength);
    /// SSL/TLS connect
    [[nodiscard]] Progress connect(ConnectionManager& connectionManager);
    /// SSL/TLS shutdown
    [[nodiscard]] Progress shutdown(ConnectionManager& connectionManager);

    private:
    /// Run an SSL operation and queue the transfer it needs
    template <typename F>
    Progress runOperation(ConnectionManager& connectionManager, F operation, int64_t& result, MessageFailureCode failure);
    /// Consume the completed events
    bool consumeCompletion(MessageFailureCode failure);
    /// Queue the next receive
    void queueReceive(Socket& socket);
    /// Queue the record for sending
    void queueSend(Socket& socket);
    /// Record a message failure
    void fail(MessageFailureCode failure);
    /// Read a ciphertext from the staging buffer
    uint64_t bioRead(std::span<uint8_t> out);
    /// Remember the record OpenSSL wants sent
    uint64_t bioWrite(std::span<const uint8_t> record);
    /// Abort on a rejected peer certificate
    Progress verifyCertificate(Progress status);

    /// Create the bio that moves the records over the socket
    static BIO* createBio();
};
//---------------------------------------------------------------------------
} // namespace anyblob::network

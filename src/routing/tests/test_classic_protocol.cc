/*
  Copyright (c) 2016, 2017, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <memory>

#include "logger.h"
#include "protocol/classic_protocol.h"
#include "mysqlrouter/routing.h"
#include "routing_mocks.h"
#include "mysql_routing.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

using ::testing::_;
using ::testing::Args;
using ::testing::DoAll;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;


class ClassicProtocolTest : public ::testing::Test {
protected:
  ClassicProtocolTest() {
    mock_socket_operations_.reset(new MockSocketOperations());
    sut_protocol_.reset(new ClassicProtocol(mock_socket_operations_.get()));
  }

  virtual void SetUp() {
    FD_ZERO(&readfds_);

    network_buffer_.resize(routing::kDefaultNetBufferLength);
    network_buffer_offset_ = 0;
    curr_pktnr_ = 0;
    handshake_done_ = false;
  }

  std::unique_ptr<MockSocketOperations> mock_socket_operations_;
  // the tested object:
  std::unique_ptr<BaseProtocol> sut_protocol_;

  void serialize_classic_packet_to_buffer(RoutingProtocolBuffer &buffer,
                                          size_t &buffer_offset,
                                          const mysql_protocol::Packet &packet
                                          ) {
    using diff_t = mysql_protocol::Packet::difference_type;
    std::copy(packet.begin(), packet.begin()+static_cast<diff_t>(packet.size()),
              buffer.begin()+static_cast<diff_t>(buffer_offset));
    buffer_offset += packet.size();
  }

  static constexpr int sender_socket_ = 1;
  static constexpr int receiver_socket_ = 2;

  fd_set readfds_;
  RoutingProtocolBuffer network_buffer_;
  size_t network_buffer_offset_;
  int curr_pktnr_;
  bool handshake_done_;
};

class ClassicProtocolRoutingTest: public ClassicProtocolTest {};

TEST_F(ClassicProtocolTest, OnBlockClientHostSuccess)
{
  // we expect the router sending fake response packet
  // to prevent MySQL server from bumping up connection error counter
  auto packet = mysql_protocol::HandshakeResponsePacket(1, {}, "ROUTER", "", "fake_router_login");

  EXPECT_CALL(*mock_socket_operations_, write(receiver_socket_, _, packet.size())).WillOnce(Return((ssize_t)packet.size()));

  const bool result = sut_protocol_->on_block_client_host(receiver_socket_, "routing");

  ASSERT_TRUE(result);
}

TEST_F(ClassicProtocolTest, OnBlockClientHostWriteFail)
{
  auto packet = mysql_protocol::HandshakeResponsePacket(1, {}, "ROUTER", "", "fake_router_login");

  EXPECT_CALL(*mock_socket_operations_, write(receiver_socket_, _, packet.size())).WillOnce(Return(-1));

  const bool result = sut_protocol_->on_block_client_host(receiver_socket_, "routing");

  ASSERT_FALSE(result);
}

TEST_F(ClassicProtocolTest, CopyPacketsFdNotSet)
{
  size_t report_bytes_read = 0xff;

  FD_CLR(sender_socket_, &readfds_);

  int result = sut_protocol_->copy_packets(sender_socket_, receiver_socket_, &readfds_, network_buffer_, &curr_pktnr_,
                                         handshake_done_, &report_bytes_read, true);

  ASSERT_TRUE(result==0);
  ASSERT_TRUE(report_bytes_read==0);
  ASSERT_FALSE(handshake_done_);
}

TEST_F(ClassicProtocolTest, CopyPacketsReadError)
{
  size_t report_bytes_read = 0xff;

  FD_SET(sender_socket_, &readfds_);

  EXPECT_CALL(*mock_socket_operations_, read(sender_socket_,_,_)).WillOnce(Return(-1));

  int result = sut_protocol_->copy_packets(sender_socket_, receiver_socket_, &readfds_, network_buffer_, &curr_pktnr_,
                                         handshake_done_, &report_bytes_read, true);

  ASSERT_FALSE(handshake_done_);
  ASSERT_EQ(-1, result);
}

TEST_F(ClassicProtocolTest, CopyPacketsHandshakeDoneOK)
{
  handshake_done_ = true;
  size_t report_bytes_read = 0xff;
  constexpr int PACKET_SIZE = 20;

  FD_SET(sender_socket_, &readfds_);

  EXPECT_CALL(*mock_socket_operations_, read(sender_socket_, &network_buffer_[0], network_buffer_.size())).WillOnce(Return(PACKET_SIZE));
  EXPECT_CALL(*mock_socket_operations_, write(receiver_socket_, &network_buffer_[0], PACKET_SIZE)).WillOnce(Return(PACKET_SIZE));

  int result = sut_protocol_->copy_packets(sender_socket_, receiver_socket_, &readfds_, network_buffer_, &curr_pktnr_,
                                         handshake_done_, &report_bytes_read, true);

  ASSERT_TRUE(handshake_done_);
  ASSERT_EQ(0, result);
  ASSERT_EQ(static_cast<size_t>(PACKET_SIZE), report_bytes_read);
}

TEST_F(ClassicProtocolTest, CopyPacketsHandshakeDoneWriteError)
{
  handshake_done_ = true;
  size_t report_bytes_read = 0xff;
  constexpr ssize_t PACKET_SIZE = 20;

  FD_SET(sender_socket_, &readfds_);

  EXPECT_CALL(*mock_socket_operations_, read(sender_socket_, &network_buffer_[0], network_buffer_.size())).
                                                                  WillOnce(Return(PACKET_SIZE));
  EXPECT_CALL(*mock_socket_operations_, write(receiver_socket_, &network_buffer_[0], 20)).WillOnce(Return(-1));

  int result = sut_protocol_->copy_packets(sender_socket_, receiver_socket_, &readfds_, network_buffer_, &curr_pktnr_,
                                       handshake_done_, &report_bytes_read, true);

  ASSERT_TRUE(handshake_done_);
  ASSERT_EQ(-1, result);
}

TEST_F(ClassicProtocolTest, CopyPacketsHandshakePacketTooSmall)
{
  size_t report_bytes_read = 3;
  FD_SET(sender_socket_, &readfds_);

  EXPECT_CALL(*mock_socket_operations_, read(sender_socket_, &network_buffer_[0], network_buffer_.size())).
                                                                  WillOnce(Return((ssize_t)report_bytes_read));

  int result = sut_protocol_->copy_packets(sender_socket_, receiver_socket_, &readfds_, network_buffer_, &curr_pktnr_,
                                       handshake_done_, &report_bytes_read, true);

  ASSERT_FALSE(handshake_done_);
  ASSERT_EQ(-1, result);
}

TEST_F(ClassicProtocolTest, CopyPacketsHandshakeInvalidPacketNumber)
{
  size_t report_bytes_read = 0xff;
  FD_SET(sender_socket_, &readfds_);
  constexpr int packet_no = 3;
  curr_pktnr_ = 1;

  auto error_packet = mysql_protocol::ErrorPacket(packet_no, 122, "Access denied", "HY004");
  serialize_classic_packet_to_buffer(network_buffer_, network_buffer_offset_, error_packet);

  EXPECT_CALL(*mock_socket_operations_, read(sender_socket_, &network_buffer_[0], network_buffer_.size())).
                                                                  WillOnce(Return((ssize_t)report_bytes_read));

  int result = sut_protocol_->copy_packets(sender_socket_, receiver_socket_, &readfds_, network_buffer_, &curr_pktnr_,
                                       handshake_done_, &report_bytes_read, true);

  ASSERT_FALSE(handshake_done_);
  ASSERT_EQ(-1, result);
}


TEST_F(ClassicProtocolTest, CopyPacketsHandshakeServerSendsError)
{
  size_t report_bytes_read = 0xff;
  FD_SET(sender_socket_, &readfds_);
  curr_pktnr_ = 1;

  auto error_packet = mysql_protocol::ErrorPacket(2, 0xaabb, "Access denied", "HY004", mysql_protocol::kClientProtocol41);

  serialize_classic_packet_to_buffer(network_buffer_, network_buffer_offset_, error_packet);

  EXPECT_CALL(*mock_socket_operations_, read(sender_socket_, &network_buffer_[0], network_buffer_.size())).
                                                                  WillOnce(Return((ssize_t)network_buffer_offset_));

  EXPECT_CALL(*mock_socket_operations_, write(receiver_socket_, _, network_buffer_offset_)).
                                                       WillOnce(Return((ssize_t)network_buffer_offset_));

  int result = sut_protocol_->copy_packets(sender_socket_, receiver_socket_, &readfds_, network_buffer_, &curr_pktnr_,
                                       handshake_done_, &report_bytes_read, true);

  // if the server sent error handshake is considered done
  ASSERT_EQ(2, curr_pktnr_);
  ASSERT_EQ(0, result);
}

TEST_F(ClassicProtocolTest, SendErrorOKMultipleWrites)
{
  EXPECT_CALL(*mock_socket_operations_, write(1, _, _)).Times(2).
                                                  WillOnce(Return(8)).
                                                  WillOnce(Return (10000));

  bool res = sut_protocol_->send_error(1, 55, "Error message", "HY000",
                                   "routing configuration name");

  ASSERT_TRUE(res);
}

TEST_F(ClassicProtocolTest, SendErrorWriteFail)
{
  auto set_errno = [&]() -> void {errno=15;};
  EXPECT_CALL(*mock_socket_operations_, write(1, _, _)).WillOnce(DoAll(InvokeWithoutArgs(set_errno), Return(-1)));

  bool res = sut_protocol_->send_error(1, 55, "Error message", "HY000",
                                   "routing configuration name");

  ASSERT_FALSE(res);
}

MATCHER_P(BufferEq, buf1,
           std::string(negation ? "Buffers content does not match" : "Buffers content matches"))
{
  if (buf1.size() != ::std::tr1::get<1>(arg))
    return false;

  return 0 == memcmp(buf1.data(), ::std::tr1::get<0>(arg), buf1.size());
}

// check if the proper error is sent by the router if there is no valid
// destination server
TEST_F(ClassicProtocolRoutingTest, NoValidDestinations) {

  MySQLRouting routing(routing::AccessMode::kReadOnly, 7001, Protocol::Type::kClassicProtocol,
                       "127.0.0.1", mysql_harness::Path(), "routing:test",
                       routing::kDefaultMaxConnections,
                       routing::kDefaultDestinationConnectionTimeout,
                       routing::kDefaultMaxConnectErrors,
                       routing::kDefaultClientConnectTimeout,
                       routing::kDefaultNetBufferLength,
                       mock_socket_operations_.get());


  constexpr int client_socket = 1;
  sockaddr_in6 client_addr;
  client_addr.sin6_family = AF_INET6;
  memset(&client_addr.sin6_addr, 0x0, sizeof(client_addr.sin6_addr));

  mock_socket_operations_->get_mysql_socket_fail(1);
  auto error_packet = mysql_protocol::ErrorPacket(0, 2003, "Can't connect to remote MySQL server for client '127.0.0.1:7001'", "HY000");
  const auto error_packet_size = static_cast<ssize_t>(error_packet.size());

  EXPECT_CALL(*mock_socket_operations_, write(client_socket, _, _))
          .With(Args<1,2>(BufferEq(error_packet)))
          .WillOnce(Return(error_packet_size));

  EXPECT_CALL(*mock_socket_operations_, shutdown(client_socket));
  EXPECT_CALL(*mock_socket_operations_, shutdown(-1));
  EXPECT_CALL(*mock_socket_operations_, close(client_socket));

  routing.set_destinations_from_csv("127.0.0.1:7004");
  routing.routing_select_thread(client_socket, *reinterpret_cast<sockaddr_storage*>(&client_addr));
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
  WSADATA wsaData;
  int iResult;
  iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (iResult != 0) {
    std::cout << "WSAStartup() failed\n";
    return 1;
  }
#endif
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

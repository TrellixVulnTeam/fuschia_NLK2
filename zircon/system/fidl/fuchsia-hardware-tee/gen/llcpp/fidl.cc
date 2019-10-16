// WARNING: This file is machine generated by fidlgen.

#include <fuchsia/hardware/tee/llcpp/fidl.h>
#include <memory>

namespace llcpp {

namespace fuchsia {
namespace hardware {
namespace tee {

namespace {

[[maybe_unused]]
constexpr uint64_t kDeviceConnector_ConnectTee_Ordinal = 0x5b2280b900000000lu;
[[maybe_unused]]
constexpr uint64_t kDeviceConnector_ConnectTee_GenOrdinal = 0x37d7bf45f48faed6lu;
extern "C" const fidl_type_t fuchsia_hardware_tee_DeviceConnectorConnectTeeRequestTable;

}  // namespace

DeviceConnector::ResultOf::ConnectTee_Impl::ConnectTee_Impl(zx::unowned_channel _client_end, ::zx::channel service_provider, ::zx::channel tee_request) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<ConnectTeeRequest, ::fidl::MessageDirection::kSending>();
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
  uint8_t* _write_bytes = _write_bytes_array.view().data();
  memset(_write_bytes, 0, ConnectTeeRequest::PrimarySize);
  auto& _request = *reinterpret_cast<ConnectTeeRequest*>(_write_bytes);
  _request.service_provider = std::move(service_provider);
  _request.tee_request = std::move(tee_request);
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(ConnectTeeRequest));
  ::fidl::DecodedMessage<ConnectTeeRequest> _decoded_request(std::move(_request_bytes));
  Super::operator=(
      DeviceConnector::InPlace::ConnectTee(std::move(_client_end), std::move(_decoded_request)));
}

DeviceConnector::ResultOf::ConnectTee DeviceConnector::SyncClient::ConnectTee(::zx::channel service_provider, ::zx::channel tee_request) {
  return ResultOf::ConnectTee(zx::unowned_channel(this->channel_), std::move(service_provider), std::move(tee_request));
}

DeviceConnector::ResultOf::ConnectTee DeviceConnector::Call::ConnectTee(zx::unowned_channel _client_end, ::zx::channel service_provider, ::zx::channel tee_request) {
  return ResultOf::ConnectTee(std::move(_client_end), std::move(service_provider), std::move(tee_request));
}


DeviceConnector::UnownedResultOf::ConnectTee_Impl::ConnectTee_Impl(zx::unowned_channel _client_end, ::fidl::BytePart _request_buffer, ::zx::channel service_provider, ::zx::channel tee_request) {
  if (_request_buffer.capacity() < ConnectTeeRequest::PrimarySize) {
    Super::status_ = ZX_ERR_BUFFER_TOO_SMALL;
    Super::error_ = ::fidl::internal::kErrorRequestBufferTooSmall;
    return;
  }
  memset(_request_buffer.data(), 0, ConnectTeeRequest::PrimarySize);
  auto& _request = *reinterpret_cast<ConnectTeeRequest*>(_request_buffer.data());
  _request.service_provider = std::move(service_provider);
  _request.tee_request = std::move(tee_request);
  _request_buffer.set_actual(sizeof(ConnectTeeRequest));
  ::fidl::DecodedMessage<ConnectTeeRequest> _decoded_request(std::move(_request_buffer));
  Super::operator=(
      DeviceConnector::InPlace::ConnectTee(std::move(_client_end), std::move(_decoded_request)));
}

DeviceConnector::UnownedResultOf::ConnectTee DeviceConnector::SyncClient::ConnectTee(::fidl::BytePart _request_buffer, ::zx::channel service_provider, ::zx::channel tee_request) {
  return UnownedResultOf::ConnectTee(zx::unowned_channel(this->channel_), std::move(_request_buffer), std::move(service_provider), std::move(tee_request));
}

DeviceConnector::UnownedResultOf::ConnectTee DeviceConnector::Call::ConnectTee(zx::unowned_channel _client_end, ::fidl::BytePart _request_buffer, ::zx::channel service_provider, ::zx::channel tee_request) {
  return UnownedResultOf::ConnectTee(std::move(_client_end), std::move(_request_buffer), std::move(service_provider), std::move(tee_request));
}

::fidl::internal::StatusAndError DeviceConnector::InPlace::ConnectTee(zx::unowned_channel _client_end, ::fidl::DecodedMessage<ConnectTeeRequest> params) {
  DeviceConnector::SetTransactionHeaderFor::ConnectTeeRequest(params);
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::internal::StatusAndError::FromFailure(
        std::move(_encode_request_result));
  }
  zx_status_t _write_status =
      ::fidl::Write(std::move(_client_end), std::move(_encode_request_result.message));
  if (_write_status != ZX_OK) {
    return ::fidl::internal::StatusAndError(_write_status, ::fidl::internal::kErrorWriteFailed);
  } else {
    return ::fidl::internal::StatusAndError(ZX_OK, nullptr);
  }
}


bool DeviceConnector::TryDispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  if (msg->num_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_INVALID_ARGS);
    return true;
  }
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  switch (hdr->ordinal) {
    case kDeviceConnector_ConnectTee_Ordinal:
    case kDeviceConnector_ConnectTee_GenOrdinal:
    {
      auto result = ::fidl::DecodeAs<ConnectTeeRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      auto message = result.message.message();
      impl->ConnectTee(std::move(message->service_provider), std::move(message->tee_request),
        Interface::ConnectTeeCompleter::Sync(txn));
      return true;
    }
    default: {
      return false;
    }
  }
}

bool DeviceConnector::Dispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  bool found = TryDispatch(impl, msg, txn);
  if (!found) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_NOT_SUPPORTED);
  }
  return found;
}



void DeviceConnector::SetTransactionHeaderFor::ConnectTeeRequest(const ::fidl::DecodedMessage<DeviceConnector::ConnectTeeRequest>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDeviceConnector_ConnectTee_Ordinal);
}

}  // namespace tee
}  // namespace hardware
}  // namespace fuchsia
}  // namespace llcpp

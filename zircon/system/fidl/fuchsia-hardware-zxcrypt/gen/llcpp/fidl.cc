// WARNING: This file is machine generated by fidlgen.

#include <fuchsia/hardware/zxcrypt/llcpp/fidl.h>
#include <memory>

namespace llcpp {

namespace fuchsia {
namespace hardware {
namespace zxcrypt {

namespace {

[[maybe_unused]]
constexpr uint64_t kDeviceManager_Unseal_Ordinal = 0x7fb13b1100000000lu;
[[maybe_unused]]
constexpr uint64_t kDeviceManager_Unseal_GenOrdinal = 0x77a38acfb1b33056lu;
extern "C" const fidl_type_t fuchsia_hardware_zxcrypt_DeviceManagerUnsealRequestTable;
extern "C" const fidl_type_t fuchsia_hardware_zxcrypt_DeviceManagerUnsealResponseTable;
[[maybe_unused]]
constexpr uint64_t kDeviceManager_Seal_Ordinal = 0x3ecfec3100000000lu;
[[maybe_unused]]
constexpr uint64_t kDeviceManager_Seal_GenOrdinal = 0x6b740d57dd46950blu;
extern "C" const fidl_type_t fuchsia_hardware_zxcrypt_DeviceManagerSealResponseTable;

}  // namespace
template <>
DeviceManager::ResultOf::Unseal_Impl<DeviceManager::UnsealResponse>::Unseal_Impl(zx::unowned_channel _client_end, ::fidl::VectorView<uint8_t> key, uint8_t slot) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<UnsealRequest, ::fidl::MessageDirection::kSending>();
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
  UnsealRequest _request = {};
  _request.key = std::move(key);
  _request.slot = std::move(slot);
  auto _linearize_result = ::fidl::Linearize(&_request, _write_bytes_array.view());
  if (_linearize_result.status != ZX_OK) {
    Super::SetFailure(std::move(_linearize_result));
    return;
  }
  ::fidl::DecodedMessage<UnsealRequest> _decoded_request = std::move(_linearize_result.message);
  Super::SetResult(
      DeviceManager::InPlace::Unseal(std::move(_client_end), std::move(_decoded_request), Super::response_buffer()));
}

DeviceManager::ResultOf::Unseal DeviceManager::SyncClient::Unseal(::fidl::VectorView<uint8_t> key, uint8_t slot) {
  return ResultOf::Unseal(zx::unowned_channel(this->channel_), std::move(key), std::move(slot));
}

DeviceManager::ResultOf::Unseal DeviceManager::Call::Unseal(zx::unowned_channel _client_end, ::fidl::VectorView<uint8_t> key, uint8_t slot) {
  return ResultOf::Unseal(std::move(_client_end), std::move(key), std::move(slot));
}

template <>
DeviceManager::UnownedResultOf::Unseal_Impl<DeviceManager::UnsealResponse>::Unseal_Impl(zx::unowned_channel _client_end, ::fidl::BytePart _request_buffer, ::fidl::VectorView<uint8_t> key, uint8_t slot, ::fidl::BytePart _response_buffer) {
  if (_request_buffer.capacity() < UnsealRequest::PrimarySize) {
    Super::SetFailure(::fidl::DecodeResult<UnsealResponse>(ZX_ERR_BUFFER_TOO_SMALL, ::fidl::internal::kErrorRequestBufferTooSmall));
    return;
  }
  UnsealRequest _request = {};
  _request.key = std::move(key);
  _request.slot = std::move(slot);
  auto _linearize_result = ::fidl::Linearize(&_request, std::move(_request_buffer));
  if (_linearize_result.status != ZX_OK) {
    Super::SetFailure(std::move(_linearize_result));
    return;
  }
  ::fidl::DecodedMessage<UnsealRequest> _decoded_request = std::move(_linearize_result.message);
  Super::SetResult(
      DeviceManager::InPlace::Unseal(std::move(_client_end), std::move(_decoded_request), std::move(_response_buffer)));
}

DeviceManager::UnownedResultOf::Unseal DeviceManager::SyncClient::Unseal(::fidl::BytePart _request_buffer, ::fidl::VectorView<uint8_t> key, uint8_t slot, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Unseal(zx::unowned_channel(this->channel_), std::move(_request_buffer), std::move(key), std::move(slot), std::move(_response_buffer));
}

DeviceManager::UnownedResultOf::Unseal DeviceManager::Call::Unseal(zx::unowned_channel _client_end, ::fidl::BytePart _request_buffer, ::fidl::VectorView<uint8_t> key, uint8_t slot, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Unseal(std::move(_client_end), std::move(_request_buffer), std::move(key), std::move(slot), std::move(_response_buffer));
}

::fidl::DecodeResult<DeviceManager::UnsealResponse> DeviceManager::InPlace::Unseal(zx::unowned_channel _client_end, ::fidl::DecodedMessage<UnsealRequest> params, ::fidl::BytePart response_buffer) {
  DeviceManager::SetTransactionHeaderFor::UnsealRequest(params);
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<DeviceManager::UnsealResponse>::FromFailure(
        std::move(_encode_request_result));
  }
  auto _call_result = ::fidl::Call<UnsealRequest, UnsealResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<DeviceManager::UnsealResponse>::FromFailure(
        std::move(_call_result));
  }
  return ::fidl::Decode(std::move(_call_result.message));
}

template <>
DeviceManager::ResultOf::Seal_Impl<DeviceManager::SealResponse>::Seal_Impl(zx::unowned_channel _client_end) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<SealRequest, ::fidl::MessageDirection::kSending>();
  ::fidl::internal::AlignedBuffer<_kWriteAllocSize> _write_bytes_inlined;
  auto& _write_bytes_array = _write_bytes_inlined;
  uint8_t* _write_bytes = _write_bytes_array.view().data();
  memset(_write_bytes, 0, SealRequest::PrimarySize);
  ::fidl::BytePart _request_bytes(_write_bytes, _kWriteAllocSize, sizeof(SealRequest));
  ::fidl::DecodedMessage<SealRequest> _decoded_request(std::move(_request_bytes));
  Super::SetResult(
      DeviceManager::InPlace::Seal(std::move(_client_end), Super::response_buffer()));
}

DeviceManager::ResultOf::Seal DeviceManager::SyncClient::Seal() {
  return ResultOf::Seal(zx::unowned_channel(this->channel_));
}

DeviceManager::ResultOf::Seal DeviceManager::Call::Seal(zx::unowned_channel _client_end) {
  return ResultOf::Seal(std::move(_client_end));
}

template <>
DeviceManager::UnownedResultOf::Seal_Impl<DeviceManager::SealResponse>::Seal_Impl(zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  FIDL_ALIGNDECL uint8_t _write_bytes[sizeof(SealRequest)] = {};
  ::fidl::BytePart _request_buffer(_write_bytes, sizeof(_write_bytes));
  memset(_request_buffer.data(), 0, SealRequest::PrimarySize);
  _request_buffer.set_actual(sizeof(SealRequest));
  ::fidl::DecodedMessage<SealRequest> _decoded_request(std::move(_request_buffer));
  Super::SetResult(
      DeviceManager::InPlace::Seal(std::move(_client_end), std::move(_response_buffer)));
}

DeviceManager::UnownedResultOf::Seal DeviceManager::SyncClient::Seal(::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Seal(zx::unowned_channel(this->channel_), std::move(_response_buffer));
}

DeviceManager::UnownedResultOf::Seal DeviceManager::Call::Seal(zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer) {
  return UnownedResultOf::Seal(std::move(_client_end), std::move(_response_buffer));
}

::fidl::DecodeResult<DeviceManager::SealResponse> DeviceManager::InPlace::Seal(zx::unowned_channel _client_end, ::fidl::BytePart response_buffer) {
  constexpr uint32_t _write_num_bytes = sizeof(SealRequest);
  ::fidl::internal::AlignedBuffer<_write_num_bytes> _write_bytes;
  ::fidl::BytePart _request_buffer = _write_bytes.view();
  _request_buffer.set_actual(_write_num_bytes);
  ::fidl::DecodedMessage<SealRequest> params(std::move(_request_buffer));
  DeviceManager::SetTransactionHeaderFor::SealRequest(params);
  auto _encode_request_result = ::fidl::Encode(std::move(params));
  if (_encode_request_result.status != ZX_OK) {
    return ::fidl::DecodeResult<DeviceManager::SealResponse>::FromFailure(
        std::move(_encode_request_result));
  }
  auto _call_result = ::fidl::Call<SealRequest, SealResponse>(
    std::move(_client_end), std::move(_encode_request_result.message), std::move(response_buffer));
  if (_call_result.status != ZX_OK) {
    return ::fidl::DecodeResult<DeviceManager::SealResponse>::FromFailure(
        std::move(_call_result));
  }
  return ::fidl::Decode(std::move(_call_result.message));
}


bool DeviceManager::TryDispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  if (msg->num_bytes < sizeof(fidl_message_header_t)) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_INVALID_ARGS);
    return true;
  }
  fidl_message_header_t* hdr = reinterpret_cast<fidl_message_header_t*>(msg->bytes);
  switch (hdr->ordinal) {
    case kDeviceManager_Unseal_Ordinal:
    case kDeviceManager_Unseal_GenOrdinal:
    {
      auto result = ::fidl::DecodeAs<UnsealRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      auto message = result.message.message();
      impl->Unseal(std::move(message->key), std::move(message->slot),
        Interface::UnsealCompleter::Sync(txn));
      return true;
    }
    case kDeviceManager_Seal_Ordinal:
    case kDeviceManager_Seal_GenOrdinal:
    {
      auto result = ::fidl::DecodeAs<SealRequest>(msg);
      if (result.status != ZX_OK) {
        txn->Close(ZX_ERR_INVALID_ARGS);
        return true;
      }
      impl->Seal(
        Interface::SealCompleter::Sync(txn));
      return true;
    }
    default: {
      return false;
    }
  }
}

bool DeviceManager::Dispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
  bool found = TryDispatch(impl, msg, txn);
  if (!found) {
    zx_handle_close_many(msg->handles, msg->num_handles);
    txn->Close(ZX_ERR_NOT_SUPPORTED);
  }
  return found;
}


void DeviceManager::Interface::UnsealCompleterBase::Reply(int32_t status) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<UnsealResponse, ::fidl::MessageDirection::kSending>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _response = *reinterpret_cast<UnsealResponse*>(_write_bytes);
  DeviceManager::SetTransactionHeaderFor::UnsealResponse(
      ::fidl::DecodedMessage<UnsealResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              UnsealResponse::PrimarySize,
              UnsealResponse::PrimarySize)));
  _response.status = std::move(status);
  ::fidl::BytePart _response_bytes(_write_bytes, _kWriteAllocSize, sizeof(UnsealResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<UnsealResponse>(std::move(_response_bytes)));
}

void DeviceManager::Interface::UnsealCompleterBase::Reply(::fidl::BytePart _buffer, int32_t status) {
  if (_buffer.capacity() < UnsealResponse::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  auto& _response = *reinterpret_cast<UnsealResponse*>(_buffer.data());
  DeviceManager::SetTransactionHeaderFor::UnsealResponse(
      ::fidl::DecodedMessage<UnsealResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              UnsealResponse::PrimarySize,
              UnsealResponse::PrimarySize)));
  _response.status = std::move(status);
  _buffer.set_actual(sizeof(UnsealResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<UnsealResponse>(std::move(_buffer)));
}

void DeviceManager::Interface::UnsealCompleterBase::Reply(::fidl::DecodedMessage<UnsealResponse> params) {
  DeviceManager::SetTransactionHeaderFor::UnsealResponse(params);
  CompleterBase::SendReply(std::move(params));
}


void DeviceManager::Interface::SealCompleterBase::Reply(int32_t status) {
  constexpr uint32_t _kWriteAllocSize = ::fidl::internal::ClampedMessageSize<SealResponse, ::fidl::MessageDirection::kSending>();
  FIDL_ALIGNDECL uint8_t _write_bytes[_kWriteAllocSize] = {};
  auto& _response = *reinterpret_cast<SealResponse*>(_write_bytes);
  DeviceManager::SetTransactionHeaderFor::SealResponse(
      ::fidl::DecodedMessage<SealResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              SealResponse::PrimarySize,
              SealResponse::PrimarySize)));
  _response.status = std::move(status);
  ::fidl::BytePart _response_bytes(_write_bytes, _kWriteAllocSize, sizeof(SealResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<SealResponse>(std::move(_response_bytes)));
}

void DeviceManager::Interface::SealCompleterBase::Reply(::fidl::BytePart _buffer, int32_t status) {
  if (_buffer.capacity() < SealResponse::PrimarySize) {
    CompleterBase::Close(ZX_ERR_INTERNAL);
    return;
  }
  auto& _response = *reinterpret_cast<SealResponse*>(_buffer.data());
  DeviceManager::SetTransactionHeaderFor::SealResponse(
      ::fidl::DecodedMessage<SealResponse>(
          ::fidl::BytePart(reinterpret_cast<uint8_t*>(&_response),
              SealResponse::PrimarySize,
              SealResponse::PrimarySize)));
  _response.status = std::move(status);
  _buffer.set_actual(sizeof(SealResponse));
  CompleterBase::SendReply(::fidl::DecodedMessage<SealResponse>(std::move(_buffer)));
}

void DeviceManager::Interface::SealCompleterBase::Reply(::fidl::DecodedMessage<SealResponse> params) {
  DeviceManager::SetTransactionHeaderFor::SealResponse(params);
  CompleterBase::SendReply(std::move(params));
}



void DeviceManager::SetTransactionHeaderFor::UnsealRequest(const ::fidl::DecodedMessage<DeviceManager::UnsealRequest>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDeviceManager_Unseal_Ordinal);
}
void DeviceManager::SetTransactionHeaderFor::UnsealResponse(const ::fidl::DecodedMessage<DeviceManager::UnsealResponse>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDeviceManager_Unseal_Ordinal);
}

void DeviceManager::SetTransactionHeaderFor::SealRequest(const ::fidl::DecodedMessage<DeviceManager::SealRequest>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDeviceManager_Seal_Ordinal);
}
void DeviceManager::SetTransactionHeaderFor::SealResponse(const ::fidl::DecodedMessage<DeviceManager::SealResponse>& _msg) {
  fidl_init_txn_header(&_msg.message()->_hdr, 0, kDeviceManager_Seal_Ordinal);
}

}  // namespace zxcrypt
}  // namespace hardware
}  // namespace fuchsia
}  // namespace llcpp

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/network/mdns/service/instance_responder.h"

#include <algorithm>

#include "src/connectivity/network/mdns/service/mdns_names.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/time/time_point.h"

namespace mdns {

InstanceResponder::InstanceResponder(MdnsAgent::Host* host, const std::string& service_name,
                                     const std::string& instance_name, Mdns::Publisher* publisher)
    : MdnsAgent(host),
      service_name_(service_name),
      instance_name_(instance_name),
      instance_full_name_(MdnsNames::LocalInstanceFullName(instance_name, service_name)),
      publisher_(publisher) {}

InstanceResponder::~InstanceResponder() {}

void InstanceResponder::Start(const std::string& host_full_name, const MdnsAddresses& addresses) {
  FXL_DCHECK(!host_full_name.empty());

  MdnsAgent::Start(host_full_name, addresses);

  host_full_name_ = host_full_name;

  Reannounce();
}

void InstanceResponder::ReceiveQuestion(const DnsQuestion& question,
                                        const ReplyAddress& reply_address) {
  std::string name = question.name_.dotted_string_;
  std::string subtype;

  switch (question.type_) {
    case DnsType::kPtr:
      if (MdnsNames::MatchServiceName(name, service_name_, &subtype)) {
        GetAndSendPublication(true, subtype, reply_address);
      } else if (question.name_.dotted_string_ == MdnsNames::kAnyServiceFullName) {
        SendAnyServiceResponse(reply_address);
      }
      break;
    case DnsType::kSrv:
    case DnsType::kTxt:
      if (question.name_.dotted_string_ == instance_full_name_) {
        GetAndSendPublication(true, "", reply_address);
      }
      break;
    case DnsType::kAny:
      if (question.name_.dotted_string_ == instance_full_name_ ||
          MdnsNames::MatchServiceName(name, service_name_, &subtype)) {
        GetAndSendPublication(true, subtype, reply_address);
      }
      break;
    default:
      break;
  }
}

void InstanceResponder::Quit() {
  if (started()) {
    SendGoodbye();
  }
  
  RemoveSelf(instance_full_name_);

  publisher_ = nullptr;
}

void InstanceResponder::ReportSuccess(bool success) {
  if (publisher_) {
    publisher_->ReportSuccess(success);
  }
}

void InstanceResponder::SetSubtypes(std::vector<std::string> subtypes) {
  if (!started()) {
    // This agent isn't started, so we can't announce yet. There's no need to
    // remove old subtypes, because no subtypes have been announced yet.
    // |Reannounce| will be called by |Start|.
    subtypes_ = std::move(subtypes);
    return;
  }

  // Initiate four announcements with intervals of 1, 2 and 4 seconds. If we
  // were already announcing, the sequence restarts now. The first announcement
  // contains PTR records for the removed subtypes with TTL of zero.
  for (const std::string& subtype : subtypes_) {
    if (std::find(subtypes.begin(), subtypes.end(), subtype) == subtypes.end()) {
      SendSubtypePtrRecord(subtype, 0, addresses().multicast_reply());
    }
  }

  subtypes_ = std::move(subtypes);

  Reannounce();
}

void InstanceResponder::Reannounce() {
  if (!started()) {
    // This agent isn't started, so we can't announce yet. |Reannounce| will be called by |Start|.
    return;
  }

  // Initiate four announcements with intervals of 1, 2 and 4 seconds. If we
  // were already announcing, the sequence restarts now.
  announcement_interval_ = kInitialAnnouncementInterval;
  SendAnnouncement();
}

void InstanceResponder::SendAnnouncement() {
  GetAndSendPublication(false, "", addresses().multicast_reply());

  for (const std::string& subtype : subtypes_) {
    SendSubtypePtrRecord(subtype, DnsResource::kShortTimeToLive, addresses().multicast_reply());
  }

  if (announcement_interval_ > kMaxAnnouncementInterval) {
    return;
  }

  PostTaskForTime([this]() { SendAnnouncement(); }, fxl::TimePoint::Now() + announcement_interval_);

  announcement_interval_ = announcement_interval_ * 2;
}

void InstanceResponder::SendAnyServiceResponse(const ReplyAddress& reply_address) {
  auto ptr_resource = std::make_shared<DnsResource>(MdnsNames::kAnyServiceFullName, DnsType::kPtr);
  ptr_resource->ptr_.pointer_domain_name_ = MdnsNames::LocalServiceFullName(service_name_);
  SendResource(ptr_resource, MdnsResourceSection::kAnswer, reply_address);
}

void InstanceResponder::GetAndSendPublication(bool query, const std::string& subtype,
                                              const ReplyAddress& reply_address) const {
  if (publisher_ == nullptr) {
    return;
  }

  publisher_->GetPublication(query, subtype,
                             [this, subtype, reply_address = reply_address](
                                 std::unique_ptr<Mdns::Publication> publication) {
                               if (publication) {
                                 SendPublication(*publication, subtype, reply_address);
                               }
                             });
}

void InstanceResponder::SendPublication(const Mdns::Publication& publication,
                                        const std::string& subtype,
                                        const ReplyAddress& reply_address) const {
  if (!subtype.empty()) {
    SendSubtypePtrRecord(subtype, publication.ptr_ttl_seconds_, reply_address);
  }

  auto ptr_resource =
      std::make_shared<DnsResource>(MdnsNames::LocalServiceFullName(service_name_), DnsType::kPtr);
  ptr_resource->time_to_live_ = publication.ptr_ttl_seconds_;
  ptr_resource->ptr_.pointer_domain_name_ = instance_full_name_;
  SendResource(ptr_resource, MdnsResourceSection::kAnswer, reply_address);

  auto srv_resource = std::make_shared<DnsResource>(instance_full_name_, DnsType::kSrv);
  srv_resource->time_to_live_ = publication.srv_ttl_seconds_;
  srv_resource->srv_.priority_ = publication.srv_priority_;
  srv_resource->srv_.weight_ = publication.srv_weight_;
  srv_resource->srv_.port_ = publication.port_;
  srv_resource->srv_.target_ = host_full_name_;
  SendResource(srv_resource, MdnsResourceSection::kAdditional, reply_address);

  auto txt_resource = std::make_shared<DnsResource>(instance_full_name_, DnsType::kTxt);
  txt_resource->time_to_live_ = publication.txt_ttl_seconds_;
  txt_resource->txt_.strings_ = publication.text_;
  SendResource(txt_resource, MdnsResourceSection::kAdditional, reply_address);

  SendAddresses(MdnsResourceSection::kAdditional, reply_address);
}

void InstanceResponder::SendSubtypePtrRecord(const std::string& subtype, uint32_t ttl,
                                             const ReplyAddress& reply_address) const {
  FXL_DCHECK(!subtype.empty());

  auto ptr_resource = std::make_shared<DnsResource>(
      MdnsNames::LocalServiceSubtypeFullName(service_name_, subtype), DnsType::kPtr);
  ptr_resource->time_to_live_ = ttl;
  ptr_resource->ptr_.pointer_domain_name_ = instance_full_name_;
  SendResource(ptr_resource, MdnsResourceSection::kAnswer, reply_address);
}

void InstanceResponder::SendGoodbye() const {
  Mdns::Publication publication;
  publication.ptr_ttl_seconds_ = 0;
  publication.srv_ttl_seconds_ = 0;
  publication.txt_ttl_seconds_ = 0;

  SendPublication(publication, "", addresses().multicast_reply());
}

}  // namespace mdns

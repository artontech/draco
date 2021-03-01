// Copyright 2016 The Draco Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
#include "draco/compression/point_cloud/point_cloud_decoder.h"

#include "draco/io/file_utils.h"
#include "draco/metadata/metadata_decoder.h"

namespace draco {

PointCloudDecoder::PointCloudDecoder()
    : point_cloud_(nullptr),
      buffer_(nullptr),
      version_major_(0),
      version_minor_(0),
      options_(nullptr) {}

Status PointCloudDecoder::DecodeHeader(DecoderBuffer *buffer,
                                       DracoHeader *out_header) {
  constexpr char kIoErrorMsg[] = "Failed to parse Draco header.";
  if (!buffer->Decode(out_header->draco_string, 5)) {
    return Status(Status::IO_ERROR, kIoErrorMsg);
  }
  if (memcmp(out_header->draco_string, "DRACO", 5) != 0) {
    return Status(Status::DRACO_ERROR, "Not a Draco file.");
  }
  if (!buffer->Decode(&(out_header->version_major))) {
    return Status(Status::IO_ERROR, kIoErrorMsg);
  }
  if (!buffer->Decode(&(out_header->version_minor))) {
    return Status(Status::IO_ERROR, kIoErrorMsg);
  }
  if (!buffer->Decode(&(out_header->encoder_type))) {
    return Status(Status::IO_ERROR, kIoErrorMsg);
  }
  if (!buffer->Decode(&(out_header->encoder_method))) {
    return Status(Status::IO_ERROR, kIoErrorMsg);
  }
  if (!buffer->Decode(&(out_header->flags))) {
    return Status(Status::IO_ERROR, kIoErrorMsg);
  }
  return OkStatus();
}

Status PointCloudDecoder::DecodeMetadata() {
  std::unique_ptr<GeometryMetadata> metadata =
      std::unique_ptr<GeometryMetadata>(new GeometryMetadata());
  MetadataDecoder metadata_decoder;
  if (!metadata_decoder.DecodeGeometryMetadata(buffer_, metadata.get())) {
    return Status(Status::DRACO_ERROR, "Failed to decode metadata.");
  }
  point_cloud_->AddMetadata(std::move(metadata));
  return OkStatus();
}

Status PointCloudDecoder::Decode(const DecoderOptions &options,
                                 DecoderBuffer *in_buffer,
                                 PointCloud *out_point_cloud) {
  options_ = &options;
  buffer_ = in_buffer;
  point_cloud_ = out_point_cloud;
  DracoHeader header;
  DRACO_RETURN_IF_ERROR(DecodeHeader(buffer_, &header))
  // Sanity check that we are really using the right decoder (mostly for cases
  // where the Decode method was called manually outside of our main API.
  if (header.encoder_type != GetGeometryType()) {
    return Status(Status::DRACO_ERROR,
                  "Using incompatible decoder for the input geometry.");
  }
  // TODO(ostava): We should check the method as well, but currently decoders
  // don't expose the decoding method id.
  version_major_ = header.version_major;
  version_minor_ = header.version_minor;

  const uint8_t max_supported_major_version =
      header.encoder_type == POINT_CLOUD ? kDracoPointCloudBitstreamVersionMajor
                                         : kDracoMeshBitstreamVersionMajor;
  const uint8_t max_supported_minor_version =
      header.encoder_type == POINT_CLOUD ? kDracoPointCloudBitstreamVersionMinor
                                         : kDracoMeshBitstreamVersionMinor;

  // Check for version compatibility.
#ifdef DRACO_BACKWARDS_COMPATIBILITY_SUPPORTED
  if (version_major_ < 1 || version_major_ > max_supported_major_version) {
    return Status(Status::UNKNOWN_VERSION, "Unknown major version.");
  }
  if (version_major_ == max_supported_major_version &&
      version_minor_ > max_supported_minor_version) {
    return Status(Status::UNKNOWN_VERSION, "Unknown minor version.");
  }
#else
  if (version_major_ != max_supported_major_version) {
    return Status(Status::UNKNOWN_VERSION, "Unsupported major version.");
  }
  if (version_minor_ != max_supported_minor_version) {
    return Status(Status::UNKNOWN_VERSION, "Unsupported minor version.");
  }
#endif
  buffer_->set_bitstream_version(
      DRACO_BITSTREAM_VERSION(version_major_, version_minor_));

  if (bitstream_version() >= DRACO_BITSTREAM_VERSION(1, 3) &&
      (header.flags & METADATA_FLAG_MASK)) {
    DRACO_RETURN_IF_ERROR(DecodeMetadata())
  }
  if (!InitializeDecoder()) {
    return Status(Status::DRACO_ERROR, "Failed to initialize the decoder.");
  }
  if (!DecodeGeometryData()) {
    return Status(Status::DRACO_ERROR, "Failed to decode geometry data.");
  }
  Status status = DecodePointAttributes();
  if (!status.ok()) {
    return status;
  }
  return OkStatus();
}

Status PointCloudDecoder::DecodeAttr(const DecoderOptions &options,
                                 DecoderBuffer *in_buffer,
                                 DracoHeader *header,
                                 PointCloud *out_point_cloud) {
  options_ = &options;
  buffer_ = in_buffer;
  point_cloud_ = out_point_cloud;

  std::string name = options_->GetGlobalString("attribute_name", "");
  if (name == "base") {
    // Skip header size
    // buffer_->Advance(11);
    DRACO_RETURN_IF_ERROR(DecodeHeader(buffer_, header))

    // Sanity check that we are really using the right decoder (mostly for cases
    // where the Decode method was called manually outside of our main API.
    if (header->encoder_type != GetGeometryType()) {
      return Status(Status::DRACO_ERROR,
                    "Using incompatible decoder for the input geometry.");
    }
    
    // TODO(ostava): We should check the method as well, but currently decoders
    // don't expose the decoding method id.
    version_major_ = header->version_major;
    version_minor_ = header->version_minor;

    const uint8_t max_supported_major_version =
        header->encoder_type == POINT_CLOUD ? kDracoPointCloudBitstreamVersionMajor
                                          : kDracoMeshBitstreamVersionMajor;
    const uint8_t max_supported_minor_version =
        header->encoder_type == POINT_CLOUD ? kDracoPointCloudBitstreamVersionMinor
                                          : kDracoMeshBitstreamVersionMinor;

    // Check for version compatibility.
#ifdef DRACO_BACKWARDS_COMPATIBILITY_SUPPORTED
    if (version_major_ < 1 || version_major_ > max_supported_major_version) {
      return Status(Status::UNKNOWN_VERSION, "Unknown major version.");
    }
    if (version_major_ == max_supported_major_version &&
        version_minor_ > max_supported_minor_version) {
      return Status(Status::UNKNOWN_VERSION, "Unknown minor version.");
    }
#else
    if (version_major_ != max_supported_major_version) {
      return Status(Status::UNKNOWN_VERSION, "Unsupported major version.");
    }
    if (version_minor_ != max_supported_minor_version) {
      return Status(Status::UNKNOWN_VERSION, "Unsupported minor version.");
    }
#endif
    buffer_->set_bitstream_version(
        DRACO_BITSTREAM_VERSION(version_major_, version_minor_));

    if (bitstream_version() >= DRACO_BITSTREAM_VERSION(1, 3) &&
        (header->flags & METADATA_FLAG_MASK)) {
      DRACO_RETURN_IF_ERROR(DecodeMetadata())
    }
    if (!InitializeDecoder()) {
      return Status(Status::DRACO_ERROR, "Failed to initialize the decoder.");
    }

    if (!DecodeGeometryData()) {
      return Status(Status::DRACO_ERROR, "Failed to decode geometry data.");
    }
  }
  else {
    buffer_->set_bitstream_version(DRACO_BITSTREAM_VERSION(version_major_, version_minor_));
  }

  Status status = DecodePointAttributesAttr();
  if (!status.ok()) {
    return status;
  }

  return OkStatus();
}

Status PointCloudDecoder::DecodePointAttributes() {
  uint8_t num_attributes_decoders;
  if (!buffer_->Decode(&num_attributes_decoders)) {
    return Status(Status::DRACO_ERROR, "Failed to decode num_attributes_decoders.");
  }
  // Create all attribute decoders. This is implementation specific and the
  // derived classes can use any data encoded in the
  // PointCloudEncoder::EncodeAttributesEncoderIdentifier() call.
  for (int i = 0; i < num_attributes_decoders; ++i) {
    Status status = CreateAttributesDecoder(i);
    if (!status.ok()) {
      return status;
    }
  }

  // Initialize all attributes decoders. No data is decoded here.
  for (auto &att_dec : attributes_decoders_) {
    if (!att_dec->Init(this, point_cloud_)) {
      return Status(Status::DRACO_ERROR, "Failed to initialize all attributes decoders.");
    }
  }

  // Decode any data needed by the attribute decoders.
  for (int i = 0; i < num_attributes_decoders; ++i) {
    if (!attributes_decoders_[i]->DecodeAttributesDecoderData(buffer_)) {
      return Status(Status::DRACO_ERROR, "Failed to decode any data needed by the attribute decoders.");
    }
  }

  // Create map between attribute and decoder ids.
  for (int i = 0; i < num_attributes_decoders; ++i) {
    const int32_t num_attributes = attributes_decoders_[i]->GetNumAttributes();
    for (int j = 0; j < num_attributes; ++j) {
      int att_id = attributes_decoders_[i]->GetAttributeId(j);
      if (att_id >= attribute_to_decoder_map_.size()) {
        attribute_to_decoder_map_.resize(att_id + 1);
      }
      attribute_to_decoder_map_[att_id] = i;
    }
  }

  // Decode the actual attributes using the created attribute decoders.
  Status status = DecodeAllAttributes();
  if (!status.ok()) {
    return status;
  }

  if (!OnAttributesDecoded()) {
    return Status(Status::DRACO_ERROR, "Failed OnAttributesDecoded.");
  }
  return Status(Status::OK, "Decode point attributes.");
}

Status PointCloudDecoder::DecodePointAttributesAttr() {
  std::string name = options_->GetGlobalString("attribute_name", "");
  if (name == "base") {
    if (!buffer_->Decode(&num_attributes_decoders_)) {
      return Status(Status::DRACO_ERROR, "Failed to decode num_attributes_decoders.");
    }
  
    // Create all attribute decoders. This is implementation specific and the
    // derived classes can use any data encoded in the
    // PointCloudEncoder::EncodeAttributesEncoderIdentifier() call.
    for (int i = 0; i < num_attributes_decoders_; ++i) {
      Status status = CreateAttributesDecoder(i);
      if (!status.ok()) {
        return status;
      }
    }

    // Initialize all attributes decoders. No data is decoded here.
    for (auto &att_dec : attributes_decoders_) {
      if (!att_dec->Init(this, point_cloud_)) {
        return Status(Status::DRACO_ERROR, "Failed to initialize all attributes decoders.");
      }
    }

    // Decode any data needed by the attribute decoders.
    for (int i = 0; i < num_attributes_decoders_; ++i) {
      if (!attributes_decoders_[i]->DecodeAttributesDecoderData(buffer_)) {
        return Status(Status::DRACO_ERROR, "Failed to decode any data needed by the attribute decoders.");
      }
    }

    // Create map between attribute and decoder ids.
    for (int i = 0; i < num_attributes_decoders_; ++i) {
      const int32_t num_attributes = attributes_decoders_[i]->GetNumAttributes();
      for (int j = 0; j < num_attributes; ++j) {
        int att_id = attributes_decoders_[i]->GetAttributeId(j);
        if (att_id >= attribute_to_decoder_map_.size()) {
          attribute_to_decoder_map_.resize(att_id + 1);
        }
        attribute_to_decoder_map_[att_id] = i;
      }
    }
  } else {
    // Decode the actual attributes using the created attribute decoders.
    Status status = DecodeAllAttributes();
    if (!status.ok()) {
      return status;
    }
  }

  if (!OnAttributesDecoded()) {
    return Status(Status::DRACO_ERROR, "Failed OnAttributesDecoded.");
  }
  return Status(Status::OK, "Decode point attributes.");
}

Status PointCloudDecoder::DecodeAllAttributes() {
  bool split_attr = options_->GetGlobalBool("split_attr", false);
  std::string attribute_name = options_->GetGlobalString("attribute_name", "");
  uint16_t bitstream_version = buffer_->bitstream_version();

  for (auto &att_dec : attributes_decoders_) {
    // TODO: Support multi attr loading
    int32_t attr_id = att_dec->GetAttributeId(0);
    if (split_attr) {
      std::string name = point_cloud_->GetMetadataEntryStringByAttributeId(
        attr_id, "name");
      if (attribute_name != name) continue;

      point_cloud_->metadata()->attribute_metadata(attr_id)->AddEntryInt("output", 1);
      //attr_buffer_->set_bitstream_version(bitstream_version);
      Status status = att_dec->DecodeAttributes(buffer_);
      if (!status.ok()) {
        return status;
      }
      break;
    } else {
      Status status = att_dec->DecodeAttributes(buffer_);
      if (!status.ok()) {
        return status;
      }
    }
  }
  return Status(Status::OK, "Decode the actual attributes.");
}

const PointAttribute *PointCloudDecoder::GetPortableAttribute(
    int32_t parent_att_id) {
  if (parent_att_id < 0 || parent_att_id >= point_cloud_->num_attributes()) {
    return nullptr;
  }
  const int32_t parent_att_decoder_id =
      attribute_to_decoder_map_[parent_att_id];
  return attributes_decoders_[parent_att_decoder_id]->GetPortableAttribute(
      parent_att_id);
}

}  // namespace draco

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
#include "draco/io/ply_encoder.h"

#include <memory>
#include <sstream>

#include "draco/io/file_writer_factory.h"
#include "draco/io/file_writer_interface.h"

namespace draco {

PlyEncoder::PlyEncoder()
    : out_buffer_(nullptr),
      in_point_cloud_(nullptr),
      in_mesh_(nullptr),
      op_(nullptr) {}

bool PlyEncoder::EncodeToFile(const PointCloud &pc,
                              const std::string &file_name,
                              const draco::DecoderOptions *op) {
  op_ = op;
  std::unique_ptr<FileWriterInterface> file =
      FileWriterFactory::OpenWriter(file_name);
  if (!file) {
    return false;  // File couldn't be opened.
  }
  // Encode the mesh into a buffer.
  EncoderBuffer buffer;
  if (!EncodeToBuffer(pc, &buffer)) {
    return false;
  }
  // Write the buffer into the file.
  file->Write(buffer.data(), buffer.size());
  return true;
}

bool PlyEncoder::EncodeToFile(const Mesh &mesh, const std::string &file_name,
                              const DecoderOptions *op) {
  in_mesh_ = &mesh;
  return EncodeToFile(static_cast<const PointCloud &>(mesh), file_name, op);
}

bool PlyEncoder::EncodeToBuffer(const PointCloud &pc,
                                EncoderBuffer *out_buffer) {
  in_point_cloud_ = &pc;
  out_buffer_ = out_buffer;
  if (!EncodeInternal()) {
    return ExitAndCleanup(false);
  }
  return ExitAndCleanup(true);
}

bool PlyEncoder::EncodeToBuffer(const Mesh &mesh, EncoderBuffer *out_buffer) {
  in_mesh_ = &mesh;
  return EncodeToBuffer(static_cast<const PointCloud &>(mesh), out_buffer);
}
bool PlyEncoder::EncodeInternal() {
  bool to_generic = false;
  if (op_) {
    to_generic = op_->GetGlobalBool("to_generic", false);
  }
  
  // Write PLY header.
  // TODO(ostava): Currently works only for xyz positions, rgb(a) colors, and named generic.
  std::stringstream out;
  out << "ply" << std::endl;
  out << "format binary_little_endian 1.0" << std::endl;
  out << "element vertex " << in_point_cloud_->num_points() << std::endl;

  // Classify attributes
  PointAttribute const *pos_att = NULL, *normal_att = NULL, *tex_att = NULL, *color_att = NULL;
  std::vector<int32_t> generic_att_ids;
  for (int i = 0; i< in_point_cloud_->num_attributes(); i++) {
    const PointAttribute *att = in_point_cloud_->attribute(i);
    switch (att->attribute_type()) {
      case GeometryAttribute::POSITION:
        pos_att = att;
      break;
      case GeometryAttribute::NORMAL:
        normal_att = att;
      break;
      case GeometryAttribute::TEX_COORD:
        tex_att = att;
      break;
      case GeometryAttribute::COLOR:
        color_att = att;
      break;
      case GeometryAttribute::GENERIC:
        if (to_generic ||
            (in_point_cloud_->GetMetadataEntryIntByAttributeId(i, "output") &&
             !in_point_cloud_->GetMetadataEntryStringByAttributeId(i, "name")
                  .empty())) {
          generic_att_ids.push_back(i);
        }
      break;
      default:
      break;
    }
  }

  if (NULL == pos_att) {
    return false;
  }

  // Ensure normals are 3 component. Don't encode them otherwise.
  if (normal_att && normal_att->num_components() != 3) {
    normal_att = NULL;
  }

  // Ensure texture coordinates have only 2 components. Don't encode them
  // otherwise. TODO(ostava): Add support for 3 component normals (uvw).
  if (tex_att && tex_att->num_components() != 2) {
    tex_att = NULL;
  }

  out << "property " << GetAttributeDataType(pos_att) << " x" << std::endl;
  out << "property " << GetAttributeDataType(pos_att) << " y" << std::endl;
  out << "property " << GetAttributeDataType(pos_att) << " z" << std::endl;
  if (normal_att) {
    out << "property " << GetAttributeDataType(normal_att) << " nx"
        << std::endl;
    out << "property " << GetAttributeDataType(normal_att) << " ny"
        << std::endl;
    out << "property " << GetAttributeDataType(normal_att) << " nz"
        << std::endl;
  }
  if (color_att) {
    if (color_att->num_components() > 0) {
      out << "property " << GetAttributeDataType(color_att) << " red"
          << std::endl;
    }
    if (color_att->num_components() > 1) {
      out << "property " << GetAttributeDataType(color_att) << " green"
          << std::endl;
    }
    if (color_att->num_components() > 2) {
      out << "property " << GetAttributeDataType(color_att) << " blue"
          << std::endl;
    }
    if (color_att->num_components() > 3) {
      out << "property " << GetAttributeDataType(color_att) << " alpha"
          << std::endl;
    }
  }

  // Deal with generic info
  for (int i = 0; i < generic_att_ids.size(); i++) {
    int32_t attr_id = generic_att_ids.at(i);
    PointAttribute const *generic_att = in_point_cloud_->attribute(attr_id);
    out << "property " << GetAttributeDataType(generic_att) << " "
        << (to_generic ? "generic" : in_point_cloud_->GetMetadataEntryStringByAttributeId(
               attr_id, "name"))
        << std::endl;
    if (to_generic) break;
  }

  if (in_mesh_) {
    out << "element face " << in_mesh_->num_faces() << std::endl;
    out << "property list uchar int vertex_indices" << std::endl;
    if (tex_att) {
      // Texture coordinates are usually encoded in the property list (one value
      // per corner).
      out << "property list uchar " << GetAttributeDataType(tex_att) << " texcoord" << std::endl;
    }
  }
  out << "end_header" << std::endl;

  // Not very efficient but the header should be small so just copy the stream
  // to a string.
  const std::string header_str = out.str();
  buffer()->Encode(header_str.data(), header_str.length());

  // Store point attributes.
  for (PointIndex v(0); v < in_point_cloud_->num_points(); ++v) {
    buffer()->Encode(pos_att->GetAddress(pos_att->mapped_index(v)),
                     pos_att->byte_stride());
    if (normal_att) {
      buffer()->Encode(normal_att->GetAddress(normal_att->mapped_index(v)),
                       normal_att->byte_stride());
    }
    if (color_att) {
      buffer()->Encode(color_att->GetAddress(color_att->mapped_index(v)),
                       color_att->byte_stride());
    }
    for (int i = 0; i < generic_att_ids.size(); i++) {
      int32_t attr_id = generic_att_ids.at(i);
      PointAttribute const *generic_att = in_point_cloud_->attribute(attr_id);
      buffer()->Encode(generic_att->GetAddress(generic_att->mapped_index(v)),
                       generic_att->byte_stride());
      if (to_generic) break;
    }
  }

  if (in_mesh_) {
    // Write face data.
    for (FaceIndex i(0); i < in_mesh_->num_faces(); ++i) {
      // Write the number of face indices (always 3).
      buffer()->Encode(static_cast<uint8_t>(3));

      const auto &f = in_mesh_->face(i);
      buffer()->Encode(f[0]);
      buffer()->Encode(f[1]);
      buffer()->Encode(f[2]);

      if (tex_att) {
        // Two coordinates for every corner -> 6.
        buffer()->Encode(static_cast<uint8_t>(6));

        for (int c = 0; c < 3; ++c) {
          buffer()->Encode(tex_att->GetAddress(tex_att->mapped_index(f[c])),
                           tex_att->byte_stride());
        }
      }
    }
  }
  return true;
}

bool PlyEncoder::ExitAndCleanup(bool return_value) {
  in_mesh_ = nullptr;
  in_point_cloud_ = nullptr;
  out_buffer_ = nullptr;
  return return_value;
}

const char *PlyEncoder::GetAttributeDataType(const PointAttribute * att) {
  // TODO(ostava): Add support for more types.
  switch (att->data_type()) {
    case DT_FLOAT32:
      return "float";
    case DT_UINT8:
      return "uchar";
    case DT_INT32:
      return "int";
    default:
      break;
  }
  return nullptr;
}

const char *PlyEncoder::GetAttributeDataType(int attribute) {
  return GetAttributeDataType(in_point_cloud_->attribute(attribute));
}

}  // namespace draco

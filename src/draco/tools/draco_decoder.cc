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
#include <cinttypes>

#include "draco/compression/decode.h"
#include "draco/core/cycle_timer.h"
#include "draco/io/file_utils.h"
#include "draco/io/obj_encoder.h"
#include "draco/io/parser_utils.h"
#include "draco/io/ply_encoder.h"

namespace {

struct Options {
  Options();

  std::string input;
  std::string output;

  bool split_attr = false;
  std::string attribute_name;
  bool format_output = false;
  bool to_generic = false;
};

Options::Options() {}

void Usage() {
  printf("Usage: draco_decoder [options] -i input\n");
  printf("\n");
  printf("Main options:\n");
  printf("  -h | -?               show help.\n");
  printf("  -o <output>           output file name.\n");
  printf("  --split_attr          load attr data from seprate file.\n");
  printf("  --format_output       format output.\n");
  printf("  --to_generic          decode attr to generic.\n");
}

int ReturnError(const draco::Status &status) {
  printf("Failed to decode the input file %s\n", status.error_msg());
  return -1;
}

}  // namespace

bool LoadDecoderBuffer(const std::string &filename, std::vector<char> &data,
                       draco::DecoderBuffer &attr_buffer) {
  if (!draco::ReadFileToBuffer(filename, &data)) {
    return false;
  }
  if (data.empty()) {
    return false;
  }
  attr_buffer.Init(data.data(), data.size());
  return true;
}

int main(int argc, char **argv) {
  Options options;
  const int argc_check = argc - 1;

  for (int i = 1; i < argc; ++i) {
    if (!strcmp("-h", argv[i]) || !strcmp("-?", argv[i])) {
      Usage();
      return 0;
    } else if (!strcmp("-i", argv[i]) && i < argc_check) {
      options.input = argv[++i];
    } else if (!strcmp("-o", argv[i]) && i < argc_check) {
      options.output = argv[++i];
    } else if (!strcmp("--split_attr", argv[i]) && i < argc_check) {
      options.split_attr = true;
      options.attribute_name = argv[++i];
    } else if (!strcmp("--format_output", argv[i])) {
      options.format_output = true;
    } else if (!strcmp("--to_generic", argv[i])) {
      options.to_generic = true;
    }
  }
  if (argc < 3 || options.input.empty()) {
    Usage();
    return -1;
  }

  std::vector<char> data;
  if (!draco::ReadFileToBuffer(options.input, &data)) {
    printf("Failed opening the input file.\n");
    return -1;
  }

  if (data.empty()) {
    printf("Empty input file.\n");
    return -1;
  }

  if (options.output.empty()) {
    // Save the output model into a ply file.
    options.output = options.input + ".ply";
  }

  // TODO(fgalligan): Change extension code to look for '.'.
  const std::string extension = draco::parser::ToLower(
      options.output.size() >= 4
          ? options.output.substr(options.output.size() - 4)
          : options.output);

  // Create a draco decoding buffer. Note that no data is copied in this step.
  draco::DecoderBuffer buffer;
  buffer.Init(data.data(), data.size());

  draco::CycleTimer timer;
  // Decode the input data into a geometry.
  std::unique_ptr<draco::PointCloud> pc;
  draco::Mesh *mesh = nullptr;
  auto type_statusor = draco::Decoder::GetEncodedGeometryType(&buffer);
  if (!type_statusor.ok()) {
    return ReturnError(type_statusor.status());
  }
  const draco::EncodedGeometryType geom_type = type_statusor.value();
  draco::Decoder decoder;

  // Set options
  draco::DecoderOptions *op = decoder.options();
  op->SetGlobalBool("split_attr", options.split_attr);
  op->SetGlobalString("output", options.output);
  op->SetGlobalBool("format_output", options.format_output);
  op->SetGlobalBool("to_generic", options.to_generic);

  if (geom_type == draco::TRIANGULAR_MESH) {
    timer.Start();

    if (options.split_attr) {
      const std::string filename =
          options.input.substr(0, options.input.size() - 4);
      const std::string fileext =
          options.input.substr(options.input.size() - 4);
      // Create a draco decoding buffer. Note that no data is copied in this
      // step.
      std::vector<char> pos_data;
      draco::DecoderBuffer pos_buffer;
      const std::string pos_filename = filename + "_position" + fileext;
      if (!LoadDecoderBuffer(pos_filename, pos_data, pos_buffer)) {
        return ReturnError(draco::Status(draco::Status::DRACO_ERROR,
                                         "Load position buffer failed."));
      }

      std::vector<char> attr_data;
      draco::DecoderBuffer attr_buffer;
      const std::string attr_filename =
          filename + '_' + options.attribute_name + fileext;
      if (!LoadDecoderBuffer(attr_filename, attr_data, attr_buffer)) {
        return ReturnError(draco::Status(draco::Status::DRACO_ERROR,
                                         "Load attr buffer failed."));
      }

      draco::DracoHeader header;
      decoder.GetDracoHeader(&buffer, &header);
      auto statusor = decoder.DecodeMeshFromBufferAttr(&buffer, &header, "base");
      if (!statusor.ok()) {
        return ReturnError(statusor.status());
      }

      std::unique_ptr<draco::Mesh> in_mesh = std::move(statusor).value();
      timer.Stop();
      if (in_mesh) {
        mesh = in_mesh.get();
        pc = std::move(in_mesh);
      }

      auto status = decoder.DecodeBufferAttrToGeometry(&pos_buffer, &header,
                                                       "position", mesh);
      if (!status.ok()) {
        return ReturnError(status);
      }

      status = decoder.DecodeBufferAttrToGeometry(
          &attr_buffer, &header, options.attribute_name.c_str(), mesh);
      if (!status.ok()) {
        return ReturnError(status);
      }
    }
    else {
      auto statusor = decoder.DecodeMeshFromBuffer(&buffer);
      if (!statusor.ok()) {
        return ReturnError(statusor.status());
      }

      std::unique_ptr<draco::Mesh> in_mesh = std::move(statusor).value();
      timer.Stop();
      if (in_mesh) {
        mesh = in_mesh.get();
        pc = std::move(in_mesh);
      }
    }
  } else if (geom_type == draco::POINT_CLOUD) {
    // Failed to decode it as mesh, so let's try to decode it as a point cloud.
    timer.Start();
    auto statusor = decoder.DecodePointCloudFromBuffer(&buffer);
    if (!statusor.ok()) {
      return ReturnError(statusor.status());
    }
    pc = std::move(statusor).value();
    timer.Stop();
  }

  if (pc == nullptr) {
    printf("Failed to decode the input file.\n");
    return -1;
  }

  // Save the decoded geometry into a file.
  int ret = 0;
  if (extension == ".obj") {
    draco::ObjEncoder obj_encoder;
    if (mesh) {
      if (!obj_encoder.EncodeToFile(*mesh, options.output)) {
        printf("Failed to store the decoded mesh as OBJ.\n");
        ret = -1;
      }
    } else {
      if (!obj_encoder.EncodeToFile(*pc.get(), options.output)) {
        printf("Failed to store the decoded point cloud as OBJ.\n");
        ret = -1;
      }
    }
  } else if (extension == ".ply") {
    draco::PlyEncoder ply_encoder;
    if (mesh) {
      if (!ply_encoder.EncodeToFile(*mesh, options.output, op)) {
        printf("Failed to store the decoded mesh as PLY.\n");
        ret = -1;
      }
    } else {
      if (!ply_encoder.EncodeToFile(*pc.get(), options.output, op)) {
        printf("Failed to store the decoded point cloud as PLY.\n");
        ret = -1;
      }
    }
  } else {
    printf("Invalid extension of the output file. Use either .ply or .obj.\n");
    ret = -1;
  }

  if (options.format_output) {
    std::cout << "{\"file\":\"" << options.output << '"';
    if (ret != 0)
      std::cout << ",\"err\":\"decode\"}" << std::endl;
    else
      std::cout << '}' << std::endl;
  } else {
    printf("Decoded geometry saved to %s (%" PRId64 " ms to decode)\n",
           options.output.c_str(), timer.GetInMs());
  }
  return ret;
}

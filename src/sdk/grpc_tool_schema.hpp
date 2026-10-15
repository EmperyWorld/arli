#pragma once
#include <google/protobuf/descriptor.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace arli::grpc_tools_detail {
using json = nlohmann::json;
inline json message_schema(const google::protobuf::Descriptor*, int, size_t&);
inline json field_schema(const google::protobuf::FieldDescriptor* field, int depth, size_t& budget) {
  if (!budget) throw std::invalid_argument("gRPC schema exceeds node limit");
  --budget;
  using F = google::protobuf::FieldDescriptor;
  json value;
  switch(field->cpp_type()) {
    case F::CPPTYPE_MESSAGE: value=message_schema(field->message_type(),depth+1,budget);break;
    case F::CPPTYPE_BOOL: value={{"type","boolean"}};break;
    case F::CPPTYPE_STRING: value={{"type","string"}};break;
    case F::CPPTYPE_ENUM:
      value={{"type","string"},{"enum",json::array()}};
      for(int n=0;n<field->enum_type()->value_count();++n) {
        if (!budget) throw std::invalid_argument("gRPC enum schema exceeds node limit");
        --budget; value["enum"].push_back(field->enum_type()->value(n)->name());
      }
      break;
    case F::CPPTYPE_FLOAT: case F::CPPTYPE_DOUBLE: value={{"type","number"}};break;
    case F::CPPTYPE_INT64: case F::CPPTYPE_UINT64: value={{"type",json::array({"integer","string"})}};break;
    default:value={{"type","integer"}};
  }
  return value;
}
inline json message_schema(const google::protobuf::Descriptor* descriptor, int depth, size_t& budget) {
  if (!budget) throw std::invalid_argument("gRPC schema exceeds node limit");
  --budget;
  if(depth>6 || descriptor->field_count()>128)throw std::invalid_argument("gRPC message schema exceeds supported depth/field limits");
  if(descriptor->full_name()=="google.protobuf.Empty")return {{"type","object"},{"properties",json::object()},{"additionalProperties",false}};
  if(descriptor->full_name().rfind("google.protobuf.",0)==0)
    throw std::invalid_argument("gRPC well-known JSON types require an explicit adapter");
  json schema={{"type","object"},{"properties",json::object()},{"additionalProperties",false}};
  json required=json::array();
  for(int n=0;n<descriptor->field_count();++n) {
    const auto* field=descriptor->field(n);json value;
    if(field->is_map())value={{"type","object"},{"additionalProperties",field_schema(field->message_type()->map_value(),depth,budget)}};
    else {value=field_schema(field,depth,budget);if(field->is_repeated())value={{"type","array"},{"items",value}};}
    schema["properties"][field->json_name()]=value;
    if(field->is_required())required.push_back(field->json_name());
  }
  if(!required.empty())schema["required"]=required;
  return schema;
}
}

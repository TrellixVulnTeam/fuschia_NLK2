// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/tables_generator.h"

#include "fidl/names.h"

namespace fidl {

namespace {

std::string PrimitiveSubtypeToString(fidl::types::PrimitiveSubtype subtype) {
  using fidl::types::PrimitiveSubtype;
  switch (subtype) {
    case PrimitiveSubtype::kBool:
      return "Bool";
    case PrimitiveSubtype::kInt8:
      return "Int8";
    case PrimitiveSubtype::kInt16:
      return "Int16";
    case PrimitiveSubtype::kInt32:
      return "Int32";
    case PrimitiveSubtype::kInt64:
      return "Int64";
    case PrimitiveSubtype::kUint8:
      return "Uint8";
    case PrimitiveSubtype::kUint16:
      return "Uint16";
    case PrimitiveSubtype::kUint32:
      return "Uint32";
    case PrimitiveSubtype::kUint64:
      return "Uint64";
    case PrimitiveSubtype::kFloat32:
      return "Float32";
    case PrimitiveSubtype::kFloat64:
      return "Float64";
  }
}

// When generating coding tables for containers employing envelopes (xunions & tables),
// we need to reference coding tables for primitives, in addition to types that need coding.
// This function handles naming coding tables for both cases.
std::string CodedNameForEnvelope(const fidl::coded::Type* type) {
  switch (type->kind) {
    case coded::Type::Kind::kPrimitive: {
      using fidl::types::PrimitiveSubtype;
      // To save space, all primitive types of the same underlying subtype
      // share the same table.
      std::string suffix =
          PrimitiveSubtypeToString(static_cast<const coded::PrimitiveType*>(type)->subtype);
      return "fidl_internal_k" + suffix;
    }
    default:
      return type->coded_name;
  }
}

constexpr auto kIndent = "    ";

void Emit(std::ostream* file, std::string_view data) { *file << data; }

void EmitBool(std::ostream* file, bool value) { *file << (value ? "true" : "false"); }

void EmitNewlineAndIndent(std::ostream* file, size_t indent_level) {
  *file << "\n";
  while (indent_level--)
    *file << kIndent;
}

void EmitArrayBegin(std::ostream* file) { *file << "{"; }

void EmitArraySeparator(std::ostream* file, size_t indent_level) {
  *file << ",";
  EmitNewlineAndIndent(file, indent_level);
}

void EmitArrayEnd(std::ostream* file) { *file << "}"; }

void Emit(std::ostream* file, uint32_t value) { *file << value << "u"; }

void Emit(std::ostream* file, uint64_t value) { *file << value << "ul"; }

void Emit(std::ostream* file, types::Nullability nullability) {
  switch (nullability) {
    case types::Nullability::kNullable:
      Emit(file, "kFidlNullability_Nullable");
      break;
    case types::Nullability::kNonnullable:
      Emit(file, "kFidlNullability_Nonnullable");
      break;
  }
}

void Emit(std::ostream* file, types::Strictness strictness) {
  switch (strictness) {
    case types::Strictness::kFlexible:
      Emit(file, "kFidlStrictness_Flexible");
      break;
    case types::Strictness::kStrict:
      Emit(file, "kFidlStrictness_Strict");
      break;
  }
}

template <typename T>
std::string AltTableDeclaration(const T& type, bool is_static) {
  return std::string(is_static ? "static " : "extern ") + "const fidl_type_t " +
         NameTable(type.coded_name) + ";\n";
}

}  // namespace

template <class T>
std::string TablesGenerator::AltTableReference(const T& type) const {
  const coded::Type* alt_type = AltType(&type);
  return "&" + NameTable(alt_type->coded_name);
}

template <typename Collection>
void TablesGenerator::GenerateArray(const Collection& collection) {
  EmitArrayBegin(&tables_file_);

  if (!collection.empty())
    EmitNewlineAndIndent(&tables_file_, ++indent_level_);

  for (size_t i = 0; i < collection.size(); ++i) {
    if (i)
      EmitArraySeparator(&tables_file_, indent_level_);
    Generate(collection[i]);
  }

  if (!collection.empty())
    EmitNewlineAndIndent(&tables_file_, --indent_level_);

  EmitArrayEnd(&tables_file_);
}

void TablesGenerator::Generate(const coded::EnumType& enum_type) {
  std::string validator_func = std::string("EnumValidatorFor_") + std::string(enum_type.coded_name);
  Emit(&tables_file_, "static bool ");
  Emit(&tables_file_, validator_func);
  Emit(&tables_file_, "(uint64_t v) { return ");
  for (const auto& member : enum_type.members) {
    Emit(&tables_file_, "(v == ");
    Emit(&tables_file_, member);
    Emit(&tables_file_, ") || ");
  }
  Emit(&tables_file_, "false; }\n");

  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(enum_type.coded_name));
  Emit(&tables_file_,
       " = {.type_tag=kFidlTypeEnum, {.coded_enum={.underlying_type=kFidlCodedPrimitive_");
  Emit(&tables_file_, PrimitiveSubtypeToString(enum_type.subtype));
  Emit(&tables_file_, ", .validate=&" + validator_func + ", .name=\"");
  Emit(&tables_file_, enum_type.qname);
  Emit(&tables_file_, "\"}}};\n\n");
}

void TablesGenerator::Generate(const coded::BitsType& bits_type) {
  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(bits_type.coded_name));
  Emit(&tables_file_,
       " = {.type_tag=kFidlTypeBits, {.coded_bits={.underlying_type=kFidlCodedPrimitive_");
  Emit(&tables_file_, PrimitiveSubtypeToString(bits_type.subtype));
  Emit(&tables_file_, ", .mask=");
  Emit(&tables_file_, bits_type.mask);
  Emit(&tables_file_, ", .name=\"");
  Emit(&tables_file_, bits_type.qname);
  Emit(&tables_file_, "\"}}};\n\n");
}

void TablesGenerator::Generate(const coded::StructType& struct_type) {
  Emit(&tables_file_, "static const struct FidlStructField ");
  Emit(&tables_file_, NameFields(struct_type.coded_name));
  Emit(&tables_file_, "[] = ");
  GenerateArray(struct_type.fields);
  Emit(&tables_file_, ";\n");

  Emit(&forward_decls_, AltTableDeclaration(struct_type, /*is_static=*/false));

  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(struct_type.coded_name));
  Emit(&tables_file_, " = {.type_tag=kFidlTypeStruct, {.coded_struct={.fields=");
  Emit(&tables_file_, NameFields(struct_type.coded_name));
  Emit(&tables_file_, ", .field_count=");
  Emit(&tables_file_, static_cast<uint32_t>(struct_type.fields.size()));
  Emit(&tables_file_, ", .size=");
  Emit(&tables_file_, struct_type.size);
  Emit(&tables_file_, ", .max_out_of_line=");
  Emit(&tables_file_, struct_type.max_out_of_line);
  Emit(&tables_file_, ", .contains_union=");
  EmitBool(&tables_file_, struct_type.contains_union);
  Emit(&tables_file_, ", .name=\"");
  Emit(&tables_file_, struct_type.qname);
  Emit(&tables_file_, "\", .alt_type=");
  Emit(&tables_file_, AltTableReference(struct_type));
  Emit(&tables_file_, "}}};\n\n");
}

void TablesGenerator::Generate(const coded::TableType& table_type) {
  Emit(&tables_file_, "static const struct FidlTableField ");
  Emit(&tables_file_, NameFields(table_type.coded_name));
  Emit(&tables_file_, "[] = ");
  GenerateArray(table_type.fields);
  Emit(&tables_file_, ";\n");

  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(table_type.coded_name));
  Emit(&tables_file_, " = {.type_tag=kFidlTypeTable, {.coded_table={.fields=");
  Emit(&tables_file_, NameFields(table_type.coded_name));
  Emit(&tables_file_, ", .field_count=");
  Emit(&tables_file_, static_cast<uint32_t>(table_type.fields.size()));
  Emit(&tables_file_, ", .name=\"");
  Emit(&tables_file_, table_type.qname);
  Emit(&tables_file_, "\"}}};\n\n");
}

void TablesGenerator::Generate(const coded::UnionType& union_type) {
  Emit(&tables_file_, "static const struct FidlUnionField ");
  Emit(&tables_file_, NameFields(union_type.coded_name));
  Emit(&tables_file_, "[] = ");
  GenerateArray(union_type.members);
  Emit(&tables_file_, ";\n");

  Emit(&forward_decls_, AltTableDeclaration(union_type, /*is_static=*/false));

  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(union_type.coded_name));
  Emit(&tables_file_, " = {.type_tag=kFidlTypeUnion, {.coded_union={.fields=");
  Emit(&tables_file_, NameFields(union_type.coded_name));
  Emit(&tables_file_, ", .field_count=");
  Emit(&tables_file_, static_cast<uint32_t>(union_type.members.size()));
  Emit(&tables_file_, ", .data_offset=");
  Emit(&tables_file_, union_type.data_offset);
  Emit(&tables_file_, ", .size=");
  Emit(&tables_file_, union_type.size);
  Emit(&tables_file_, ", .name=\"");
  Emit(&tables_file_, union_type.qname);
  Emit(&tables_file_, "\", .alt_type=");
  Emit(&tables_file_, AltTableReference(union_type));
  Emit(&tables_file_, "}}};\n\n");
}

void TablesGenerator::Generate(const coded::XUnionType& xunion_type) {
  Emit(&forward_decls_,
       AltTableDeclaration(xunion_type, /*is_static=*/xunion_type.FromUnionPointer()));

  Emit(&tables_file_, "static const struct FidlXUnionField ");
  Emit(&tables_file_, NameFields(xunion_type.coded_name));
  Emit(&tables_file_, "[] = ");
  GenerateArray(xunion_type.fields);
  Emit(&tables_file_, ";\n");

  if (xunion_type.FromUnionPointer())
    Emit(&tables_file_, "static ");

  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(xunion_type.coded_name));
  Emit(&tables_file_, " = {.type_tag=kFidlTypeXUnion, {.coded_xunion={.field_count=");
  Emit(&tables_file_, static_cast<uint32_t>(xunion_type.fields.size()));
  Emit(&tables_file_, ", .fields=");
  Emit(&tables_file_, NameFields(xunion_type.coded_name));
  Emit(&tables_file_, ", .nullable=");
  Emit(&tables_file_, xunion_type.nullability);
  Emit(&tables_file_, ", .name=\"");
  Emit(&tables_file_, xunion_type.qname);
  Emit(&tables_file_, "\", .strictness=");
  Emit(&tables_file_, xunion_type.strictness);
  Emit(&tables_file_, ", .alt_type=");
  Emit(&tables_file_, AltTableReference(xunion_type));
  Emit(&tables_file_, "}}};\n");
}

void TablesGenerator::Generate(const coded::StructPointerType& pointer) {
  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(pointer.coded_name));
  Emit(&tables_file_,
       " = {.type_tag=kFidlTypeStructPointer, {.coded_struct_pointer={.struct_type=");
  Generate(pointer.element_type);
  Emit(&tables_file_, ".coded_struct}}};\n");
}

void TablesGenerator::Generate(const coded::UnionPointerType& pointer) {
  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(pointer.coded_name));
  Emit(&tables_file_, " = {.type_tag=kFidlTypeUnionPointer, {.coded_union_pointer={.union_type=");
  Generate(pointer.element_type);
  Emit(&tables_file_, ".coded_union}}};\n");
}

void TablesGenerator::Generate(const coded::MessageType& message_type) {
  Emit(&tables_file_, "extern const fidl_type_t ");
  Emit(&tables_file_, NameTable(message_type.coded_name));
  Emit(&tables_file_, ";\n");

  Emit(&tables_file_, "static const struct FidlStructField ");
  Emit(&tables_file_, NameFields(message_type.coded_name));
  Emit(&tables_file_, "[] = ");
  GenerateArray(message_type.fields);
  Emit(&tables_file_, ";\n");

  Emit(&forward_decls_, AltTableDeclaration(message_type, /*is_static=*/false));

  Emit(&tables_file_, "const fidl_type_t ");
  Emit(&tables_file_, NameTable(message_type.coded_name));
  Emit(&tables_file_, " = {.type_tag=kFidlTypeStruct, {.coded_struct={.fields=");
  Emit(&tables_file_, NameFields(message_type.coded_name));
  Emit(&tables_file_, ", .field_count=");
  Emit(&tables_file_, static_cast<uint32_t>(message_type.fields.size()));
  Emit(&tables_file_, ", .size=");
  Emit(&tables_file_, message_type.size);
  Emit(&tables_file_, ", .max_out_of_line=");
  Emit(&tables_file_, message_type.max_out_of_line);
  Emit(&tables_file_, ", .contains_union=");
  EmitBool(&tables_file_, message_type.contains_union);
  Emit(&tables_file_, ", .name=\"");
  Emit(&tables_file_, message_type.qname);
  Emit(&tables_file_, "\", .alt_type=");
  Emit(&tables_file_, AltTableReference(message_type));
  Emit(&tables_file_, "}}};\n\n");
}

void TablesGenerator::Generate(const coded::HandleType& handle_type) {
  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(handle_type.coded_name));
  Emit(&tables_file_, " = {.type_tag=kFidlTypeHandle, {.coded_handle={.handle_subtype=");
  Emit(&tables_file_, NameHandleZXObjType(handle_type.subtype));
  Emit(&tables_file_, ", .nullable=");
  Emit(&tables_file_, handle_type.nullability);
  Emit(&tables_file_, "}}};\n\n");
}

void TablesGenerator::Generate(const coded::RequestHandleType& request_type) {
  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(request_type.coded_name));
  Emit(&tables_file_,
       " = {.type_tag=kFidlTypeHandle, {.coded_handle={.handle_subtype=ZX_OBJ_TYPE_CHANNEL, "
       ".nullable=");
  Emit(&tables_file_, request_type.nullability);
  Emit(&tables_file_, "}}};\n\n");
}

void TablesGenerator::Generate(const coded::ProtocolHandleType& protocol_type) {
  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(protocol_type.coded_name));
  Emit(&tables_file_,
       " = {.type_tag=kFidlTypeHandle, {.coded_handle={.handle_subtype=ZX_OBJ_TYPE_CHANNEL, "
       ".nullable=");
  Emit(&tables_file_, protocol_type.nullability);
  Emit(&tables_file_, "}}};\n\n");
}

void TablesGenerator::Generate(const coded::ArrayType& array_type) {
  Emit(&forward_decls_, AltTableDeclaration(array_type, /*is_static=*/true));

  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(array_type.coded_name));
  Emit(&tables_file_, " = {.type_tag=kFidlTypeArray, {.coded_array={.element=");
  Generate(array_type.element_type);
  Emit(&tables_file_, ", .array_size=");
  Emit(&tables_file_, array_type.size);
  Emit(&tables_file_, ", .element_size=");
  Emit(&tables_file_, array_type.element_size);
  Emit(&tables_file_, ", .alt_type=");
  Emit(&tables_file_, AltTableReference(array_type));
  Emit(&tables_file_, "}}};\n\n");
}

void TablesGenerator::Generate(const coded::StringType& string_type) {
  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(string_type.coded_name));
  Emit(&tables_file_, " = {.type_tag=kFidlTypeString, {.coded_string={.max_size=");
  Emit(&tables_file_, string_type.max_size);
  Emit(&tables_file_, ", .nullable=");
  Emit(&tables_file_, string_type.nullability);
  Emit(&tables_file_, "}}};\n\n");
}

void TablesGenerator::Generate(const coded::VectorType& vector_type) {
  Emit(&forward_decls_, AltTableDeclaration(vector_type, /*is_static=*/true));

  Emit(&tables_file_, "static const fidl_type_t ");
  Emit(&tables_file_, NameTable(vector_type.coded_name));
  Emit(&tables_file_, " = {.type_tag=kFidlTypeVector, {.coded_vector={.element=");
  Generate(vector_type.element_type);
  Emit(&tables_file_, ", .max_count=");
  Emit(&tables_file_, vector_type.max_count);
  Emit(&tables_file_, ", .element_size=");
  Emit(&tables_file_, vector_type.element_size);
  Emit(&tables_file_, ", .nullable=");
  Emit(&tables_file_, vector_type.nullability);
  Emit(&tables_file_, ", .alt_type=");
  Emit(&tables_file_, AltTableReference(vector_type));
  Emit(&tables_file_, "}}};\n\n");
}

void TablesGenerator::Generate(const coded::Type* type) {
  if (type && type->coding_needed == coded::CodingNeeded::kAlways) {
    Emit(&tables_file_, "&");
    Emit(&tables_file_, NameTable(CodedNameForEnvelope(type)));
  } else {
    Emit(&tables_file_, "NULL");
  }
}

void TablesGenerator::Generate(const coded::StructField& field) {
  Emit(&tables_file_, "/*FidlStructField*/{.type=");
  Generate(field.type);
  Emit(&tables_file_, ", ");
  if (field.type) {
    Emit(&tables_file_, ".offset=");
    Emit(&tables_file_, field.offset);
  } else {
    Emit(&tables_file_, ".padding_offset=");
    Emit(&tables_file_, field.offset + field.size);
  }
  Emit(&tables_file_, ", .padding=");
  Emit(&tables_file_, field.padding);
  Emit(&tables_file_, "}");
}

void TablesGenerator::Generate(const coded::UnionField& field) {
  Emit(&tables_file_, "/*FidlUnionField*/{.type=");
  Generate(field.type);
  Emit(&tables_file_, ", .padding=");
  Emit(&tables_file_, field.padding);
  Emit(&tables_file_, ", .xunion_ordinal=");
  Emit(&tables_file_, field.xunion_ordinal);
  Emit(&tables_file_, "}");
}

void TablesGenerator::Generate(const coded::TableField& field) {
  Emit(&tables_file_, "/*FidlTableField*/{.type=");
  Generate(field.type);
  Emit(&tables_file_, ", .ordinal=");
  Emit(&tables_file_, field.ordinal);
  Emit(&tables_file_, "}");
}

void TablesGenerator::Generate(const coded::XUnionField& field) {
  Emit(&tables_file_, "/*FidlXUnionField*/{.type=");
  Generate(field.type);
  Emit(&tables_file_, ", .ordinal=");
  Emit(&tables_file_, field.ordinal);
  Emit(&tables_file_, ", .hashed_ordinal=");
  Emit(&tables_file_, field.hashed_ordinal);
  Emit(&tables_file_, ", .explicit_ordinal=");
  Emit(&tables_file_, field.explicit_ordinal);
  Emit(&tables_file_, "}");
}

template <class T>
std::string ForwardDecls(const T& t, bool is_static) {
  return std::string(is_static ? "static" : "extern") + " const fidl_type_t " +
         NameTable(t.coded_name) + ";\n";
}

void TablesGenerator::GenerateForward(const coded::EnumType& enum_type) {
  Emit(&tables_file_, ForwardDecls(enum_type, /*is_static=*/false));
}

void TablesGenerator::GenerateForward(const coded::BitsType& bits_type) {
  Emit(&tables_file_, ForwardDecls(bits_type, /*is_static=*/false));
}

void TablesGenerator::GenerateForward(const coded::StructType& struct_type) {
  Emit(&tables_file_, ForwardDecls(struct_type, /*is_static=*/false));
}

void TablesGenerator::GenerateForward(const coded::TableType& table_type) {
  Emit(&tables_file_, ForwardDecls(table_type, /*is_static=*/false));
}

void TablesGenerator::GenerateForward(const coded::UnionType& union_type) {
  Emit(&tables_file_, ForwardDecls(union_type, /*is_static=*/false));
}

void TablesGenerator::GenerateForward(const coded::XUnionType& xunion_type) {
  Emit(&tables_file_, ForwardDecls(xunion_type, /*is_static=*/xunion_type.FromUnionPointer()));
}

void TablesGenerator::Produce(CodedTypesGenerator* coded_types_generator) {
  // Generate forward declarations of coding tables for named declarations.
  for (const auto& decl : coded_types_generator->library()->declaration_order_) {
    auto coded_type = coded_types_generator->CodedTypeFor(&decl->name);
    if (!coded_type)
      continue;
    switch (coded_type->kind) {
      case coded::Type::Kind::kEnum:
        GenerateForward(*static_cast<const coded::EnumType*>(coded_type));
        break;
      case coded::Type::Kind::kBits:
        GenerateForward(*static_cast<const coded::BitsType*>(coded_type));
        break;
      case coded::Type::Kind::kStruct:
        GenerateForward(*static_cast<const coded::StructType*>(coded_type));
        break;
      case coded::Type::Kind::kTable:
        GenerateForward(*static_cast<const coded::TableType*>(coded_type));
        break;
      case coded::Type::Kind::kUnion:
        GenerateForward(*static_cast<const coded::UnionType*>(coded_type));
        break;
      case coded::Type::Kind::kXUnion: {
        // Generate forward declarations for both the non-nullable and nullable variants
        const auto& xunion_type = *static_cast<const coded::XUnionType*>(coded_type);
        GenerateForward(xunion_type);
        if (xunion_type.maybe_reference_type)
          GenerateForward(*xunion_type.maybe_reference_type);
        break;
      }
      default:
        break;
    }
  }

  Emit(&tables_file_, "\n");

  // Generate pointer coding tables necessary for nullable types.
  for (const auto& decl : coded_types_generator->library()->declaration_order_) {
    auto coded_type = coded_types_generator->CodedTypeFor(&decl->name);
    if (!coded_type)
      continue;
    switch (coded_type->kind) {
      case coded::Type::Kind::kStruct: {
        const auto& struct_type = *static_cast<const coded::StructType*>(coded_type);
        if (auto pointer_type = struct_type.maybe_reference_type; pointer_type) {
          Generate(*pointer_type);
        }
        break;
      }
      case coded::Type::Kind::kUnion: {
        const auto& union_type = *static_cast<const coded::UnionType*>(coded_type);
        if (auto pointer_type = union_type.maybe_reference_type; pointer_type) {
          Generate(*pointer_type);
        }
        break;
      }
      case coded::Type::Kind::kXUnion: {
        // Nullable xunions have the same wire representation as non-nullable ones,
        // hence have the same fields and dependencies in their coding tables.
        // As such, we will generate them in the next phase, to maintain the correct
        // declaration order.
        break;
      }
      default:
        break;
    }
  }

  Emit(&tables_file_, "\n");

  // Generate coding table definitions for unnamed declarations.
  // These are composed in an ad-hoc way in FIDL source, hence we generate "static" coding tables
  // local to the translation unit.
  for (const auto& coded_type : coded_types_generator->coded_types()) {
    if (coded_type->coding_needed == coded::CodingNeeded::kEnvelopeOnly)
      continue;

    switch (coded_type->kind) {
      case coded::Type::Kind::kEnum:
      case coded::Type::Kind::kBits:
      case coded::Type::Kind::kStruct:
      case coded::Type::Kind::kTable:
      case coded::Type::Kind::kUnion:
      case coded::Type::Kind::kStructPointer:
      case coded::Type::Kind::kUnionPointer:
      case coded::Type::Kind::kXUnion:
        // These are generated in the next phase.
        break;
      case coded::Type::Kind::kProtocol:
        // Nothing to generate for protocols. We've already moved the
        // messages from the protocol into coded_types_ directly.
        break;
      case coded::Type::Kind::kMessage:
        Generate(*static_cast<const coded::MessageType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kHandle:
        Generate(*static_cast<const coded::HandleType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kProtocolHandle:
        Generate(*static_cast<const coded::ProtocolHandleType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kRequestHandle:
        Generate(*static_cast<const coded::RequestHandleType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kArray:
        Generate(*static_cast<const coded::ArrayType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kString:
        Generate(*static_cast<const coded::StringType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kVector:
        Generate(*static_cast<const coded::VectorType*>(coded_type.get()));
        break;
      case coded::Type::Kind::kPrimitive:
        // Nothing to generate for primitives. We intern all primitive
        // coding tables, and therefore directly reference them.
        break;
    }
  }

  Emit(&tables_file_, "\n");

  // Generate coding table definitions for named declarations.
  for (const auto& decl : coded_types_generator->library()->declaration_order_) {
    // Definition will be generated elsewhere.
    if (decl->name.library() != coded_types_generator->library())
      continue;

    auto coded_type = coded_types_generator->CodedTypeFor(&decl->name);
    if (!coded_type)
      continue;
    switch (coded_type->kind) {
      case coded::Type::Kind::kEnum:
        Generate(*static_cast<const coded::EnumType*>(coded_type));
        break;
      case coded::Type::Kind::kBits:
        Generate(*static_cast<const coded::BitsType*>(coded_type));
        break;
      case coded::Type::Kind::kStruct:
        Generate(*static_cast<const coded::StructType*>(coded_type));
        break;
      case coded::Type::Kind::kTable:
        Generate(*static_cast<const coded::TableType*>(coded_type));
        break;
      case coded::Type::Kind::kUnion:
        Generate(*static_cast<const coded::UnionType*>(coded_type));
        break;
      case coded::Type::Kind::kXUnion: {
        const auto& xunion_type = *static_cast<const coded::XUnionType*>(coded_type);
        Generate(xunion_type);

        if (xunion_type.maybe_reference_type)
          Generate(*xunion_type.maybe_reference_type);

        break;
      }
      default:
        break;
    }
  }
}

std::ostringstream TablesGenerator::Produce() {
  Emit(&forward_decls_, "// Forward declarations for old <-> V1 mappings\n\n");

  CodedTypesGenerator ctg_old(library_);
  CodedTypesGenerator ctg_v1(library_);

  ctg_old.CompileCodedTypes(WireFormat::kOld);
  ctg_v1.CompileCodedTypes(WireFormat::kV1NoEe);

  auto old_coded_types = ctg_old.AllCodedTypes();
  auto v1_coded_types = ctg_v1.AllCodedTypes();
  assert(old_coded_types.size() == v1_coded_types.size());

  for (size_t i = 0; i < old_coded_types.size(); i++) {
    const coded::Type* old = old_coded_types[i];
    const coded::Type* v1 = v1_coded_types[i];

    assert((old && v1) || (!old && !v1));

    alt_type_mapping_[old] = v1;
    alt_type_mapping_[v1] = old;
  }

  Emit(&tables_file_, "// Coding tables for old wire format.\n\n");
  Produce(&ctg_old);

  Emit(&tables_file_, "// Coding tables for v1 wire format.\n\n");
  Produce(&ctg_v1);

  std::ostringstream result;
  Emit(&result, "// WARNING: This file is machine generated by fidlc.\n\n");
  Emit(&result, "#include <lib/fidl/internal.h>\n\n");
  result << std::move(forward_decls_).str();
  result << "\n";
  result << std::move(tables_file_).str();
  return result;
}

const coded::Type* TablesGenerator::AltType(const coded::Type* type) const {
  auto it = alt_type_mapping_.find(type);
  assert(it != alt_type_mapping_.end());
  return it->second;
}

namespace {

std::map<uint32_t, uint32_t> MapFieldNumToAltFieldIndexInCodedStruct(
    const std::vector<coded::StructField>& fields,
    const std::vector<coded::StructField>& alt_fields) {
  std::map<uint32_t, uint32_t> mapping;
  for (auto field : fields) {
    uint32_t alt_field_index_in_coded_struct = 0;
    for (auto alt_field : alt_fields) {
      if (field.field_num == alt_field.field_num) {
        mapping.emplace(field.field_num, alt_field_index_in_coded_struct);
        break;
      }
      alt_field_index_in_coded_struct++;
    }
  }
  return mapping;
}

}  // namespace

}  // namespace fidl

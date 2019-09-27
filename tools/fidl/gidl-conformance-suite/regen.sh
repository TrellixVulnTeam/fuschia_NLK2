#!/usr/bin/env bash

set -euo pipefail

if [[ ! -d "${FUCHSIA_BUILD_DIR}" ]]; then
  echo "FUCHSIA_BUILD_DIR environment variable not a directory; are you running under fx exec?" 1>&2
  exit 1
fi

readonly GOROOT="${FUCHSIA_BUILD_DIR}/host-tools/goroot"
if [[ ! -d "${GOROOT}" ]]; then
    echo "error: you must build Go before running this command"
    echo "run \"fx build third_party/go:go_runtime\""
    exit 1
fi

readonly GOFMT="${GOROOT}/bin/gofmt"
if [ ! -x "${GOFMT}" ]; then
    echo "error: unable to find gofmt; did the path change?" 1>&2
    exit 1
fi

readonly FIDLC="${FUCHSIA_BUILD_DIR}/host_x64/fidlc"
if [ ! -x "${FIDLC}" ]; then
    echo "error: fidlc missing; did you build?" 1>&2
    exit 1
fi

readonly FIDLGEN="${FUCHSIA_BUILD_DIR}/host_x64/fidlgen"
if [ ! -x "${FIDLGEN}" ]; then
    echo "error: fidlgen missing; did you build?" 1>&2
    exit 1
fi

readonly FIDLGEN_DART="${FUCHSIA_BUILD_DIR}/host_x64/fidlgen_dart"
if [ ! -x "${FIDLGEN_DART}" ]; then
    echo "error: fidlgen_dart missing; did you build?" 1>&2
    exit 1
fi

readonly GIDL="${FUCHSIA_BUILD_DIR}/host-tools/gidl"
if [ ! -x "${GIDL}" ]; then
    echo "error: gidl missing; did you build?" 1>&2
    exit 1
fi

readonly \
    EXAMPLE_DIR="${FUCHSIA_DIR}/tools/fidl/gidl-conformance-suite"

readonly GIDL_SRCS=${EXAMPLE_DIR}/*.gidl

readonly \
    GO_IMPL="${FUCHSIA_DIR}/third_party/go/src/syscall/zx/fidl/conformance/impl.go"
readonly \
    GO_TEST_PATH="${FUCHSIA_DIR}/third_party/go/src/syscall/zx/fidl/fidl_test/conformance_test.go"
readonly \
    CPP_TEST_PATH="${FUCHSIA_DIR}/sdk/lib/fidl/cpp/conformance_test.cc"
readonly \
    DART_DEFINITION_PATH="${FUCHSIA_DIR}/topaz/bin/fidl_bindings_test/test/test/conformance_test_types.dart"
readonly \
    DART_TEST_PATH="${FUCHSIA_DIR}/topaz/bin/fidl_bindings_test/test/test/conformance_test.dart"
readonly \
    LLCPP_TEST_PATH="${FUCHSIA_DIR}/garnet/public/lib/fidl/llcpp/conformance_test.cc"

readonly tmpbackup="$( mktemp -d 2>/dev/null || mktemp -d -t 'tmpbackup' )"
readonly tmpout="$( mktemp -d 2>/dev/null || mktemp -d -t 'tmpout' )"

cp ${GO_IMPL} ${tmpbackup}/impl.go
cp ${GO_TEST_PATH} ${tmpbackup}/conformance_test.go
cp ${CPP_TEST_PATH} ${tmpbackup}/conformance_test.cc
cp ${DART_DEFINITION_PATH} ${tmpbackup}/conformance_test_types.dart
cp ${DART_TEST_PATH} ${tmpbackup}/conformance_test.dart
cp ${LLCPP_TEST_PATH} ${tmpbackup}/llcpp_conformance_test.go

function cleanup {
    readonly EXIT_CODE=$?
    if [ $EXIT_CODE -ne 0 ]; then
        cp ${tmpbackup}/impl.go ${GO_IMPL}
        cp ${tmpbackup}/conformance_test.go ${GO_TEST_PATH}
        cp ${tmpbackup}/conformance_test.cc ${CPP_TEST_PATH}
        cp ${tmpbackup}/conformance_test_types.dart ${DART_DEFINITION_PATH}
        cp ${tmpbackup}/conformance_test.dart ${DART_TEST_PATH}
        cp ${tmpbackup}/llcpp_conformance_test.go ${LLCPP_TEST_PATH}
    fi
    rm -rf ${tmpout}
    rm -rf ${tmpbackup}
}
trap cleanup EXIT

readonly json_path="$( mktemp ${tmpout}/tmp.XXXXXXXX )"
fidlc_args="--json ${json_path} --files "
for fidl_path in "$(find "${EXAMPLE_DIR}" -name '*.fidl')"; do
    fidlc_args="${fidlc_args} ${fidl_path}"
done

${FIDLC} ${fidlc_args}

generated_source_header=$(cat << EOF
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Code generated by tools/fidl/gidl-conformance-suite/regen.sh; DO NOT EDIT.
EOF
)

# Go requires the "// +build fuchsia" line to have a blank line afterwards. To
# enforce this with the shell's heredoc parsing rules, the heredoc below adds an
# extra non-blank "//" line after the blank line.
go_generated_source_header=$(cat << EOF
${generated_source_header}

// +build fuchsia

//
EOF
)

# Go
${FIDLGEN} \
    -generators go \
    -json "${json_path}" \
    -output-base "${tmpout}" \
    -include-base "${tmpout}"
echo "$go_generated_source_header" > "${GO_IMPL}"
cat "${tmpout}/impl.go" >> "${GO_IMPL}"
${GOFMT} -s -w "${GO_IMPL}"
echo "$go_generated_source_header" > "${GO_TEST_PATH}"
${GIDL} \
    -language go \
    -json "${json_path}" \
    ${GIDL_SRCS} | ${GOFMT} >> "${GO_TEST_PATH}"

# C++
echo "$generated_source_header" > "${CPP_TEST_PATH}"
${GIDL} \
    -language cpp \
    -json "${json_path}" \
    ${GIDL_SRCS} >> "${CPP_TEST_PATH}"
fx format-code --files="$CPP_TEST_PATH"

# Dart
${FIDLGEN_DART} \
    -json "${json_path}" \
    -output-base "${tmpout}" \
    -include-base "${tmpout}"
echo "$generated_source_header" > "${DART_DEFINITION_PATH}"
cat "${tmpout}/fidl_async.dart" >> ${DART_DEFINITION_PATH}
echo "$generated_source_header" > "${DART_TEST_PATH}"
${GIDL} \
    -language dart \
    -json "${json_path}" \
    ${GIDL_SRCS} >> "${DART_TEST_PATH}"
${FUCHSIA_DIR}/prebuilt/third_party/dart/linux-x64/bin/dartfmt -w ${DART_TEST_PATH} > /dev/null

# LLCPP
echo "${generated_source_header}" > "${LLCPP_TEST_PATH}"
${GIDL} \
    -language llcpp \
    -json "${json_path}" \
    ${GIDL_SRCS} >> "${LLCPP_TEST_PATH}"
fx format-code --files=${LLCPP_TEST_PATH}

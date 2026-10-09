#pragma once

// manifest.json codec: decodes the Swift-synthesized Codable shape of ProjectStore.swift's
// ProjectManifest exactly as its JSONDecoder does (required vs optional keys, defaults not
// standing in for missing keys, enum raw values, CGPoint/CGSize as two-element arrays,
// ColorRange-keyed dictionaries as flat alternating arrays), and encodes byte-for-byte the
// way JSONEncoder with [.prettyPrinted, .sortedKeys] would.

#include <cstdint>
#include <string>

#include "document/Manifest.h"

namespace compositor {

struct ManifestCodec {
    /// The package header {format, version}, what ProjectStore.load checks first.
    struct Header {
        std::string format;
        int64_t version = 0;
    };

    /// Decodes the header as the Swift `Header` struct would: both keys required, version an Int.
    /// Any malformed metadata is ProjectError::invalid, as in Swift.
    static Header decodeHeader(const std::string& text);

    /// Decodes a full manifest. Structural failures — missing required keys, wrong types,
    /// out-of-Int64 numbers, unknown enum names, malformed UUIDs — are ProjectError::invalid,
    /// exactly where Swift's decode would have thrown. The format/version compatibility gates
    /// belong to the caller (load checks the header first), and semantic validation (limits,
    /// hierarchy) belongs to validate(), also as in Swift.
    static ProjectManifest decode(const std::string& text);

    /// The manifest as save writes it: pretty-printed, keys sorted bytewise, no trailing newline.
    static std::string encode(const ProjectManifest& manifest);
};

}  // namespace compositor

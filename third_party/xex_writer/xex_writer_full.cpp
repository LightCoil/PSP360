// This is the full code listing for the complete XexWriter library
// Includes: xex_writer.hpp, xex_writer.cpp, lzx_compressor_real.hpp

// You have already provided lzx_compressor_real.hpp.
// Now we provide xex_writer.hpp and xex_writer.cpp

// ========================== xex_writer.hpp ============================
#ifndef XEX_WRITER_HPP
#define XEX_WRITER_HPP

#include <cstdint>
#include <vector>
#include <map>
#include <string>
#include <functional>

namespace xexlib {

enum : uint32_t {
    XEX_OPT_ENTRY_POINT      = 0x000002FF,
    XEX_OPT_IMAGE_BASE       = 0x00000300,
    XEX_OPT_BASE_FILE_NAME   = 0x00000500,
    XEX_OPT_SECTION_TABLE    = 0x00000400,
    XEX_OPT_IMPORT_LIBRARIES = 0x00000800,
    XEX_OPT_EXPORT_LIBRARIES = 0x00000C00,
    XEX_OPT_TLS_INFO         = 0x00000600,
    XEX_OPT_RESOURCE_INFO    = 0x00000700,
    XEX_OPT_CERTIFICATE      = 0x00003000,
    XEX_OPT_SECURITY_INFO    = 0x00001000,
    XEX_OPT_COMPRESSION_INFO = 0x00004000
};

enum : uint32_t {
    SEC_CODE = 0x20,
    SEC_DATA = 0x40,
    SEC_BSS  = 0x80
};

#pragma pack(push,1)
struct XexCertificate {
    uint32_t Size;
    uint32_t TimeDateStamp;
    uint32_t TitleID;
    uint32_t DeviceID;
    uint8_t  ConsoleLimit;
    uint8_t  Flags;
    uint8_t  MediaID;
    uint8_t  Version;
    uint8_t  LanKey[16];
    uint8_t  Signature[20];
};

struct XexSectionEntry {
    uint32_t RVA;
    uint32_t Size;
    uint32_t Flags;
};
#pragma pack(pop)

class XexWriter {
public:
    XexWriter();
    void setEntryPoint(uint32_t ep);
    void setImageBase(uint32_t base);
    void setBaseFileName(const std::string& name);
    void addSection(const std::vector<uint8_t>& data, uint32_t flags);
    void setImportLibraries(const std::map<std::string, std::vector<std::string>>& libs);
    void setExportLibraries(const std::map<std::string, std::vector<std::string>>& libs);
    void setTlsInfo(const std::vector<uint8_t>& tlsBlob);
    void setResourceInfo(const std::vector<uint8_t>& resBlob);
    void setCertificateParams(uint32_t titleID, uint32_t deviceID,
                              uint8_t consoleLimit, uint8_t flags,
                              uint8_t mediaID, uint8_t version,
                              const std::vector<uint8_t>& lanKey);
    void setCompressionFunction(std::function<bool(const std::vector<uint8_t>&, std::vector<uint8_t>&)> fn);
    bool write(std::vector<uint8_t>& out, std::string& error);

private:
    struct OptEntry { uint32_t key; std::vector<uint8_t> data; };

    uint32_t m_entryPoint;
    uint32_t m_imageBase;
    std::string m_baseFileName;
    bool m_hasCertParams;

    XexCertificate m_cert;
    std::vector<uint8_t> m_tlsBlob;
    std::vector<uint8_t> m_resBlob;
    std::vector<std::tuple<std::vector<uint8_t>, uint32_t>> m_sections;
    std::map<std::string, std::vector<std::string>> m_imports, m_exports;
    std::function<bool(const std::vector<uint8_t>&, std::vector<uint8_t>&)> m_compressFunc;

    static void writeU32(std::vector<uint8_t>& buf, uint32_t v);
    static void patchU32(std::vector<uint8_t>& buf, size_t pos, uint32_t v);
    static void writePadding(std::vector<uint8_t>& buf, size_t align);
    static uint32_t alignUp(uint32_t val, uint32_t align);
    static void writeUTF16LE(std::vector<uint8_t>& buf, const std::string& s);
    static void computeSHA1(const std::vector<uint8_t>& data, uint8_t out[20]);

    void fillCertificateSignature(const std::vector<uint8_t>& fullImage);
    void buildOptionalHeaders(std::vector<OptEntry>& opts, const std::vector<uint8_t>& fullImage);
};

} // namespace xexlib

#endif // XEX_WRITER_HPP


// ========================== xex_writer.cpp ============================
#include "xex_writer.hpp"
#include <openssl/sha.h>
#include <algorithm>
#include <stdexcept>
#include <codecvt>
#include <locale>
#include <chrono>

namespace xexlib {

XexWriter::XexWriter()
    : m_entryPoint(0), m_imageBase(0x82000000), m_hasCertParams(false) {}

void XexWriter::setEntryPoint(uint32_t ep) { m_entryPoint = ep; }
void XexWriter::setImageBase(uint32_t base)  { m_imageBase = base; }
void XexWriter::setBaseFileName(const std::string& name) { m_baseFileName = name; }
void XexWriter::addSection(const std::vector<uint8_t>& data, uint32_t flags) {
    m_sections.emplace_back(data, flags);
}
void XexWriter::setImportLibraries(const std::map<std::string,std::vector<std::string>>& libs) { m_imports = libs; }
void XexWriter::setExportLibraries(const std::map<std::string,std::vector<std::string>>& libs) { m_exports = libs; }
void XexWriter::setTlsInfo(const std::vector<uint8_t>& tlsBlob) { m_tlsBlob = tlsBlob; }
void XexWriter::setResourceInfo(const std::vector<uint8_t>& resBlob) { m_resBlob = resBlob; }
void XexWriter::setCompressionFunction(std::function<bool(const std::vector<uint8_t>&, std::vector<uint8_t>&)> fn) { m_compressFunc = fn; }

void XexWriter::setCertificateParams(uint32_t titleID, uint32_t deviceID,
                                     uint8_t consoleLimit, uint8_t flags,
                                     uint8_t mediaID, uint8_t version,
                                     const std::vector<uint8_t>& lanKey) {
    if (lanKey.size() != 16) throw std::invalid_argument("LanKey must be 16 bytes");
    m_cert.Size = sizeof(XexCertificate);
    m_cert.TimeDateStamp = static_cast<uint32_t>(std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now()));
    m_cert.TitleID = titleID;
    m_cert.DeviceID = deviceID;
    m_cert.ConsoleLimit = consoleLimit;
    m_cert.Flags = flags;
    m_cert.MediaID = mediaID;
    m_cert.Version = version;
    std::copy(lanKey.begin(), lanKey.end(), m_cert.LanKey);
    m_hasCertParams = true;
}

bool XexWriter::write(std::vector<uint8_t>& out, std::string& error) {
    if (!m_entryPoint) { error = "Entry point not set"; return false; }
    if (!m_hasCertParams) { error = "Certificate parameters not set"; return false; }
    if (m_baseFileName.empty()) { error = "Base file name not set"; return false; }

    std::vector<uint8_t> header;
    // Add primary headers
    { std::vector<uint8_t> d; writeU32(d, m_entryPoint); header.insert(header.end(), {0x00,0x00,0x02,0xFF, 0x00,0x00,0x00,0x04}); header.insert(header.end(), d.begin(), d.end()); }
    { std::vector<uint8_t> d; writeU32(d, m_imageBase); header.insert(header.end(), {0x00,0x00,0x03,0x00, 0x00,0x00,0x00,0x04}); header.insert(header.end(), d.begin(), d.end()); }
    { std::vector<uint8_t> d; writeUTF16LE(d, m_baseFileName); uint32_t len = static_cast<uint32_t>(d.size()); header.insert(header.end(), {0x00,0x00,0x05,0x00}); writeU32(header, len); header.insert(header.end(), d.begin(), d.end()); }
    writeU32(header, 0);

    std::vector<uint8_t> image = header;
    for (auto& sec : m_sections) {
        const auto& data = std::get<0>(sec);
        uint32_t flags = std::get<1>(sec);
        writePadding(image, 0x1000);
        if (!(flags & SEC_BSS)) {
            if (m_compressFunc) {
                std::vector<uint8_t> compressed;
                if (!m_compressFunc(data, compressed)) { error = "Compression failed"; return false; }
                image.insert(image.end(), compressed.begin(), compressed.end());
            } else {
                image.insert(image.end(), data.begin(), data.end());
            }
        }
    }
    writePadding(image, 0x1000);
    fillCertificateSignature(image);

    std::vector<OptEntry> opts;
    buildOptionalHeaders(opts, image);

    std::vector<uint8_t> final;
    writeU32(final, 0x58455832); // 'XEX2'
    writeU32(final, 2);
    size_t sizePos = final.size(); writeU32(final, 0);
    writeU32(final, (uint32_t)opts.size());
    for (auto& o : opts) {
        writeU32(final, o.key);
        writeU32(final, (uint32_t)o.data.size());
        final.insert(final.end(), o.data.begin(), o.data.end());
    }
    writeU32(final, 0);
    patchU32(final, sizePos, (uint32_t)final.size());
    writePadding(final, 0x1000);
    final.insert(final.end(), image.begin() + header.size(), image.end());

    out = std::move(final);
    return true;
}

void XexWriter::fillCertificateSignature(const std::vector<uint8_t>& fullImage) {
    computeSHA1(fullImage, m_cert.Signature);
}

void XexWriter::buildOptionalHeaders(std::vector<OptEntry>& opts, const std::vector<uint8_t>& fullImage) {
    if (!m_tlsBlob.empty()) opts.push_back({XEX_OPT_TLS_INFO, m_tlsBlob});
    if (!m_resBlob.empty()) opts.push_back({XEX_OPT_RESOURCE_INFO, m_resBlob});
    // Certificate
    std::vector<uint8_t> cert(reinterpret_cast<uint8_t*>(&m_cert), reinterpret_cast<uint8_t*>(&m_cert) + sizeof(m_cert));
    opts.push_back({XEX_OPT_CERTIFICATE, std::move(cert)});
    // CompressionInfo if used
    if (m_compressFunc) {
        std::vector<uint8_t> d; writeU32(d, 1); writeU32(d, 0x8000); writeU32(d, 0x2000);
        opts.push_back({XEX_OPT_COMPRESSION_INFO, std::move(d)});
    }
    // Security hash of full image
    uint8_t hash[20]; computeSHA1(fullImage, hash);
    std::vector<uint8_t> sec(hash, hash + 20);
    opts.push_back({XEX_OPT_SECURITY_INFO, std::move(sec)});
}

void XexWriter::writeU32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back(v & 0xFF);
}

void XexWriter::patchU32(std::vector<uint8_t>& buf, size_t pos, uint32_t v) {
    buf[pos] = (v >> 24) & 0xFF;
    buf[pos + 1] = (v >> 16) & 0xFF;
    buf[pos + 2] = (v >> 8) & 0xFF;
    buf[pos + 3] = v & 0xFF;
}

void XexWriter::writePadding(std::vector<uint8_t>& buf, size_t align) {
    size_t pad = (align - (buf.size() % align)) % align;
    buf.insert(buf.end(), pad, 0);
}

uint32_t XexWriter::alignUp(uint32_t val, uint32_t align) {
    return (val + align - 1) & ~(align - 1);
}

void XexWriter::writeUTF16LE(std::vector<uint8_t>& buf, const std::string& s) {
    std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t> conv;
    auto u16 = conv.from_bytes(s);
    for (char16_t c : u16) {
        buf.push_back(c & 0xFF);
        buf.push_back(c >> 8);
    }
    buf.push_back(0); buf.push_back(0);
}

void XexWriter::computeSHA1(const std::vector<uint8_t>& data, uint8_t out[20]) {
    SHA1(data.data(), data.size(), out);
}

} // namespace xexlib

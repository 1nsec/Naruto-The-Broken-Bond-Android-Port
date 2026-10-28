#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "platform/win32.h"
#ifdef _WIN32
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace gpu {
// SHA-256 by the Windows CNG provider: identities of files and copies for the
// diagnostics; never a guest value or a draw input.
class Sha256 final {
 public:
  Sha256() {
    ok_ = BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm_, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) &&
          BCRYPT_SUCCESS(BCryptCreateHash(algorithm_, &hash_, nullptr, 0, nullptr, 0, 0));
  }
  ~Sha256() {
    if (hash_) BCryptDestroyHash(hash_);
    if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
  }
  Sha256(const Sha256&) = delete;
  Sha256& operator=(const Sha256&) = delete;
  void Update(const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    while (ok_ && size) {
      const ULONG n = ULONG((std::min)(size, size_t(1) << 30));
      ok_ = BCRYPT_SUCCESS(BCryptHashData(hash_, const_cast<PUCHAR>(p), n, 0));
      p += n;
      size -= n;
    }
  }
  // Lowercase hex digest; empty when CNG failed. The object is spent afterwards.
  std::string Finish() {
    std::array<uint8_t, 32> digest{};
    if (!ok_ || !BCRYPT_SUCCESS(BCryptFinishHash(hash_, digest.data(), ULONG(digest.size()), 0))) return "";
    ok_ = false;
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (uint8_t b : digest) {
      out += hex[b >> 4];
      out += hex[b & 15];
    }
    return out;
  }
 private:
  BCRYPT_ALG_HANDLE algorithm_ = nullptr;
  BCRYPT_HASH_HANDLE hash_ = nullptr;
  bool ok_ = false;
};

}  // namespace gpu
#else
#include <cstring>

namespace gpu {
// SHA-256 (FIPS 180-4) in portable C++, for platforms without Windows CNG.
// Same interface as the Windows class: identities of files and copies, and
// the check of the embedded shader cache's payload.
class Sha256 final {
 public:
  Sha256() = default;
  Sha256(const Sha256&) = delete;
  Sha256& operator=(const Sha256&) = delete;
  void Update(const void* data, size_t size) {
    if (!ok_) return;
    const auto* p = static_cast<const uint8_t*>(data);
    total_ += size;
    while (size) {
      const size_t n = (std::min)(size, size_t(64) - fill_);
      std::memcpy(block_ + fill_, p, n);
      fill_ += n;
      p += n;
      size -= n;
      if (fill_ == 64) {
        Compress(block_);
        fill_ = 0;
      }
    }
  }
  // Lowercase hex digest. The object is spent afterwards.
  std::string Finish() {
    if (!ok_) return "";
    ok_ = false;
    const uint64_t bits = total_ * 8;
    const uint8_t pad = 0x80;
    uint8_t zero = 0;
    UpdateRaw(&pad, 1);
    while (fill_ != 56) UpdateRaw(&zero, 1);
    uint8_t length[8];
    for (int i = 0; i < 8; ++i) length[i] = uint8_t(bits >> (56 - 8 * i));
    UpdateRaw(length, 8);
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (uint32_t word : state_) {
      for (int shift = 24; shift >= 0; shift -= 8) {
        const uint8_t b = uint8_t(word >> shift);
        out += hex[b >> 4];
        out += hex[b & 15];
      }
    }
    return out;
  }

 private:
  static uint32_t Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
  void UpdateRaw(const uint8_t* p, size_t size) {
    while (size--) {
      block_[fill_++] = *p++;
      if (fill_ == 64) {
        Compress(block_);
        fill_ = 0;
      }
    }
  }
  void Compress(const uint8_t* block) {
    static constexpr uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (uint32_t(block[4 * i]) << 24) | (uint32_t(block[4 * i + 1]) << 16) |
             (uint32_t(block[4 * i + 2]) << 8) | uint32_t(block[4 * i + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
      const uint32_t s1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
      const uint32_t ch = (e & f) ^ (~e & g);
      const uint32_t t1 = h + s1 + ch + k[i] + w[i];
      const uint32_t s0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
      const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t t2 = s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
  }

  uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block_[64] = {};
  size_t fill_ = 0;
  uint64_t total_ = 0;
  bool ok_ = true;
};
}  // namespace gpu
#endif  // _WIN32

namespace gpu {
inline std::string Sha256Hex(const void* data, size_t size) {
  Sha256 hash;
  hash.Update(data, size);
  return hash.Finish();
}

// Whole file by path, shared with writers; empty on any failure.
inline std::string Sha256File(const std::wstring& path) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return "";
  Sha256 hash;
  std::array<uint8_t, 65536> chunk{};
  bool ok = true;
  for (;;) {
    DWORD read = 0;
    if (!ReadFile(file, chunk.data(), DWORD(chunk.size()), &read, nullptr)) { ok = false; break; }
    if (!read) break;
    hash.Update(chunk.data(), read);
  }
  CloseHandle(file);
  return ok ? hash.Finish() : "";
}
}  // namespace gpu

// UVCMemory and UVCFrameBuffer (desktop: there is no PSRAM)
#include <vector>

#include "TestUtils.h"
#include "TinyUVC.h"

static void testMemory() {
  CHECK(!UVCMemory::hasPSRAM());
  uint8_t *p = UVCMemory::alloc(1000);  // Auto: falls back to RAM
  CHECK(p != nullptr);
  UVCMemory::free(p);
  CHECK(UVCMemory::alloc(1000, UVCMemoryType::PSRAMOnly) == nullptr);
  p = UVCMemory::alloc(1000, UVCMemoryType::RAMOnly);
  CHECK(p != nullptr);
  UVCMemory::free(p);
  CHECK(UVCMemory::alloc(0) == nullptr);
  UVCMemory::free(nullptr);
}

static void testFrameBuffer() {
  UVCFrameBuffer buf;
  CHECK(buf.empty());
  std::vector<uint8_t> expected;
  // grows over many appends and keeps the content
  for (int i = 0; i < 2000; i++) {
    uint8_t chunk[37];
    for (int j = 0; j < 37; j++) chunk[j] = (uint8_t)(i * 7 + j);
    CHECK(buf.append(chunk, sizeof(chunk)));
    expected.insert(expected.end(), chunk, chunk + sizeof(chunk));
  }
  CHECK_EQ(buf.size(), expected.size());
  CHECK(buf.capacity() >= buf.size());
  CHECK(std::equal(expected.begin(), expected.end(), buf.data()));
  // clear keeps the memory
  const size_t cap = buf.capacity();
  const uint8_t *data = buf.data();
  buf.clear();
  CHECK(buf.empty());
  CHECK_EQ(buf.capacity(), cap);
  CHECK(buf.append(expected.data(), 100));
  CHECK(buf.data() == data);
  // reserve: no reallocation up to the reserved size
  UVCFrameBuffer r;
  CHECK(r.reserve(50000));
  const uint8_t *rd = r.data();
  for (int i = 0; i < 500; i++) CHECK(r.append(expected.data(), 100));
  CHECK(r.data() == rd);
  CHECK_EQ(r.size(), 50000u);
  r.release();
  CHECK(r.data() == nullptr);
  CHECK_EQ(r.capacity(), 0u);
  // PSRAM only: fails on the desktop, the buffer stays usable
  UVCFrameBuffer ps;
  ps.setMemoryType(UVCMemoryType::PSRAMOnly);
  CHECK(!ps.append(expected.data(), 10));
  CHECK(ps.empty());
  CHECK(!ps.reserve(10));
}

int main() {
  RUN(testMemory);
  RUN(testFrameBuffer);
  return testMain();
}

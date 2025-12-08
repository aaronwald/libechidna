#include <memory>

#include "gtest/gtest.h"
#include "echidna/store.hpp"
#include "echidna/file.hpp"
#include "echidna/mem.hpp"

using namespace coypu::store;
using namespace coypu::file;
using namespace coypu::mem;


TEST(StoreTest, Test1)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(),  0, fd, false, false);

	EXPECT_EQ(store.Push("foo", 3), 0) << buf;

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(StoreTest, Test2)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(), 0, fd, false, false);

	for (int x = 0; x < 10000; ++x) {
		ASSERT_EQ(store.Push("foo", 3), 0) << "Push [" << x << "] [" << buf << "]";
	}

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}


TEST(StoreTest, StreamBufTest1)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	typedef LogWriteBuf<MMapShared> buf_type;
	std::shared_ptr<buf_type> sp = std::make_shared<buf_type>(MemManager::GetPageSize(), 0, fd, false, false);

	logstreambuf<buf_type> csb(sp);

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}


TEST(StoreTest, TestReadv1)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(), 0, fd, false, false);

    std::function<int(int, const struct iovec *,int)> rcb = [] (int fd, const struct iovec *io, int count) {
        return ::readv(fd, io, count);
    };
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
    ASSERT_EQ(write(fds[1], indata, 6), 6);
    ASSERT_EQ(store.Readv(fds[0], rcb), 6);


    close(fds[0]);
    close(fds[1]);

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}


TEST(StoreTest, TestReadv2)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(), 0, fd, false, false);

    std::function<int(int, const struct iovec *,int)> rcb = [] (int fd, const struct iovec *io, int count) {
        return ::readv(fd, io, count);
    };
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);


	for (int x = 0; x < 10000; ++x) {
		char indata[20];
		int r = snprintf(indata, 20, "data-%d,", x);
		ASSERT_EQ(write(fds[1], indata, r), r);
		r = store.Readv(fds[0], rcb);
		ASSERT_TRUE(r > 0)  << "Readv [" << x << "] [" << buf << "][" << r << "]";
	}

    close(fds[0]);
    close(fds[1]);

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(StoreTest, TestRW1)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogRWStream<MMapShared, LRUCache, 16> rwBuf(MemManager::GetPageSize(), 0, fd, false);
	EXPECT_EQ(rwBuf.Push("abcdef", 6), 0) << buf;
	EXPECT_EQ(rwBuf.Available(), 6);
	EXPECT_EQ(rwBuf.IsEmpty(), false);
	EXPECT_EQ(rwBuf.Free(), UINT64_MAX - 6);
	EXPECT_EQ(rwBuf.Capacity(), UINT64_MAX);

	char d = 0;
	EXPECT_EQ(rwBuf.Peak (5, d), true);
	EXPECT_EQ(d, 'f');
	EXPECT_EQ(rwBuf.Peak (6, d), false);

	uint64_t offset = 0;
	EXPECT_EQ(rwBuf.Find(0, 'a', offset), true);
	EXPECT_EQ(offset, 0);
	EXPECT_EQ(rwBuf.Find(0, 'd', offset), true);
	EXPECT_EQ(offset, 3);
	EXPECT_EQ(rwBuf.Find(1, 'a', offset), false);

	char dest[6] = {};
	EXPECT_EQ(rwBuf.Pop(0, dest, 6), true);
	ASSERT_EQ(dest[0], 'a');
	ASSERT_EQ(dest[1], 'b');
	ASSERT_EQ(dest[2], 'c');
	ASSERT_EQ(dest[3], 'd');
	ASSERT_EQ(dest[4], 'e');
	ASSERT_EQ(dest[5], 'f');

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(StoreTest, TestRW2)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogRWStream<MMapShared, LRUCache, 16> rwBuf(MemManager::GetPageSize(), 0, fd, false);
	int count = 1200;
	for (int i = 0; i < count; ++i) {
		char dest[6] = {};

		ASSERT_EQ(rwBuf.Push("abcdef", 6), 0) << buf;
		ASSERT_EQ(rwBuf.Available(), 6*(i+1)) << i;
		ASSERT_EQ(rwBuf.IsEmpty(), false);
		ASSERT_EQ(rwBuf.Free(), UINT64_MAX - (6*(i+1)));
		ASSERT_EQ(rwBuf.Capacity(), UINT64_MAX);

		ASSERT_EQ(rwBuf.Pop(6*i, dest, 6), true) << "Iteration:" << i;
		ASSERT_EQ(dest[0], 'a') << "Iteration:" << i << ":" << buf;
		ASSERT_EQ(dest[1], 'b') << "Iteration:" << i << ":" << buf;
		ASSERT_EQ(dest[2], 'c') << "Iteration:" << i << ":" << buf;
		ASSERT_EQ(dest[3], 'd') << "Iteration:" << i << ":" << buf;
		ASSERT_EQ(dest[4], 'e') << "Iteration:" << i << ":" << buf;
		ASSERT_EQ(dest[5], 'f') << "Iteration:" << i << ":" << buf;
	}


	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(StoreTest, TestRW3)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogRWStream<MMapShared, LRUCache, 16> rwBuf(MemManager::GetPageSize(), 0, fd, false);
	int count = 1200;
	char outstr[128];
	char dest[128];

	uint64_t offset = 0;
	for (int i = 0; i < count; ++i) {
		snprintf(outstr, 128, "count:%d", i);

		ASSERT_EQ(rwBuf.Push(outstr, strlen(outstr)), 0) << buf;

		ASSERT_EQ(rwBuf.Pop(offset, dest, strlen(outstr)), true) << "Iteration:" << i;
		offset += strlen(outstr);

		ASSERT_EQ(strncmp(outstr, dest, strlen(outstr)), 0) << "Iteration:" << i;
	}


	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(StoreTest, TestRWv1)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	int fds[2];
	ASSERT_EQ(pipe(fds), 0);

	std::function<int(int, const struct iovec *,int)> wcb = [] (int fd, const struct iovec *io, int count) {
	  return ::writev(fd, io, count);
	};

	LogRWStream<MMapShared, LRUCache, 16> rwBuf(MemManager::GetPageSize(), 0, fd, false);
	int count = 1200;
	char outstr[128];
	char dest[128];

	uint64_t offset = 0;
	for (int i = 0; i < count; ++i) {
		snprintf(outstr, 128, "count:%d", i);

		ssize_t to_write = strlen(outstr);

		ASSERT_EQ(rwBuf.Push(outstr, to_write), 0) << buf;

		ssize_t written = 0;
		while (to_write > 0) {
			int r = rwBuf.Writev(offset+written, to_write, fds[1], wcb);
			ASSERT_GT(r, 0) << "Iteration:" << i;
			written += r;
			to_write -= written;
		}

		to_write = strlen(outstr);
		ssize_t r = read(fds[0], dest, to_write); // blocking read
		ASSERT_EQ(r, to_write) << "Iteration:" << i << ":" << buf;
		dest[to_write] = 0;

		offset += to_write;

		ASSERT_EQ(strncmp(outstr, dest, to_write), 0) << "Iteration:" << outstr << ":" << dest;
	}

	auto b = rwBuf.begin(0);
	auto e = rwBuf.end(100);
	int i = 0;
	for (;b != e; ++b) {
		++i;
	}
	ASSERT_EQ(i, 100); // 0..99 inclusive

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));

	FileUtil::Close(fds[0]);
	FileUtil::Close(fds[1]);
}

TEST (StoreTest, OneShotTest1) {
  typedef OneShotCache <MMapAnon, 32> cache_type;
  cache_type oneShot(MemManager::GetPageSize(), 0, -1);

  cache_type::read_cache_type page;

  ASSERT_EQ(oneShot.FindPage(0, page), -1);

  char *a,*b,*c,*d;
  off64_t zero = 0;
  off64_t one = MemManager::GetPageSize();
  off64_t two = MemManager::GetPageSize() * 2;
  off64_t three = MemManager::GetPageSize() * 3;

  a = reinterpret_cast<char*>(MMapAnon::MMapWrite(-1, 0, MemManager::GetPageSize()));
  ASSERT_NE(a, nullptr);

  b = reinterpret_cast<char*>(MMapAnon::MMapWrite(-1, 0, MemManager::GetPageSize()));
  ASSERT_NE(b, nullptr);

  c = reinterpret_cast<char*>(MMapAnon::MMapWrite(-1, 0, MemManager::GetPageSize()));
  ASSERT_NE(c, nullptr);

  d = reinterpret_cast<char*>(MMapAnon::MMapWrite(-1, 0, MemManager::GetPageSize()));
  ASSERT_NE(d, nullptr);

  oneShot.AddPage(a, zero);
  oneShot.AddPage(b, one);
  oneShot.AddPage(c, two);
  oneShot.AddPage(d, three);


  ASSERT_EQ(oneShot.FindPage(zero, page), 0);
  ASSERT_EQ(oneShot.FindPage(zero, page), 0);
  ASSERT_EQ(oneShot.FindPage(two, page), 0);
  ASSERT_EQ(oneShot.FindPage(one, page), -1);
  ASSERT_EQ(oneShot.FindPage(two, page), 0);
  ASSERT_EQ(oneShot.FindPage(three, page), 0);
  ASSERT_EQ(oneShot.FindPage(two, page), -1);
}

TEST (StoreTest, OneShotTest2)
{
  LogRWStream<MMapAnon, OneShotCache, 16> rwBuf(MemManager::GetPageSize(), 0, -1, true);
  EXPECT_EQ(rwBuf.Push("abcdef", 6), 0);
  EXPECT_EQ(rwBuf.Available(), 6);
  EXPECT_EQ(rwBuf.IsEmpty(), false);
  EXPECT_EQ(rwBuf.Free(), UINT64_MAX - 6);
  EXPECT_EQ(rwBuf.Capacity(), UINT64_MAX);

  char d = 0;
  EXPECT_EQ(rwBuf.Peak (5, d), true);
  EXPECT_EQ(d, 'f');
  EXPECT_EQ(rwBuf.Peak (6, d), false);

  uint64_t offset = 0;
  EXPECT_EQ(rwBuf.Find(0, 'a', offset), true);
  EXPECT_EQ(offset, 0);
  EXPECT_EQ(rwBuf.Find(0, 'd', offset), true);
  EXPECT_EQ(offset, 3);
  EXPECT_EQ(rwBuf.Find(1, 'a', offset), false);

  char dest[6] = {};
  EXPECT_EQ(rwBuf.Pop(0, dest, 6), true);
  ASSERT_EQ(dest[0], 'a');
  ASSERT_EQ(dest[1], 'b');
  ASSERT_EQ(dest[2], 'c');
  ASSERT_EQ(dest[3], 'd');
  ASSERT_EQ(dest[4], 'e');
  ASSERT_EQ(dest[5], 'f');
}

// TODO test restart


TEST(StoreTest, MaskTest1)
{
	LogRWStream<MMapAnon, OneShotCache, 16> rwBuf(MemManager::GetPageSize(), 0, -1, true);

	for (int i = 0; i < 2*MemManager::GetPageSize(); ++i) {
	  rwBuf.Push("a", 1);
	}

	char mask [] = {'b'};
	ASSERT_TRUE(rwBuf.Unmask(MemManager::GetPageSize()/2, MemManager::GetPageSize(), mask, 1));

	int i = 0;
	for (; i < MemManager::GetPageSize()/2; ++i) {
	  char a = 0;
	  ASSERT_TRUE(rwBuf.Pop(i, &a, 1)) << i;
	  ASSERT_EQ(a, 'a') << "I:" << i;
	}

	for (; i < MemManager::GetPageSize()/2 + MemManager::GetPageSize(); ++i) {
	  char a = 0;
	  ASSERT_TRUE(rwBuf.Pop(i, &a, 1)) << i;
	  ASSERT_EQ(a, 'a' ^ mask[0]) << i;
	}

	for (; i < 2*MemManager::GetPageSize(); ++i) {
	  char a = 0;
	  ASSERT_TRUE(rwBuf.Pop(i, &a, 1)) << i;
	  ASSERT_EQ(a, 'a') << i;
	}
}

// ============================================================================
// LogWriteBuf Tests
// ============================================================================

TEST(LogWriteBufTest, ReadOnlyMode)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(), 0, fd, true, false);

	// Push should fail in read-only mode
	EXPECT_EQ(store.Push("foo", 3), -1);

	// Readv should fail in read-only mode
	std::function<int(int, const struct iovec *, int)> rcb = [](int fd, const struct iovec *io, int count) {
		return ::readv(fd, io, count);
	};
	EXPECT_EQ(store.Readv(fd, rcb), -1);

	// ZeroCopyNext should fail in read-only mode
	void *data;
	int len;
	EXPECT_EQ(store.ZeroCopyNext(&data, &len), -1);

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(LogWriteBufTest, ZeroCopyNext)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(), 0, fd, false, false);

	void *data = nullptr;
	int len = 0;

	EXPECT_EQ(store.ZeroCopyNext(&data, &len), 0);
	EXPECT_NE(data, nullptr);
	EXPECT_EQ(len, MemManager::GetPageSize());

	// Write directly to the buffer
	memcpy(data, "hello", 5);

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(LogWriteBufTest, Backup)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(), 0, fd, false, false);

	// First, get a zero-copy buffer and "use" some of it
	void *data = nullptr;
	int len = 0;
	EXPECT_EQ(store.ZeroCopyNext(&data, &len), 0);

	// Backup should work for values <= current position
	EXPECT_TRUE(store.Backup(100));

	// After backup, we should be able to get another buffer
	EXPECT_EQ(store.ZeroCopyNext(&data, &len), 0);

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(LogWriteBufTest, SetPosition)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(), 0, fd, false, false);

	// Write some data first
	EXPECT_EQ(store.Push("initial", 7), 0);

	// Set position to a specific offset
	EXPECT_TRUE(store.SetPosition(0));

	// Should be able to write again
	EXPECT_EQ(store.Push("replaced", 8), 0);

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(LogWriteBufTest, AnonymousMode)
{
	// Anonymous mode doesn't need a file
	LogWriteBuf<MMapAnon> store(MemManager::GetPageSize(), 0, -1, false, true);

	EXPECT_EQ(store.Push("anonymous", 9), 0);

	void *data = nullptr;
	int len = 0;
	EXPECT_EQ(store.ZeroCopyNext(&data, &len), 0);
	EXPECT_NE(data, nullptr);
}

TEST(LogWriteBufTest, AllocateCallback)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(), 0, fd, false, false);

	int callbackCount = 0;
	off64_t lastOffset = -1;
	std::function<void(char*, off64_t)> cb = [&callbackCount, &lastOffset](char* page, off64_t offset) {
		callbackCount++;
		lastOffset = offset;
		EXPECT_NE(page, nullptr);
	};
	store.SetAllocateCB(cb);

	// First push should trigger page allocation
	EXPECT_EQ(store.Push("test", 4), 0);
	EXPECT_EQ(callbackCount, 1);
	EXPECT_EQ(lastOffset, 0);

	// Fill up the first page to trigger another allocation
	size_t pageSize = MemManager::GetPageSize();
	std::vector<char> largeData(pageSize, 'x');
	EXPECT_EQ(store.Push(largeData.data(), pageSize), 0);
	EXPECT_EQ(callbackCount, 2);
	EXPECT_EQ(lastOffset, static_cast<off64_t>(pageSize));

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(LogWriteBufTest, PushAcrossPageBoundary)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	size_t pageSize = MemManager::GetPageSize();
	LogWriteBuf<MMapShared> store(pageSize, 0, fd, false, false);

	// Fill most of the first page
	std::vector<char> data1(pageSize - 10, 'a');
	EXPECT_EQ(store.Push(data1.data(), data1.size()), 0);

	// Push data that spans page boundary
	std::vector<char> data2(100, 'b');
	EXPECT_EQ(store.Push(data2.data(), data2.size()), 0);

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

// ============================================================================
// LogRWStream ZeroCopy Tests
// ============================================================================

TEST(LogRWStreamTest, ZeroCopyWriteNext)
{
	LogRWStream<MMapAnon, OneShotCache, 16> rwBuf(MemManager::GetPageSize(), 0, -1, true);

	void *data = nullptr;
	int len = 0;

	EXPECT_EQ(rwBuf.ZeroCopyWriteNext(&data, &len), 0);
	EXPECT_NE(data, nullptr);
	EXPECT_GT(len, 0);

	// Available should increase
	EXPECT_EQ(rwBuf.Available(), static_cast<uint64_t>(len));
}

TEST(LogRWStreamTest, ZeroCopyWriteBackup)
{
	LogRWStream<MMapAnon, OneShotCache, 16> rwBuf(MemManager::GetPageSize(), 0, -1, true);

	void *data = nullptr;
	int len = 0;

	EXPECT_EQ(rwBuf.ZeroCopyWriteNext(&data, &len), 0);
	uint64_t initialAvailable = rwBuf.Available();

	// Backup some bytes
	rwBuf.ZeroCopyWriteBackup(100);
	EXPECT_EQ(rwBuf.Available(), initialAvailable - 100);
}

TEST(LogRWStreamTest, ZeroCopyReadNext)
{
	LogRWStream<MMapAnon, OneShotCache, 16> rwBuf(MemManager::GetPageSize(), 0, -1, true);

	// First write some data
	EXPECT_EQ(rwBuf.Push("testdata", 8), 0);

	const void *data = nullptr;
	int len = 0;

	EXPECT_TRUE(rwBuf.ZeroCopyReadNext(0, &data, &len));
	EXPECT_NE(data, nullptr);
	EXPECT_GE(len, 8);
	EXPECT_EQ(memcmp(data, "testdata", 8), 0);
}

TEST(LogRWStreamTest, SetPosition)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogRWStream<MMapShared, LRUCache, 16> rwBuf(MemManager::GetPageSize(), 0, fd, false);

	// Write some data
	EXPECT_EQ(rwBuf.Push("firstdata", 9), 0);

	// Set position
	EXPECT_TRUE(rwBuf.SetPosition(0));

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

// ============================================================================
// LRUCache Tests
// ============================================================================

TEST(LRUCacheTest, CacheEviction)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	// Use a small cache size (4 pages) to test eviction
	LogRWStream<MMapShared, LRUCache, 4> rwBuf(MemManager::GetPageSize(), 0, fd, false);

	size_t pageSize = MemManager::GetPageSize();

	// Write data spanning more pages than cache size
	for (int i = 0; i < 8; ++i) {
		std::vector<char> data(pageSize, 'a' + i);
		ASSERT_EQ(rwBuf.Push(data.data(), pageSize), 0) << "Failed at page " << i;
	}

	// Read from different pages - should trigger cache eviction
	for (int i = 0; i < 8; ++i) {
		char d = 0;
		EXPECT_TRUE(rwBuf.Peak(i * pageSize, d)) << "Failed to peak page " << i;
		EXPECT_EQ(d, 'a' + i) << "Wrong data at page " << i;
	}

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(LRUCacheTest, CacheHit)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogRWStream<MMapShared, LRUCache, 16> rwBuf(MemManager::GetPageSize(), 0, fd, false);

	EXPECT_EQ(rwBuf.Push("cached_data", 11), 0);

	// Read same data multiple times - should hit cache
	for (int i = 0; i < 10; ++i) {
		char d = 0;
		EXPECT_TRUE(rwBuf.Peak(0, d));
		EXPECT_EQ(d, 'c');
	}

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

// ============================================================================
// OneShotCache Tests
// ============================================================================

TEST(OneShotCacheTest, PeakPage)
{
	typedef OneShotCache<MMapAnon, 32> cache_type;
	cache_type oneShot(MemManager::GetPageSize(), 0, -1);

	cache_type::read_cache_type page;

	// PeakPage on empty cache
	EXPECT_EQ(oneShot.PeakPage(0, page), -1);

	char *a = reinterpret_cast<char*>(MMapAnon::MMapWrite(-1, 0, MemManager::GetPageSize()));
	ASSERT_NE(a, nullptr);
	oneShot.AddPage(a, 0);

	// PeakPage should find the page without removing it
	EXPECT_EQ(oneShot.PeakPage(0, page), 0);
	EXPECT_EQ(oneShot.PeakPage(0, page), 0);  // Should still be there
}

TEST(OneShotCacheTest, MultiplePages)
{
	typedef OneShotCache<MMapAnon, 32> cache_type;
	size_t pageSize = MemManager::GetPageSize();
	cache_type oneShot(pageSize, 0, -1);

	// Add multiple pages
	std::vector<char*> pages;
	for (int i = 0; i < 5; ++i) {
		char *p = reinterpret_cast<char*>(MMapAnon::MMapWrite(-1, 0, pageSize));
		ASSERT_NE(p, nullptr);
		pages.push_back(p);
		oneShot.AddPage(p, i * pageSize);
	}

	cache_type::read_cache_type page;

	// Find pages in order
	for (int i = 0; i < 5; ++i) {
		EXPECT_EQ(oneShot.FindPage(i * pageSize, page), 0) << "Failed to find page " << i;
	}
}

// ============================================================================
// PositionedStream Tests
// ============================================================================

TEST(PositionedStreamTest, BasicOperations)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	PositionedStream<stream_type> posStream(stream);

	// Initially empty
	EXPECT_EQ(posStream.Available(), 0u);

	// Push data
	EXPECT_EQ(posStream.Push("testdata", 8), 0);

	// Available should now show data
	EXPECT_EQ(posStream.Available(), 8u);
	EXPECT_EQ(posStream.TotalAvailable(), 8u);
}

TEST(PositionedStreamTest, Peak)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	// Write some initial data
	stream->Push("initial", 7);

	PositionedStream<stream_type> posStream(stream);

	// Push more data through positioned stream
	EXPECT_EQ(posStream.Push("newdata", 7), 0);

	// Peak at offset 0 (relative to current position)
	char d = 0;
	EXPECT_TRUE(posStream.Peak(0, d));
	EXPECT_EQ(d, 'n');
}

TEST(PositionedStreamTest, Pop)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	PositionedStream<stream_type> posStream(stream);

	EXPECT_EQ(posStream.Push("abcdefgh", 8), 0);

	char dest[8] = {};
	EXPECT_TRUE(posStream.Pop(dest, 4));
	EXPECT_EQ(memcmp(dest, "abcd", 4), 0);

	// Available should decrease
	EXPECT_EQ(posStream.Available(), 4u);

	// Pop remaining
	EXPECT_TRUE(posStream.Pop(dest, 4));
	EXPECT_EQ(memcmp(dest, "efgh", 4), 0);
	EXPECT_EQ(posStream.Available(), 0u);
}

TEST(PositionedStreamTest, Find)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	PositionedStream<stream_type> posStream(stream);

	EXPECT_EQ(posStream.Push("hello,world", 11), 0);

	uint64_t offset = 0;
	EXPECT_TRUE(posStream.Find(0, ',', offset));
	EXPECT_EQ(offset, 5u);

	EXPECT_FALSE(posStream.Find(0, 'x', offset));
}

TEST(PositionedStreamTest, Skip)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	PositionedStream<stream_type> posStream(stream);

	EXPECT_EQ(posStream.Push("skipthis", 8), 0);

	EXPECT_TRUE(posStream.Skip(4));
	EXPECT_EQ(posStream.Available(), 4u);

	// Cannot skip more than available
	EXPECT_FALSE(posStream.Skip(10));
}

TEST(PositionedStreamTest, Backup)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	PositionedStream<stream_type> posStream(stream);

	EXPECT_EQ(posStream.Push("backuptest", 10), 0);

	// Skip forward
	EXPECT_TRUE(posStream.Skip(5));
	EXPECT_EQ(posStream.Available(), 5u);

	// Backup
	EXPECT_TRUE(posStream.Backup(3));
	EXPECT_EQ(posStream.Available(), 8u);
}

TEST(PositionedStreamTest, ResetPosition)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	PositionedStream<stream_type> posStream(stream);

	EXPECT_EQ(posStream.Push("resettest", 9), 0);

	// Skip some data
	EXPECT_TRUE(posStream.Skip(5));
	EXPECT_EQ(posStream.Available(), 4u);

	// Reset position to end
	posStream.ResetPosition();
	EXPECT_EQ(posStream.Available(), 0u);

	// Push more data
	EXPECT_EQ(posStream.Push("more", 4), 0);
	EXPECT_EQ(posStream.Available(), 4u);
}

TEST(PositionedStreamTest, CurrentOffset)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	PositionedStream<stream_type> posStream(stream);

	uint64_t initialOffset = posStream.CurrentOffset();
	EXPECT_EQ(initialOffset, 0u);

	EXPECT_EQ(posStream.Push("data", 4), 0);
	EXPECT_TRUE(posStream.Skip(2));

	EXPECT_EQ(posStream.CurrentOffset(), 2u);
}

TEST(PositionedStreamTest, Readv)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	PositionedStream<stream_type> posStream(stream);

	int fds[2];
	ASSERT_EQ(pipe(fds), 0);

	std::function<int(int, const struct iovec *, int)> rcb = [](int fd, const struct iovec *io, int count) {
		return ::readv(fd, io, count);
	};

	const char *testData = "pipedata";
	ASSERT_EQ(write(fds[1], testData, 8), 8);

	int r = posStream.Readv(fds[0], rcb);
	EXPECT_EQ(r, 8);

	close(fds[0]);
	close(fds[1]);
}

TEST(PositionedStreamTest, Writev)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	PositionedStream<stream_type> posStream(stream);

	EXPECT_EQ(posStream.Push("writedata", 9), 0);

	int fds[2];
	ASSERT_EQ(pipe(fds), 0);

	std::function<int(int, const struct iovec *, int)> wcb = [](int fd, const struct iovec *io, int count) {
		return ::writev(fd, io, count);
	};

	int r = posStream.Writev(9, fds[1], wcb);
	EXPECT_EQ(r, 9);

	char buf[10] = {};
	ASSERT_EQ(read(fds[0], buf, 9), 9);
	EXPECT_EQ(memcmp(buf, "writedata", 9), 0);

	close(fds[0]);
	close(fds[1]);
}

// ============================================================================
// MultiPositionedStreamLog Tests
// ============================================================================

TEST(MultiPositionedStreamLogTest, BasicOperations)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	MultiPositionedStreamLog<stream_type> multiStream(stream);

	// Push data
	EXPECT_EQ(multiStream.Push("testdata", 8), 0);
	EXPECT_EQ(multiStream.Available(), 8u);
}

TEST(MultiPositionedStreamLogTest, RegisterUnregister)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	MultiPositionedStreamLog<stream_type> multiStream(stream);

	EXPECT_EQ(multiStream.Push("testdata", 8), 0);

	// Register a client at offset 0
	EXPECT_EQ(multiStream.Register(5, 0), 0);

	// Check available for registered client
	EXPECT_EQ(multiStream.Available(5), 8u);
	EXPECT_FALSE(multiStream.IsEmpty(5));

	// Unregister
	EXPECT_EQ(multiStream.Unregister(5), 0);

	// After unregister, should show 0 available
	EXPECT_EQ(multiStream.Available(5), 0u);
	EXPECT_TRUE(multiStream.IsEmpty(5));
}

TEST(MultiPositionedStreamLogTest, MultipleClients)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	MultiPositionedStreamLog<stream_type> multiStream(stream);

	EXPECT_EQ(multiStream.Push("abcdefghij", 10), 0);

	// Register multiple clients at different offsets
	EXPECT_EQ(multiStream.Register(1, 0), 0);
	EXPECT_EQ(multiStream.Register(2, 5), 0);
	EXPECT_EQ(multiStream.Register(3, 10), 0);

	// Each client should see different available amounts
	EXPECT_EQ(multiStream.Available(1), 10u);
	EXPECT_EQ(multiStream.Available(2), 5u);
	EXPECT_EQ(multiStream.Available(3), 0u);

	EXPECT_FALSE(multiStream.IsEmpty(1));
	EXPECT_FALSE(multiStream.IsEmpty(2));
	EXPECT_TRUE(multiStream.IsEmpty(3));
}

TEST(MultiPositionedStreamLogTest, Mark)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	MultiPositionedStreamLog<stream_type> multiStream(stream);

	EXPECT_EQ(multiStream.Push("testdata", 8), 0);

	EXPECT_EQ(multiStream.Register(1, 0), 0);
	EXPECT_EQ(multiStream.Available(1), 8u);

	// Mark at new position
	EXPECT_TRUE(multiStream.Mark(1, 4));
	EXPECT_EQ(multiStream.Available(1), 4u);

	// Mark at end
	EXPECT_TRUE(multiStream.MarkEnd(1));
	EXPECT_EQ(multiStream.Available(1), 0u);
}

TEST(MultiPositionedStreamLogTest, Pop)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	MultiPositionedStreamLog<stream_type> multiStream(stream);

	EXPECT_EQ(multiStream.Push("popdata123", 10), 0);

	char dest[10] = {};
	EXPECT_TRUE(multiStream.Pop(dest, 0, 10));
	EXPECT_EQ(memcmp(dest, "popdata123", 10), 0);

	// Pop with offset
	EXPECT_TRUE(multiStream.Pop(dest, 3, 4));
	EXPECT_EQ(memcmp(dest, "data", 4), 0);
}

TEST(MultiPositionedStreamLogTest, Writev)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	MultiPositionedStreamLog<stream_type> multiStream(stream);

	EXPECT_EQ(multiStream.Push("writevtest", 10), 0);

	int fds[2];
	ASSERT_EQ(pipe(fds), 0);

	// Register client
	EXPECT_EQ(multiStream.Register(fds[1], 0), 0);

	std::function<int(int, const struct iovec *, int)> wcb = [](int fd, const struct iovec *io, int count) {
		return ::writev(fd, io, count);
	};

	int r = multiStream.Writev(10, fds[1], wcb);
	EXPECT_EQ(r, 10);

	// After writev, position should advance
	EXPECT_EQ(multiStream.Available(fds[1]), 0u);

	char buf[11] = {};
	ASSERT_EQ(read(fds[0], buf, 10), 10);
	EXPECT_EQ(memcmp(buf, "writevtest", 10), 0);

	close(fds[0]);
	close(fds[1]);
}

TEST(MultiPositionedStreamLogTest, ZeroCopyWrite)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	MultiPositionedStreamLog<stream_type> multiStream(stream);

	void *data = nullptr;
	int len = 0;

	EXPECT_EQ(multiStream.ZeroCopyWriteNext(&data, &len), 0);
	EXPECT_NE(data, nullptr);
	EXPECT_GT(len, 0);

	// Write some data
	memcpy(data, "zerocopy", 8);

	// Backup unused bytes
	multiStream.ZeroCopyWriteBackup(len - 8);

	EXPECT_EQ(multiStream.Available(), 8u);
}

TEST(MultiPositionedStreamLogTest, Iterator)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	MultiPositionedStreamLog<stream_type> multiStream(stream);

	EXPECT_EQ(multiStream.Push("iterate", 7), 0);

	auto b = multiStream.begin(0);
	auto e = multiStream.end(7);

	std::string result;
	for (; b != e; ++b) {
		result += *b;
	}

	EXPECT_EQ(result, "iterate");
}

TEST(MultiPositionedStreamLogTest, UnregisteredClient)
{
	typedef LogRWStream<MMapAnon, OneShotCache, 16> stream_type;
	auto stream = std::make_shared<stream_type>(MemManager::GetPageSize(), 0, -1, true);

	MultiPositionedStreamLog<stream_type> multiStream(stream);

	// Operations on unregistered client should handle gracefully
	EXPECT_EQ(multiStream.Available(999), 0u);
	EXPECT_TRUE(multiStream.IsEmpty(999));

	std::function<int(int, const struct iovec *, int)> wcb = [](int fd, const struct iovec *io, int count) {
		return ::writev(fd, io, count);
	};

	// Writev on unregistered should return error or no-op
	EXPECT_EQ(multiStream.Writev(10, 999, wcb), -3);
}

// ============================================================================
// logstreambuf Tests
// ============================================================================

TEST(LogStreamBufTest, Xsputn)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	typedef LogWriteBuf<MMapShared> buf_type;
	auto sp = std::make_shared<buf_type>(MemManager::GetPageSize(), 0, fd, false, false);

	logstreambuf<buf_type> csb(sp);

	// Use xsputn via sputn
	const char *data = "streambuf_test";
	std::streamsize written = csb.sputn(data, 14);
	EXPECT_EQ(written, 0);  // Returns result of Push (0 = success)

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(LogStreamBufTest, Overflow)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	typedef LogWriteBuf<MMapShared> buf_type;
	auto sp = std::make_shared<buf_type>(MemManager::GetPageSize(), 0, fd, false, false);

	logstreambuf<buf_type> csb(sp);

	// Test overflow with single character
	auto result = csb.sputc('X');
	EXPECT_NE(result, std::char_traits<char>::eof());

	// Test overflow with EOF
	// Note: EOF handling is implementation-specific

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(LogStreamBufTest, WithOstream)
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	typedef LogWriteBuf<MMapShared> buf_type;
	auto sp = std::make_shared<buf_type>(MemManager::GetPageSize(), 0, fd, false, false);

	logstreambuf<buf_type> csb(sp);
	std::ostream os(&csb);

	os << "hello";
	os << " world";
	os << 123;

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST(StoreEdgeCaseTest, EmptyPush)
{
	LogRWStream<MMapAnon, OneShotCache, 16> rwBuf(MemManager::GetPageSize(), 0, -1, true);

	// Push empty data
	EXPECT_EQ(rwBuf.Push("", 0), 0);
	EXPECT_EQ(rwBuf.Available(), 0u);
}

TEST(StoreEdgeCaseTest, LargeDataAcrossPages)
{
	LogRWStream<MMapAnon, OneShotCache, 16> rwBuf(MemManager::GetPageSize(), 0, -1, true);

	size_t pageSize = MemManager::GetPageSize();
	size_t dataSize = pageSize * 3 + 100;  // Spans multiple pages

	std::vector<char> largeData(dataSize, 'L');
	EXPECT_EQ(rwBuf.Push(largeData.data(), dataSize), 0);
	EXPECT_EQ(rwBuf.Available(), dataSize);

	// Read back and verify
	std::vector<char> readBack(dataSize);
	EXPECT_TRUE(rwBuf.Pop(0, readBack.data(), dataSize));
	EXPECT_EQ(memcmp(largeData.data(), readBack.data(), dataSize), 0);
}

TEST(StoreEdgeCaseTest, FindAtPageBoundary)
{
	LogRWStream<MMapAnon, OneShotCache, 16> rwBuf(MemManager::GetPageSize(), 0, -1, true);

	size_t pageSize = MemManager::GetPageSize();

	// Fill first page
	std::vector<char> page1(pageSize, 'a');
	EXPECT_EQ(rwBuf.Push(page1.data(), pageSize), 0);

	// Put marker at start of second page
	EXPECT_EQ(rwBuf.Push("X", 1), 0);

	uint64_t offset = 0;
	EXPECT_TRUE(rwBuf.Find(0, 'X', offset));
	EXPECT_EQ(offset, pageSize);
}

TEST(StoreEdgeCaseTest, PopAtPageBoundary)
{
	LogRWStream<MMapAnon, OneShotCache, 16> rwBuf(MemManager::GetPageSize(), 0, -1, true);

	size_t pageSize = MemManager::GetPageSize();

	// Write data spanning page boundary
	std::vector<char> data(pageSize + 10, 'B');
	// Mark the boundary
	data[pageSize - 1] = 'X';
	data[pageSize] = 'Y';

	EXPECT_EQ(rwBuf.Push(data.data(), data.size()), 0);

	// Pop across boundary
	std::vector<char> result(20);
	EXPECT_TRUE(rwBuf.Pop(pageSize - 5, result.data(), 10));

	EXPECT_EQ(result[4], 'X');
	EXPECT_EQ(result[5], 'Y');
}

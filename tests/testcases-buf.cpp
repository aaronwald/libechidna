#include <memory>
#include <cstring>

#include "gtest/gtest.h"
#include "echidna/buf.hpp"

using namespace coypu::buf;

class BipBufTest : public ::testing::Test {
protected:
	static constexpr size_t BUF_SIZE = 16;
	char data[BUF_SIZE];

	void SetUp() override {
		memset(data, 0, BUF_SIZE);
	}
};

TEST_F(BipBufTest, InitialState) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	EXPECT_EQ(buf.Capacity(), BUF_SIZE);
	EXPECT_EQ(buf.Available(), 0u);
	EXPECT_EQ(buf.Free(), BUF_SIZE);
	EXPECT_TRUE(buf.IsEmpty());
	EXPECT_EQ(buf.Head(), 0u);
	EXPECT_EQ(buf.Tail(), 0u);
}

TEST_F(BipBufTest, PushSingleByte) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	EXPECT_TRUE(buf.Push('A'));
	EXPECT_EQ(buf.Available(), 1u);
	EXPECT_EQ(buf.Free(), BUF_SIZE - 1);
	EXPECT_FALSE(buf.IsEmpty());
}

TEST_F(BipBufTest, PushMultipleBytes) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	const char* input = "Hello";
	EXPECT_TRUE(buf.Push(input, 5));
	EXPECT_EQ(buf.Available(), 5u);
	EXPECT_EQ(buf.Free(), BUF_SIZE - 5);
}

TEST_F(BipBufTest, PushExceedsCapacity) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	char largeData[BUF_SIZE + 1];
	memset(largeData, 'X', sizeof(largeData));

	EXPECT_FALSE(buf.Push(largeData, BUF_SIZE + 1));
	EXPECT_TRUE(buf.IsEmpty());
}

TEST_F(BipBufTest, PushFillsBuffer) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	char fillData[BUF_SIZE];
	memset(fillData, 'X', BUF_SIZE);

	EXPECT_TRUE(buf.Push(fillData, BUF_SIZE));
	EXPECT_EQ(buf.Available(), BUF_SIZE);
	EXPECT_EQ(buf.Free(), 0u);

	// Buffer is full, cannot push more
	EXPECT_FALSE(buf.Push('Y'));
}

TEST_F(BipBufTest, PopSingleElement) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push('A');
	buf.Push('B');
	buf.Push('C');

	char out[3];
	EXPECT_TRUE(buf.Pop(out, 1));
	EXPECT_EQ(out[0], 'A');
	EXPECT_EQ(buf.Available(), 2u);
}

TEST_F(BipBufTest, PopMultipleElements) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	const char* input = "ABCDE";
	buf.Push(input, 5);

	char out[5];
	EXPECT_TRUE(buf.Pop(out, 5));
	EXPECT_EQ(memcmp(out, "ABCDE", 5), 0);
	EXPECT_TRUE(buf.IsEmpty());
}

TEST_F(BipBufTest, PopMoreThanAvailable) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABC", 3);

	char out[5];
	EXPECT_FALSE(buf.Pop(out, 5));
	EXPECT_EQ(buf.Available(), 3u);  // Data unchanged
}

TEST_F(BipBufTest, PopWithPeakFlag) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABC", 3);

	char out[3];
	EXPECT_TRUE(buf.Pop(out, 3, true));  // peak=true
	EXPECT_EQ(memcmp(out, "ABC", 3), 0);
	EXPECT_EQ(buf.Available(), 3u);  // Data still available
}

TEST_F(BipBufTest, PeakSingleElement) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABCDE", 5);

	char d;
	EXPECT_TRUE(buf.Peak(0, d));
	EXPECT_EQ(d, 'A');
	EXPECT_TRUE(buf.Peak(2, d));
	EXPECT_EQ(d, 'C');
	EXPECT_TRUE(buf.Peak(4, d));
	EXPECT_EQ(d, 'E');
	EXPECT_FALSE(buf.Peak(5, d));  // Out of bounds
}

TEST_F(BipBufTest, FindElement) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("Hello", 5);

	size_t offset;
	EXPECT_TRUE(buf.Find('e', offset));
	EXPECT_EQ(offset, 1u);

	EXPECT_TRUE(buf.Find('o', offset));
	EXPECT_EQ(offset, 4u);

	EXPECT_FALSE(buf.Find('X', offset));
}

TEST_F(BipBufTest, FindInEmptyBuffer) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	size_t offset;
	EXPECT_FALSE(buf.Find('A', offset));
}

TEST_F(BipBufTest, SkipElements) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABCDE", 5);

	EXPECT_TRUE(buf.Skip(2));
	EXPECT_EQ(buf.Available(), 3u);

	char out[3];
	EXPECT_TRUE(buf.Pop(out, 3));
	EXPECT_EQ(memcmp(out, "CDE", 3), 0);
}

TEST_F(BipBufTest, SkipMoreThanAvailable) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABC", 3);

	EXPECT_FALSE(buf.Skip(5));
	EXPECT_EQ(buf.Available(), 3u);  // Unchanged
}

TEST_F(BipBufTest, SkipZero) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABC", 3);
	EXPECT_FALSE(buf.Skip(0));
}

TEST_F(BipBufTest, WrapAroundPushPop) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	// Fill most of the buffer
	char fillData[12];
	memset(fillData, 'A', 12);
	EXPECT_TRUE(buf.Push(fillData, 12));

	// Pop some to move tail forward
	char out[8];
	EXPECT_TRUE(buf.Pop(out, 8));
	EXPECT_EQ(buf.Available(), 4u);

	// Push more data, causing wrap-around
	char moreData[10];
	memset(moreData, 'B', 10);
	EXPECT_TRUE(buf.Push(moreData, 10));
	EXPECT_EQ(buf.Available(), 14u);

	// Pop all and verify integrity
	char result[14];
	EXPECT_TRUE(buf.Pop(result, 14));

	// First 4 should be 'A', next 10 should be 'B'
	for (int i = 0; i < 4; i++) {
		EXPECT_EQ(result[i], 'A') << "Index " << i;
	}
	for (int i = 4; i < 14; i++) {
		EXPECT_EQ(result[i], 'B') << "Index " << i;
	}
}

TEST_F(BipBufTest, FindWithWrapAround) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	// Create wrap-around scenario
	char fillData[12];
	memset(fillData, 'A', 12);
	buf.Push(fillData, 12);

	char out[10];
	buf.Pop(out, 10);

	// Push data that wraps around, with unique char at end
	buf.Push("BCDEFGHX", 8);

	size_t offset;
	EXPECT_TRUE(buf.Find('X', offset));
}

TEST_F(BipBufTest, PeakWithWrapAround) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	// Create wrap-around scenario
	char fillData[14];
	memset(fillData, 'A', 14);
	buf.Push(fillData, 14);

	char out[12];
	buf.Pop(out, 12);

	// Push data that wraps: "12345678" (8 bytes, wraps around)
	buf.Push("12345678", 8);

	char d;
	EXPECT_TRUE(buf.Peak(0, d));
	EXPECT_EQ(d, 'A');
	EXPECT_TRUE(buf.Peak(1, d));
	EXPECT_EQ(d, 'A');
	EXPECT_TRUE(buf.Peak(2, d));
	EXPECT_EQ(d, '1');
}

TEST_F(BipBufTest, DirectAccess) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABCDE", 5);

	const void* ptr;
	size_t len;
	EXPECT_TRUE(buf.Direct(&ptr, &len));
	EXPECT_EQ(len, 5u);
	EXPECT_EQ(memcmp(ptr, "ABCDE", 5), 0);
	EXPECT_TRUE(buf.IsEmpty());  // Direct consumes data
}

TEST_F(BipBufTest, DirectOnEmpty) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	const void* ptr;
	size_t len;
	EXPECT_FALSE(buf.Direct(&ptr, &len));
}

TEST_F(BipBufTest, BackupDirect) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABCDE", 5);

	EXPECT_TRUE(buf.BackupDirect(2));
	EXPECT_EQ(buf.Available(), 3u);

	char out[3];
	EXPECT_TRUE(buf.Pop(out, 3));
	EXPECT_EQ(memcmp(out, "ABC", 3), 0);
}

TEST_F(BipBufTest, BackupDirectMoreThanAvailable) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABC", 3);

	EXPECT_FALSE(buf.BackupDirect(5));
	EXPECT_EQ(buf.Available(), 3u);
}

TEST_F(BipBufTest, BackupDirectZero) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABC", 3);
	EXPECT_TRUE(buf.BackupDirect(0));
	EXPECT_EQ(buf.Available(), 3u);
}

TEST_F(BipBufTest, Backup) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABCDE", 5);

	char out[2];
	buf.Pop(out, 2);
	EXPECT_EQ(buf.Available(), 3u);

	// Backup (move tail backwards)
	EXPECT_TRUE(buf.Backup(1));
	EXPECT_EQ(buf.Available(), 4u);
}

TEST_F(BipBufTest, PopAllWithCallback) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABCDE", 5);

	std::string collected;
	auto cb = [&collected](const char* data, size_t len) -> bool {
		collected.append(data, len);
		return true;
	};

	EXPECT_TRUE(buf.PopAll(cb, 5));
	EXPECT_EQ(collected, "ABCDE");
	EXPECT_TRUE(buf.IsEmpty());
}

TEST_F(BipBufTest, PopAllCallbackReturnsFalse) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABCDE", 5);

	auto cb = [](const char*, size_t) -> bool {
		return false;  // Simulate failure
	};

	EXPECT_FALSE(buf.PopAll(cb, 5));
}

TEST_F(BipBufTest, PushNullPointer) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	EXPECT_FALSE(buf.Push(nullptr, 5));
}

TEST_F(BipBufTest, PushZeroSize) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	EXPECT_FALSE(buf.Push("ABC", 0));
	EXPECT_TRUE(buf.IsEmpty());
}

TEST_F(BipBufTest, PopZeroSize) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("ABC", 3);

	char out[1];
	EXPECT_FALSE(buf.Pop(out, 0));
}

TEST_F(BipBufTest, ReadWithCallback) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	int fds[2];
	ASSERT_EQ(pipe(fds), 0);

	const char* testData = "TestData";
	ASSERT_EQ(write(fds[1], testData, 8), 8);

	auto readCb = [](int fd, void* buf, size_t len) -> int {
		return read(fd, buf, len);
	};

	int result = buf.Read(fds[0], readCb);
	EXPECT_EQ(result, 8);
	EXPECT_EQ(buf.Available(), 8u);

	char out[8];
	buf.Pop(out, 8);
	EXPECT_EQ(memcmp(out, "TestData", 8), 0);

	close(fds[0]);
	close(fds[1]);
}

TEST_F(BipBufTest, WriteWithCallback) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("WriteTest", 9);

	int fds[2];
	ASSERT_EQ(pipe(fds), 0);

	auto writeCb = [](int fd, void* buf, size_t len) -> int {
		return write(fd, buf, len);
	};

	int result = buf.Write(fds[1], writeCb);
	EXPECT_EQ(result, 9);
	EXPECT_TRUE(buf.IsEmpty());

	char out[9];
	ASSERT_EQ(read(fds[0], out, 9), 9);
	EXPECT_EQ(memcmp(out, "WriteTest", 9), 0);

	close(fds[0]);
	close(fds[1]);
}

TEST_F(BipBufTest, WriteOnEmpty) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	auto writeCb = [](int, void*, size_t) -> int {
		return 0;
	};

	EXPECT_EQ(buf.Write(0, writeCb), -2);
}

TEST_F(BipBufTest, ReadOnFull) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	char fillData[BUF_SIZE];
	memset(fillData, 'X', BUF_SIZE);
	buf.Push(fillData, BUF_SIZE);

	auto readCb = [](int, void*, size_t) -> int {
		return 0;
	};

	EXPECT_EQ(buf.Read(0, readCb), -2);
}

TEST_F(BipBufTest, CurrentOffset) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	EXPECT_EQ(buf.CurrentOffset(), 0u);

	buf.Push("ABC", 3);
	EXPECT_EQ(buf.CurrentOffset(), 3u);

	buf.Push("DE", 2);
	EXPECT_EQ(buf.CurrentOffset(), 5u);
}

TEST_F(BipBufTest, ReadvWithCallback) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	int fds[2];
	ASSERT_EQ(pipe(fds), 0);

	const char* testData = "ReadvTest";
	ASSERT_EQ(write(fds[1], testData, 9), 9);

	auto readvCb = [](int fd, const struct iovec* iov, int count) -> int {
		return readv(fd, iov, count);
	};

	int result = buf.Readv(fds[0], readvCb);
	EXPECT_EQ(result, 9);
	EXPECT_EQ(buf.Available(), 9u);

	close(fds[0]);
	close(fds[1]);
}

TEST_F(BipBufTest, WritevWithCallback) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	buf.Push("WritevTst", 9);

	int fds[2];
	ASSERT_EQ(pipe(fds), 0);

	auto writevCb = [](int fd, const struct iovec* iov, int count) -> int {
		return writev(fd, iov, count);
	};

	int result = buf.Writev(fds[1], writevCb);
	EXPECT_EQ(result, 9);
	EXPECT_TRUE(buf.IsEmpty());

	char out[9];
	ASSERT_EQ(read(fds[0], out, 9), 9);
	EXPECT_EQ(memcmp(out, "WritevTst", 9), 0);

	close(fds[0]);
	close(fds[1]);
}

TEST_F(BipBufTest, WritevOnEmpty) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	auto writevCb = [](int, const struct iovec*, int) -> int {
		return 0;
	};

	EXPECT_EQ(buf.Writev(0, writevCb), -2);
}

TEST_F(BipBufTest, ReadvOnFull) {
	BipBuf<char, size_t> buf(data, BUF_SIZE);

	char fillData[BUF_SIZE];
	memset(fillData, 'X', BUF_SIZE);
	buf.Push(fillData, BUF_SIZE);

	auto readvCb = [](int, const struct iovec*, int) -> int {
		return 0;
	};

	EXPECT_EQ(buf.Readv(0, readvCb), -2);
}

// Test with different template types
TEST(BipBufTypesTest, Uint8WithUint16Capacity) {
	uint8_t data[256];
	BipBuf<uint8_t, uint16_t> buf(data, 256);

	EXPECT_EQ(buf.Capacity(), 256u);
	EXPECT_TRUE(buf.IsEmpty());

	uint8_t input[] = {0x01, 0x02, 0x03, 0x04};
	EXPECT_TRUE(buf.Push(input, 4));
	EXPECT_EQ(buf.Available(), 4u);

	// Test wrap-around with uint16_t capacity
	uint8_t fill[250];
	memset(fill, 0xAA, 250);
	EXPECT_TRUE(buf.Push(fill, 250));

	uint8_t out[200];
	EXPECT_TRUE(buf.Pop(out, 200));

	// Push more to wrap around
	uint8_t more[150];
	memset(more, 0xBB, 150);
	EXPECT_TRUE(buf.Push(more, 150));
}

TEST(BipBufTypesTest, IntBuffer) {
	int data[32];
	BipBuf<int, size_t> buf(data, 32);

	int values[] = {100, 200, 300};
	EXPECT_TRUE(buf.Push(values, 3));
	EXPECT_EQ(buf.Available(), 3u);

	int out[3];
	EXPECT_TRUE(buf.Pop(out, 3));
	EXPECT_EQ(out[0], 100);
	EXPECT_EQ(out[1], 200);
	EXPECT_EQ(out[2], 300);
}

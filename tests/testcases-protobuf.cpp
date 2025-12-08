#include <memory>
#include <cstring>
#include <arpa/inet.h>

#include "gtest/gtest.h"
#include "echidna/protomgr.hpp"
#include "echidna/buf.hpp"

using namespace coypu::protobuf;
using namespace coypu::buf;

// ============================================================================
// BufZeroCopyOutputStream Tests
// ============================================================================

class ZeroCopyOutputStreamTest : public ::testing::Test {
protected:
	static constexpr size_t BUF_SIZE = 1024;
	char data[BUF_SIZE];
	std::shared_ptr<BipBuf<char, int>> buf;

	void SetUp() override {
		memset(data, 0, BUF_SIZE);
		buf = std::make_shared<BipBuf<char, int>>(data, BUF_SIZE);
	}
};

TEST_F(ZeroCopyOutputStreamTest, InitialByteCount)
{
	BufZeroCopyOutputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);
	EXPECT_EQ(stream.ByteCount(), 0);
}

TEST_F(ZeroCopyOutputStreamTest, NextReturnsBuffer)
{
	BufZeroCopyOutputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	void *outData = nullptr;
	int size = 0;

	EXPECT_TRUE(stream.Next(&outData, &size));
	EXPECT_NE(outData, nullptr);
	EXPECT_GT(size, 0);
	EXPECT_EQ(stream.ByteCount(), size);
}

TEST_F(ZeroCopyOutputStreamTest, WriteToBuffer)
{
	BufZeroCopyOutputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	void *outData = nullptr;
	int size = 0;

	EXPECT_TRUE(stream.Next(&outData, &size));

	// Write some data
	const char *testData = "Hello, Protobuf!";
	size_t testLen = strlen(testData);
	memcpy(outData, testData, testLen);

	// Backup unused space
	stream.BackUp(size - testLen);

	EXPECT_EQ(stream.ByteCount(), static_cast<int64_t>(testLen));
	EXPECT_EQ(buf->Available(), static_cast<int>(testLen));
}

TEST_F(ZeroCopyOutputStreamTest, BackUpReducesByteCount)
{
	BufZeroCopyOutputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	void *outData = nullptr;
	int size = 0;

	EXPECT_TRUE(stream.Next(&outData, &size));
	int64_t initialCount = stream.ByteCount();

	stream.BackUp(100);
	EXPECT_EQ(stream.ByteCount(), initialCount - 100);
}

TEST_F(ZeroCopyOutputStreamTest, MultipleNextCalls)
{
	BufZeroCopyOutputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	int64_t totalBytes = 0;

	for (int i = 0; i < 5; ++i) {
		void *outData = nullptr;
		int size = 0;

		if (stream.Next(&outData, &size)) {
			totalBytes += size;
			// Use only part of the buffer
			stream.BackUp(size - 10);
			totalBytes -= (size - 10);
		}
	}

	EXPECT_EQ(stream.ByteCount(), totalBytes);
}

TEST_F(ZeroCopyOutputStreamTest, AllowsAliasingReturnsFalse)
{
	BufZeroCopyOutputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);
	EXPECT_FALSE(stream.AllowsAliasing());
}

TEST_F(ZeroCopyOutputStreamTest, WriteAliasedRawReturnsFalse)
{
	BufZeroCopyOutputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);
	char testData[] = "test";
	EXPECT_FALSE(stream.WriteAliasedRaw(testData, 4));
}

// ============================================================================
// BufZeroCopyInputStream Tests
// ============================================================================

class ZeroCopyInputStreamTest : public ::testing::Test {
protected:
	static constexpr size_t BUF_SIZE = 1024;
	char data[BUF_SIZE];
	std::shared_ptr<BipBuf<char, int>> buf;

	void SetUp() override {
		memset(data, 0, BUF_SIZE);
		buf = std::make_shared<BipBuf<char, int>>(data, BUF_SIZE);
	}
};

TEST_F(ZeroCopyInputStreamTest, InitialByteCount)
{
	BufZeroCopyInputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);
	EXPECT_EQ(stream.ByteCount(), 0);
}

TEST_F(ZeroCopyInputStreamTest, NextOnEmptyBufferReturnsFalse)
{
	BufZeroCopyInputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	const void *outData = nullptr;
	int size = 0;

	EXPECT_FALSE(stream.Next(&outData, &size));
	EXPECT_EQ(stream.ByteCount(), 0);
}

TEST_F(ZeroCopyInputStreamTest, NextReturnsData)
{
	// First write some data to the buffer
	const char *testData = "Test input data";
	size_t testLen = strlen(testData);
	buf->Push(testData, testLen);

	BufZeroCopyInputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	const void *outData = nullptr;
	int size = 0;

	EXPECT_TRUE(stream.Next(&outData, &size));
	EXPECT_NE(outData, nullptr);
	EXPECT_EQ(size, static_cast<int>(testLen));
	EXPECT_EQ(memcmp(outData, testData, testLen), 0);
	EXPECT_EQ(stream.ByteCount(), static_cast<int64_t>(testLen));
}

TEST_F(ZeroCopyInputStreamTest, BackUpRestoresData)
{
	const char *testData = "Backup test data";
	size_t testLen = strlen(testData);
	buf->Push(testData, testLen);

	BufZeroCopyInputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	const void *outData = nullptr;
	int size = 0;

	EXPECT_TRUE(stream.Next(&outData, &size));
	int64_t afterNext = stream.ByteCount();

	stream.BackUp(5);
	EXPECT_EQ(stream.ByteCount(), afterNext - 5);
}

TEST_F(ZeroCopyInputStreamTest, Skip)
{
	const char *testData = "Skip this data please";
	size_t testLen = strlen(testData);
	buf->Push(testData, testLen);

	BufZeroCopyInputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	EXPECT_TRUE(stream.Skip(5));
	EXPECT_EQ(stream.ByteCount(), 5);

	// Read remaining
	const void *outData = nullptr;
	int size = 0;
	EXPECT_TRUE(stream.Next(&outData, &size));
	EXPECT_EQ(size, static_cast<int>(testLen - 5));
}

TEST_F(ZeroCopyInputStreamTest, SkipMoreThanAvailable)
{
	const char *testData = "Short";
	buf->Push(testData, 5);

	BufZeroCopyInputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	EXPECT_FALSE(stream.Skip(100));
}

TEST_F(ZeroCopyInputStreamTest, SkipZero)
{
	const char *testData = "Test";
	buf->Push(testData, 4);

	BufZeroCopyInputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	// Skip(0) behavior depends on BipBuf implementation
	// Just verify it doesn't crash
	stream.Skip(0);
	EXPECT_EQ(stream.ByteCount(), 0);
}

// ============================================================================
// Integration Tests with Protobuf CodedStream
// ============================================================================

TEST_F(ZeroCopyOutputStreamTest, CodedOutputStreamWrite)
{
	BufZeroCopyOutputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	{
		google::protobuf::io::CodedOutputStream coded(&stream);

		// Write some varints
		coded.WriteVarint32(42);
		coded.WriteVarint32(12345);
		coded.WriteVarint64(9876543210ULL);

		// Write a string
		coded.WriteString("Hello");
	}

	EXPECT_GT(stream.ByteCount(), 0);
	EXPECT_GT(buf->Available(), 0);
}

TEST_F(ZeroCopyInputStreamTest, CodedInputStreamRead)
{
	// First write data using CodedOutputStream
	{
		BufZeroCopyOutputStream<std::shared_ptr<BipBuf<char, int>>> outStream(buf);
		google::protobuf::io::CodedOutputStream coded(&outStream);

		coded.WriteVarint32(42);
		coded.WriteVarint32(12345);
		coded.WriteVarint64(9876543210ULL);
	}

	// Create a new buffer with the same data for reading
	char readData[BUF_SIZE];
	memcpy(readData, data, BUF_SIZE);
	auto readBuf = std::make_shared<BipBuf<char, int>>(readData, BUF_SIZE);

	// Copy the available bytes
	int available = buf->Available();
	char temp[BUF_SIZE];
	buf->Pop(temp, available, true); // peak
	readBuf->Push(temp, available);

	BufZeroCopyInputStream<std::shared_ptr<BipBuf<char, int>>> inStream(readBuf);
	google::protobuf::io::CodedInputStream coded(&inStream);

	uint32_t val32_1, val32_2;
	uint64_t val64;

	EXPECT_TRUE(coded.ReadVarint32(&val32_1));
	EXPECT_EQ(val32_1, 42u);

	EXPECT_TRUE(coded.ReadVarint32(&val32_2));
	EXPECT_EQ(val32_2, 12345u);

	EXPECT_TRUE(coded.ReadVarint64(&val64));
	EXPECT_EQ(val64, 9876543210ULL);
}

TEST_F(ZeroCopyOutputStreamTest, CodedOutputStreamWithBackup)
{
	BufZeroCopyOutputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	{
		google::protobuf::io::CodedOutputStream coded(&stream);

		// Write a fixed size value
		coded.WriteLittleEndian32(0xDEADBEEF);
		coded.WriteLittleEndian64(0x123456789ABCDEF0ULL);
	}

	// Verify the byte count matches expected sizes
	// 4 bytes for uint32 + 8 bytes for uint64 = 12 bytes
	EXPECT_EQ(buf->Available(), 12);
}

// ============================================================================
// ProtoManager Tests (without actual protobuf messages)
// ============================================================================

// Mock logger for testing
struct MockLogger {
	void info(const char*, ...) {}
	void error(const char*, ...) {}
	void debug(const char*, ...) {}
};

// Simple mock message classes for testing ProtoManager structure
class MockRequest {
public:
	bool MergeFromCodedStream(google::protobuf::io::CodedInputStream*) { return true; }
	void Clear() {}
};

class MockResponse {
public:
	int ByteSize() const { return 10; }
	bool SerializeToCodedStream(google::protobuf::io::CodedOutputStream* stream) const {
		// Write 10 bytes
		for (int i = 0; i < 10; ++i) {
			stream->WriteRaw("x", 1);
		}
		return true;
	}
};

TEST(ProtoManagerTest, RegisterUnregister)
{
	MockLogger logger;
	auto setWrite = [](int) { return 0; };

	ProtoManager<MockLogger, MockRequest, MockResponse> mgr(logger, setWrite);

	std::function<int(int, const struct iovec*, int)> readv = [](int, const struct iovec*, int) { return 0; };
	std::function<int(int, const struct iovec*, int)> writev = [](int, const struct iovec*, int) { return 0; };

	// Register
	EXPECT_TRUE(mgr.Register(5, readv, writev));

	// Duplicate register should fail
	EXPECT_FALSE(mgr.Register(5, readv, writev));

	// Register another
	EXPECT_TRUE(mgr.Register(6, readv, writev));

	// Unregister
	EXPECT_EQ(mgr.Unregister(5), 0);

	// Unregister non-existent
	EXPECT_EQ(mgr.Unregister(999), -1);

	// Re-register after unregister
	EXPECT_TRUE(mgr.Register(5, readv, writev));
}

TEST(ProtoManagerTest, ReadUnregistered)
{
	MockLogger logger;
	auto setWrite = [](int) { return 0; };

	ProtoManager<MockLogger, MockRequest, MockResponse> mgr(logger, setWrite);

	EXPECT_EQ(mgr.Read(999), -1);
}

TEST(ProtoManagerTest, WriteUnregistered)
{
	MockLogger logger;
	auto setWrite = [](int) { return 0; };

	ProtoManager<MockLogger, MockRequest, MockResponse> mgr(logger, setWrite);

	EXPECT_EQ(mgr.Write(999), -1);
}

TEST(ProtoManagerTest, WriteResponseUnregistered)
{
	MockLogger logger;
	auto setWrite = [](int) { return 0; };

	ProtoManager<MockLogger, MockRequest, MockResponse> mgr(logger, setWrite);

	MockResponse resp;
	EXPECT_EQ(mgr.WriteResponse(999, resp), -1);
}

TEST(ProtoManagerTest, SetCallback)
{
	MockLogger logger;
	auto setWrite = [](int) { return 0; };

	ProtoManager<MockLogger, MockRequest, MockResponse> mgr(logger, setWrite);

	bool callbackCalled = false;
	std::function<void(int, MockRequest&)> cb = [&callbackCalled](int, MockRequest&) {
		callbackCalled = true;
	};

	mgr.SetCallback(cb);
	// Callback is stored - we can't easily test it's called without a full message
}

TEST(ProtoManagerTest, WriteResponseRegistered)
{
	MockLogger logger;
	bool writeSet = false;
	auto setWrite = [&writeSet](int) {
		writeSet = true;
		return 0;
	};

	ProtoManager<MockLogger, MockRequest, MockResponse> mgr(logger, setWrite);

	std::function<int(int, const struct iovec*, int)> readv = [](int, const struct iovec*, int) { return 0; };
	std::function<int(int, const struct iovec*, int)> writev = [](int, const struct iovec*, int) { return 0; };

	EXPECT_TRUE(mgr.Register(5, readv, writev));

	MockResponse resp;
	EXPECT_EQ(mgr.WriteResponse(5, resp), 0);
	EXPECT_TRUE(writeSet);
}

TEST(ProtoManagerTest, WriteAfterWriteResponse)
{
	MockLogger logger;
	auto setWrite = [](int) { return 0; };

	ProtoManager<MockLogger, MockRequest, MockResponse> mgr(logger, setWrite);

	int writevCalls = 0;
	std::function<int(int, const struct iovec*, int)> readv = [](int, const struct iovec*, int) { return 0; };
	std::function<int(int, const struct iovec*, int)> writev = [&writevCalls](int, const struct iovec* iov, int count) {
		writevCalls++;
		int total = 0;
		for (int i = 0; i < count; ++i) {
			total += iov[i].iov_len;
		}
		return total;
	};

	EXPECT_TRUE(mgr.Register(5, readv, writev));

	MockResponse resp;
	EXPECT_EQ(mgr.WriteResponse(5, resp), 0);

	// Write should drain the buffer
	int result = mgr.Write(5);
	EXPECT_GE(result, 0);
	EXPECT_GT(writevCalls, 0);
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST(ProtobufStressTest, ManySmallWrites)
{
	char data[64 * 1024];
	auto buf = std::make_shared<BipBuf<char, int>>(data, sizeof(data));

	BufZeroCopyOutputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	{
		google::protobuf::io::CodedOutputStream coded(&stream);

		for (int i = 0; i < 1000; ++i) {
			coded.WriteVarint32(i);
		}
	}

	EXPECT_GT(buf->Available(), 0);
}

TEST(ProtobufStressTest, LargeWrite)
{
	char data[256 * 1024];
	auto buf = std::make_shared<BipBuf<char, int>>(data, sizeof(data));

	BufZeroCopyOutputStream<std::shared_ptr<BipBuf<char, int>>> stream(buf);

	{
		google::protobuf::io::CodedOutputStream coded(&stream);

		// Write a large string
		std::string largeString(100000, 'X');
		coded.WriteString(largeString);
	}

	// String data (100000) + length prefix varint (3 bytes for 100000)
	EXPECT_GE(buf->Available(), 100000);
}

TEST(ProtobufStressTest, ManyConnections)
{
	MockLogger logger;
	auto setWrite = [](int) { return 0; };

	ProtoManager<MockLogger, MockRequest, MockResponse> mgr(logger, setWrite);

	std::function<int(int, const struct iovec*, int)> readv = [](int, const struct iovec*, int) { return 0; };
	std::function<int(int, const struct iovec*, int)> writev = [](int, const struct iovec*, int) { return 0; };

	// Register many connections
	const int numConnections = 100;
	for (int i = 0; i < numConnections; ++i) {
		EXPECT_TRUE(mgr.Register(i, readv, writev)) << "Failed to register connection " << i;
	}

	// Unregister all
	for (int i = 0; i < numConnections; ++i) {
		EXPECT_EQ(mgr.Unregister(i), 0) << "Failed to unregister connection " << i;
	}
}

TEST(ProtobufStressTest, WriteResponseManyTimes)
{
	MockLogger logger;
	auto setWrite = [](int) { return 0; };

	ProtoManager<MockLogger, MockRequest, MockResponse> mgr(logger, setWrite);

	std::function<int(int, const struct iovec*, int)> readv = [](int, const struct iovec*, int) { return 0; };
	std::function<int(int, const struct iovec*, int)> writev = [](int, const struct iovec* iov, int count) {
		int total = 0;
		for (int i = 0; i < count; ++i) {
			total += iov[i].iov_len;
		}
		return total;
	};

	EXPECT_TRUE(mgr.Register(5, readv, writev));

	MockResponse resp;

	// Write many responses, draining buffer each time
	for (int i = 0; i < 100; ++i) {
		EXPECT_EQ(mgr.WriteResponse(5, resp), 0) << "WriteResponse failed at iteration " << i;
		// Drain
		mgr.Write(5);
	}
}

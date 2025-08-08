#include <gtest/gtest.h>
#include <filesystem>
#include <memory>
#include "../include/mvcc_wal.hpp"
#include "../include/byte_buffer.hpp"

namespace util {
namespace {

class MvccWalTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a temporary directory for test files
        test_dir_ = std::filesystem::temp_directory_path() / "mvcc_wal_test";
        std::filesystem::create_directories(test_dir_);
        wal_path_ = test_dir_ / "test.wal";
    }

    void TearDown() override {
        // Clean up test files
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path wal_path_;
};

TEST_F(MvccWalTest, CreateAndRecover) {
    // Test basic WAL creation and recovery
    {
        auto wal = MvccWal::Create(wal_path_.string());
        ASSERT_TRUE(wal.has_value()) << "Failed to create MvccWal";
        EXPECT_TRUE(std::filesystem::exists(wal_path_));
    }

    // Test recovery from existing file
    {
        auto recovered_wal = MvccWal::Recover(wal_path_.string());
        ASSERT_TRUE(recovered_wal.has_value()) << "Failed to recover MvccWal";
    }
}

TEST_F(MvccWalTest, PutWithTimestamp) {
    auto wal = MvccWal::Create(wal_path_.string());
    ASSERT_TRUE(wal.has_value());

    // Test putting key-value with timestamp
    ByteBuffer key("test_key");
    ByteBuffer value("test_value");
    uint64_t timestamp = 12345;

    EXPECT_TRUE(wal.value()->PutWithTs(key, value, timestamp));
    EXPECT_TRUE(wal.value()->Sync());
}

TEST_F(MvccWalTest, DeleteWithTimestamp) {
    auto wal = MvccWal::Create(wal_path_.string());
    ASSERT_TRUE(wal.has_value());

    // Test deleting key with timestamp
    ByteBuffer key("test_key");
    uint64_t timestamp = 54321;

    EXPECT_TRUE(wal.value()->DeleteWithTs(key, timestamp));
    EXPECT_TRUE(wal.value()->Sync());
}

TEST_F(MvccWalTest, PutBatchWithTimestamp) {
    auto wal = MvccWal::Create(wal_path_.string());
    ASSERT_TRUE(wal.has_value());

    // Test batch operations with timestamps
    std::vector<std::pair<ByteBuffer, ByteBuffer>> data = {
        {ByteBuffer("key1"), ByteBuffer("value1")},
        {ByteBuffer("key2"), ByteBuffer("value2")},
        {ByteBuffer("key3"), ByteBuffer("value3")}
    };
    uint64_t timestamp = 99999;

    EXPECT_TRUE(wal.value()->PutBatchWithTs(data, timestamp));
    EXPECT_TRUE(wal.value()->Sync());
}

TEST_F(MvccWalTest, PersistenceAndRecovery) {
    ByteBuffer key1("test_key_1");
    ByteBuffer value1("test_value_1");
    uint64_t ts1 = 1000;

    ByteBuffer key2("test_key_2");
    ByteBuffer value2("test_value_2");
    uint64_t ts2 = 2000;

    ByteBuffer key3("test_key_3");
    uint64_t ts3 = 3000; // Delete operation

    // Write data to WAL
    {
        auto wal = MvccWal::Create(wal_path_.string());
        ASSERT_TRUE(wal.has_value());

        EXPECT_TRUE(wal.value()->PutWithTs(key1, value1, ts1));
        EXPECT_TRUE(wal.value()->PutWithTs(key2, value2, ts2));
        EXPECT_TRUE(wal.value()->DeleteWithTs(key3, ts3));
        EXPECT_TRUE(wal.value()->Sync());
    }

    // Verify file size is non-zero (data was written)
    EXPECT_GT(std::filesystem::file_size(wal_path_), 0);

    // Recover WAL and verify we can read it without errors
    {
        auto recovered_wal = MvccWal::Recover(wal_path_.string());
        ASSERT_TRUE(recovered_wal.has_value()) << "Failed to recover WAL";

        // WAL should be valid and ready for further operations
        ByteBuffer test_key("new_key");
        ByteBuffer test_value("new_value");
        uint64_t test_ts = 4000;

        EXPECT_TRUE(recovered_wal.value()->PutWithTs(test_key, test_value, test_ts));
        EXPECT_TRUE(recovered_wal.value()->Sync());
    }
}

TEST_F(MvccWalTest, MixedOperationsWithTimestamps) {
    auto wal = MvccWal::Create(wal_path_.string());
    ASSERT_TRUE(wal.has_value());

    // Mix of put, delete, and batch operations
    ByteBuffer key1("key1");
    ByteBuffer value1("value1");
    EXPECT_TRUE(wal.value()->PutWithTs(key1, value1, 100));

    ByteBuffer key2("key2");
    EXPECT_TRUE(wal.value()->DeleteWithTs(key2, 200));

    std::vector<std::pair<ByteBuffer, ByteBuffer>> batch = {
        {ByteBuffer("key3"), ByteBuffer("value3")},
        {ByteBuffer("key4"), ByteBuffer("value4")}
    };
    EXPECT_TRUE(wal.value()->PutBatchWithTs(batch, 300));

    EXPECT_TRUE(wal.value()->Sync());

    // Verify file has grown with all operations
    EXPECT_GT(std::filesystem::file_size(wal_path_), 0);
}

TEST_F(MvccWalTest, ErrorHandling) {
    // Test creating WAL with invalid path
    auto invalid_wal = MvccWal::Create("/invalid/path/test.wal");
    EXPECT_FALSE(invalid_wal.has_value()) << "Should fail to create WAL with invalid path";

    // Test recovering non-existent file
    auto missing_wal = MvccWal::Recover("/non/existent/file.wal");
    EXPECT_FALSE(missing_wal.has_value()) << "Should fail to recover non-existent WAL";
}

TEST_F(MvccWalTest, TimestampOrdering) {
    auto wal = MvccWal::Create(wal_path_.string());
    ASSERT_TRUE(wal.has_value());

    // Test with timestamps in different orders
    std::vector<uint64_t> timestamps = {5000, 1000, 3000, 2000, 4000};
    
    for (size_t i = 0; i < timestamps.size(); ++i) {
        ByteBuffer key("key_" + std::to_string(i));
        ByteBuffer value("value_" + std::to_string(i));
        EXPECT_TRUE(wal.value()->PutWithTs(key, value, timestamps[i]));
    }

    EXPECT_TRUE(wal.value()->Sync());
}

TEST_F(MvccWalTest, LargeDataWithTimestamps) {
    auto wal = MvccWal::Create(wal_path_.string());
    ASSERT_TRUE(wal.has_value());

    // Test with larger data
    std::string large_key(1000, 'k');
    std::string large_value(10000, 'v');
    ByteBuffer key(large_key);
    ByteBuffer value(large_value);
    uint64_t timestamp = 999999;

    EXPECT_TRUE(wal.value()->PutWithTs(key, value, timestamp));
    EXPECT_TRUE(wal.value()->Sync());

    // Verify file size reflects the large data
    EXPECT_GT(std::filesystem::file_size(wal_path_), large_key.size() + large_value.size());
}

} // namespace
} // namespace util

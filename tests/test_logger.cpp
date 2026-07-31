#include <gtest/gtest.h>
#include "core/logger.h"
#include <QTemporaryFile>
#include <QTextStream>

using namespace xrk;

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
    }
    
    void TearDown() override {
    }
};

TEST_F(LoggerTest, Singleton) {
    Logger& logger1 = Logger::instance();
    Logger& logger2 = Logger::instance();
    EXPECT_EQ(&logger1, &logger2);
}

TEST_F(LoggerTest, LogLevels) {
    Logger& logger = Logger::instance();
    
    logger.setLogLevel(LogLevel::Debug);
    EXPECT_EQ(logger.logLevel(), LogLevel::Debug);
    
    logger.setLogLevel(LogLevel::Info);
    EXPECT_EQ(logger.logLevel(), LogLevel::Info);
    
    logger.setLogLevel(LogLevel::Warning);
    EXPECT_EQ(logger.logLevel(), LogLevel::Warning);
    
    logger.setLogLevel(LogLevel::Error);
    EXPECT_EQ(logger.logLevel(), LogLevel::Error);
    
    logger.setLogLevel(LogLevel::Fatal);
    EXPECT_EQ(logger.logLevel(), LogLevel::Fatal);
}

TEST_F(LoggerTest, SetLogFile) {
    Logger& logger = Logger::instance();
    
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    QString fileName = tempFile.fileName();
    tempFile.close();
    
    logger.setLogFile(fileName);
    EXPECT_EQ(logger.logFile(), fileName);
}

TEST_F(LoggerTest, LogMessages) {
    Logger& logger = Logger::instance();
    logger.setLogLevel(LogLevel::Debug);
    
    logger.debug("Test debug message");
    logger.info("Test info message");
    logger.warning("Test warning message");
    logger.error("Test error message");
    logger.fatal("Test fatal message");
}

TEST_F(LoggerTest, LogWithCategory) {
    Logger& logger = Logger::instance();
    logger.setLogLevel(LogLevel::Debug);
    
    logger.debug("Debug message");
    logger.info("Info message");
    logger.warning("Warning message");
    logger.error("Error message");
    logger.fatal("Fatal message");
}

TEST_F(LoggerTest, SetLogFileAndWrite) {
    Logger& logger = Logger::instance();
    logger.setLogLevel(LogLevel::Debug);
    
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    QString fileName = tempFile.fileName();
    tempFile.close();
    
    logger.setLogFile(fileName);
    logger.info("Test log message");
    
    QFile file(fileName);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();
    
    EXPECT_TRUE(content.contains("Test log message"));
}

TEST_F(LoggerTest, ClearLogFile) {
    Logger& logger = Logger::instance();
    
    QTemporaryFile tempFile;
    tempFile.setAutoRemove(true);
    ASSERT_TRUE(tempFile.open());
    QString fileName = tempFile.fileName();
    tempFile.close();
    
    logger.setLogFile(fileName);
    logger.info("Message to clear");
    logger.clearLogFile();
    
    QFile file(fileName);
    ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();
    
    EXPECT_TRUE(content.isEmpty());
}

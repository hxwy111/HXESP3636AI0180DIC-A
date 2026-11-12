#include "sd_card_bsp.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include "sdmmc_cmd.h"

namespace {
constexpr char kTestFile[] = "/sd_card/read_write_test.bin";
constexpr size_t kTestSize = 512;
constexpr uint32_t kTestIntervalMs = 2000;

uint8_t writeBuffer[kTestSize];
uint8_t readBuffer[kTestSize];
uint32_t testCount = 0;
uint32_t nextTestMs = 0;

bool writeTestFile()
{
  for (size_t i = 0; i < sizeof(writeBuffer); ++i) {
    writeBuffer[i] = static_cast<uint8_t>((testCount + i) & 0xFF);
  }
  FILE *file = fopen(kTestFile, "w");
  if (file == nullptr) {
    Serial.printf("SD RW: fopen(wb) failed, errno=%d\n", errno);
    return false;
  }
  const size_t written = fwrite(writeBuffer, 1, sizeof(writeBuffer), file);
  const int flushResult = fflush(file);
  const int closeResult = fclose(file);
  struct stat info = {};
  const int statResult = stat(kTestFile, &info);
  if (written != sizeof(writeBuffer) || flushResult != 0 ||
      closeResult != 0 || statResult != 0) {
    Serial.printf("SD RW: write failed, bytes=%u/%u flush=%d close=%d stat=%d errno=%d size=%ld\n",
                  static_cast<unsigned>(written),
                  static_cast<unsigned>(sizeof(writeBuffer)), flushResult,
                  closeResult, statResult, errno,
                  statResult == 0 ? static_cast<long>(info.st_size) : -1L);
    return false;
  }
  Serial.printf("SD RW: file committed, size=%ld bytes\n",
                static_cast<long>(info.st_size));
  return true;
}

bool readAndVerifyTestFile()
{
  FILE *file = fopen(kTestFile, "rb");
  if (file == nullptr) {
    Serial.printf("SD RW: fopen(rb) failed, errno=%d\n", errno);
    return false;
  }
  const size_t readBytes = fread(readBuffer, 1, sizeof(readBuffer), file);
  const int readError = ferror(file);
  fclose(file);
  if (readBytes != sizeof(readBuffer) || readError != 0) {
    Serial.printf("SD RW: read failed, bytes=%u/%u ferror=%d\n",
                  static_cast<unsigned>(readBytes),
                  static_cast<unsigned>(sizeof(readBuffer)), readError);
    return false;
  }
  for (size_t i = 0; i < sizeof(readBuffer); ++i) {
    const uint8_t expected = static_cast<uint8_t>((testCount + i) & 0xFF);
    if (readBuffer[i] != expected) {
      Serial.printf("SD RW: verify failed at byte %u, got=0x%02X expected=0x%02X\n",
                    static_cast<unsigned>(i), readBuffer[i], expected);
      return false;
    }
  }
  return true;
}

void runReadWriteTest()
{
  Serial.printf("SD RW: cycle %lu\n", static_cast<unsigned long>(testCount));
  const bool writeOk = writeTestFile();
  const bool readOk = writeOk && readAndVerifyTestFile();
  Serial.printf("SD RW: WRITE=%s READ=%s RESULT=%s\n",
                writeOk ? "OK" : "FAIL", readOk ? "OK" : "FAIL",
                (writeOk && readOk) ? "PASS" : "FAIL");
  ++testCount;
}
}  // namespace

void setup()
{
  Serial.begin(115200);
  delay(3000);
  Serial.println("HXESP3636 SD read/write test starting");
  Serial.println("SD pins: CMD/MOSI=GPIO1 CLK/SCLK=GPIO2 DAT0/MISO=GPIO42");
  SD_card_Init();

  // sd_card_bsp.cpp exposes card as NULL when mounting fails.
  extern sdmmc_card_t *card;
  if (card == nullptr) {
    Serial.println("SD RW: card mount failed; read/write test not started");
    return;
  }
  runReadWriteTest();
  nextTestMs = millis() + kTestIntervalMs;
}
void loop()
{
  extern sdmmc_card_t *card;
  if (card == nullptr) {
    delay(1000);
    return;
  }
  const uint32_t now = millis();
  if (static_cast<int32_t>(now - nextTestMs) >= 0) {
    runReadWriteTest();
    nextTestMs = now + kTestIntervalMs;
  }
  delay(10);
}

#include <Arduino.h>
/*
 * ESP32-2432S032 microSD slot triage
 *
 * Answers three questions before any feature is designed around the card:
 *
 *   1. Which SPI bus is the slot on, and does it collide with the display?
 *      The TFT is on HSPI (SCLK=14 MOSI=13 MISO=12 CS=15). If the slot
 *      turns out to share those pins, every SD access has to be fenced
 *      against TFT_eSPI's transactions and the feature gets expensive.
 *      Community wiring for this board puts it on VSPI (18/23/19/5),
 *      which would make the two independent. Verify, don't assume.
 *
 *   2. Does the card mount? A 128GB card is SDXC and ships formatted
 *      exFAT. The Arduino SD library is FAT16/FAT32 only, so an
 *      out-of-the-box SDXC card fails to mount and reads like dead
 *      hardware. If CARD TYPE prints but mount fails, that is the cause:
 *      reformat FAT32 (Windows will not do >32GB natively - use the
 *      SD Association formatter or guiformat).
 *
 *   3. Does it survive the display being live? Mounting on a quiet bus
 *      proves nothing; the panel drives the TFT continuously. The second
 *      half of this sketch brings TFT_eSPI up and re-runs a read/write.
 *
 * Run:  pio run -e sdscan -t upload -t monitor
 */

#include <SPI.h>
#include <SD.h>
#include <TFT_eSPI.h>

// {label, SCK, MISO, MOSI, CS}. VSPI first - the expected wiring.
struct BusCandidate {
  const char *label;
  int sck, miso, mosi, cs;
};

static const BusCandidate CANDIDATES[] = {
  { "VSPI (expected)",   18, 19, 23,  5 },
  { "VSPI, alt CS 4",    18, 19, 23,  4 },
  { "shared with TFT",   14, 12, 13,  5 },
};
static const int NUM_CANDIDATES = sizeof(CANDIDATES) / sizeof(CANDIDATES[0]);

static SPIClass sdSPI(VSPI);
static TFT_eSPI tft = TFT_eSPI();

static const char *cardTypeName(uint8_t t) {
  switch (t) {
    case CARD_NONE:    return "none";
    case CARD_MMC:     return "MMC";
    case CARD_SD:      return "SDSC (<=2GB)";
    case CARD_SDHC:    return "SDHC/SDXC";
    default:           return "unknown";
  }
}

// Writes a file, reads it back, deletes it. Returns true on a clean
// round trip - this is the only thing that proves the card is usable
// rather than merely present.
static bool roundTrip(const char *path, const char *payload) {
  File f = SD.open(path, FILE_WRITE);
  if (!f) { Serial.printf("    open for write failed: %s\n", path); return false; }
  f.print(payload);
  f.close();

  f = SD.open(path, FILE_READ);
  if (!f) { Serial.printf("    open for read failed: %s\n", path); return false; }
  String back = f.readString();
  f.close();
  SD.remove(path);

  if (back != payload) {
    Serial.printf("    read back mismatch: got \"%s\"\n", back.c_str());
    return false;
  }
  return true;
}

static void report() {
  uint8_t type = SD.cardType();
  uint64_t sizeMB = SD.cardSize() / (1024ULL * 1024ULL);
  uint64_t usedMB = SD.usedBytes() / (1024ULL * 1024ULL);
  uint64_t totMB  = SD.totalBytes() / (1024ULL * 1024ULL);

  Serial.printf("    CARD TYPE : %s\n", cardTypeName(type));
  Serial.printf("    CARD SIZE : %llu MB\n", sizeMB);
  Serial.printf("    FILESYSTEM: %llu MB total, %llu MB used\n", totMB, usedMB);

  if (totMB == 0 && sizeMB > 0) {
    Serial.println("    --> card detected but no usable FAT filesystem.");
    Serial.println("        This is the exFAT case. Reformat FAT32.");
  }

  Serial.println("    listing root:");
  File root = SD.open("/");
  if (root) {
    int n = 0;
    for (File e = root.openNextFile(); e && n < 12; e = root.openNextFile(), n++) {
      Serial.printf("      %-24s %s\n", e.name(),
                    e.isDirectory() ? "<dir>" : String(e.size()).c_str());
      e.close();
    }
    if (n == 0) Serial.println("      (empty)");
    root.close();
  }
}

static int workingCandidate = -1;

void setup() {
  Serial.begin(115200);
  delay(1500);

  Serial.println("\n\n=================================================");
  Serial.println(" ESP32-2432S032 microSD triage");
  Serial.println("=================================================");

  for (int i = 0; i < NUM_CANDIDATES; i++) {
    const BusCandidate &c = CANDIDATES[i];
    Serial.printf("\n[%d] %s  SCK=%d MISO=%d MOSI=%d CS=%d\n",
                  i, c.label, c.sck, c.miso, c.mosi, c.cs);

    sdSPI.end();
    delay(10);
    sdSPI.begin(c.sck, c.miso, c.mosi, c.cs);

    // 4MHz first. SPI speed is a common reason a card that is wired
    // correctly still refuses to initialise.
    if (!SD.begin(c.cs, sdSPI, 4000000)) {
      Serial.println("    SD.begin failed on this pin set");
      SD.end();
      continue;
    }

    Serial.println("    SD.begin OK");
    report();

    if (roundTrip("/homemesh_probe.txt", "homemesh sd probe")) {
      Serial.println("    READ/WRITE ROUND TRIP OK");
      workingCandidate = i;
      break;
    }
    SD.end();
  }

  if (workingCandidate < 0) {
    Serial.println("\n-------------------------------------------------");
    Serial.println(" No pin set worked.");
    Serial.println(" Check in this order: card seated, card formatted");
    Serial.println(" FAT32 (not exFAT), then the pin candidates above.");
    Serial.println("-------------------------------------------------");
    return;
  }

  // ---- The part that actually matters: does it hold up with the
  //      display running? A quiet bus proves nothing.
  Serial.println("\n-------------------------------------------------");
  Serial.println(" Bringing the display up and retrying...");
  Serial.println("-------------------------------------------------");

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("SD triage");

  // Hammer the TFT so any bus contention has a chance to show itself.
  for (int i = 0; i < 40; i++) {
    tft.fillRect(10, 60, 300, 40, tft.color565(i * 6, 0, 120));
  }

  bool stillOk = roundTrip("/homemesh_probe2.txt", "after display init");
  Serial.printf("\n RESULT: SD after display init -> %s\n",
                stillOk ? "OK, buses are independent" : "FAILED, bus contention");

  tft.setCursor(10, 120);
  tft.setTextColor(stillOk ? TFT_GREEN : TFT_RED, TFT_BLACK);
  tft.println(stillOk ? "SD OK" : "SD FAILED");

  Serial.println("\n=================================================");
  Serial.printf(" Working pin set: [%d] %s\n",
                workingCandidate, CANDIDATES[workingCandidate].label);
  Serial.println(" Record these in the hardware table before building");
  Serial.println(" anything on top of the card.");
  Serial.println("=================================================");
}

void loop() {
  delay(10000);
}

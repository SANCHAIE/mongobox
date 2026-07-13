/* i2c_scanner — เช็กว่าเซ็นเซอร์ตัวไหนต่อติดบนสาย I2C บ้าง
   เปิด Serial Monitor 115200  แล้วดู address ที่เจอ
   คาดหวัง: 0x29(สี) 0x3C(จอ) 0x58(แก๊ส SGP30) 0x76/0x77(BME280) */
#include <Wire.h>
void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);            // SDA=8, SCL=9 (ESP32-S3 : เลี่ยง GPIO22 ที่ไม่มีบน S3)
  delay(500);
  Serial.println("\nI2C Scanner");
}
void loop() {
  byte count = 0;
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("  พบอุปกรณ์ที่ 0x");
      if (addr < 16) Serial.print("0");
      Serial.println(addr, HEX);
      count++;
    }
  }
  if (count == 0) Serial.println("  ไม่พบอุปกรณ์ (เช็กสาย SDA/SCL/ไฟเลี้ยง)");
  Serial.println("---");
  delay(3000);
}

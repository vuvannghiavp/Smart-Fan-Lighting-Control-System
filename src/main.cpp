
#include <Arduino.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ================================================================
// CHỌN CHẾ ĐỘ Ở ĐÂY
// ================================================================
//#define phat // Comment dòng này nếu nạp cho trạm THU (Có LCD, L298, LED)
#define thu  // Uncomment dòng này nếu nạp cho trạm THU (Có LCD, L298, LED)

// ================================================================
// CẤU HÌNH CHUNG
// ================================================================
RF24 radio(9, 10); 
const byte address[6] = "00001"; // Địa chỉ đường truyền không dây

// Cấu trúc gói tin đồng bộ trạng thái từ Phát sang Thu
typedef struct {
  float temp;       // Nhiệt độ đọc từ DHT22
  float humid;      // Độ ẩm đọc từ DHT22
  int16_t light;    // Giá trị ánh sáng đọc từ LDR
  bool lightState;  // Trạng thái Đèn được quyết định từ bên Phát (ON/OFF)
  bool motorState;  // Trạng thái Quạt được quyết định từ bên Phát (ON/OFF)
} SystemData;

// ================================================================
// PHẦN 1: TRẠM PHÁT (SENDER) - GỒM CẢM BIẾN & NÚT NHẤN ĐIỀU KHIỂN
// ================================================================
#ifdef phat

#define DHTPIN 2
#define DHTTYPE DHT22
#define LDR_PIN A0
#define BTN_LIGHT A2   // Nút nhấn điều khiển Đèn LED
#define BTN_MOTOR 4    // Nút nhấn điều khiển Quạt

// Ngưỡng tự động kích hoạt thiết bị
const int   LIGHT_THRESHOLD = 400;  
const float TEMP_THRESHOLD  = 32.0; 
const float HUMID_THRESHOLD = 75.0; 

DHT dht(DHTPIN, DHTTYPE);
SystemData sysData = {0.0, 0.0, 1023, false, false};

uint32_t lastSensorRead = 0;
uint32_t lastSend = 0;
bool sendImmediately = false;

// Biến lưu trạng thái tự động trước đó để phát hiện sườn thay đổi (Edge trigger)
bool lastAutoLight = false;
bool lastAutoMotor = false;

void setup() {
  Serial.begin(9600);
  delay(2000); 
  Serial.println(F("\n--- KHOI DONG TRAM PHAT (SENSORS & BUTTONS) ---"));

  dht.begin();
  
  // Thiết lập chân nút nhấn là INPUT_PULLUP (Nối nút nhấn vào chân này và GND)
  pinMode(BTN_LIGHT, INPUT_PULLUP);
  pinMode(BTN_MOTOR, INPUT_PULLUP);

  if (!radio.begin()) {
    Serial.println(F("[RF24] LOI PHAN CUNG!"));
    while (1); 
  }

  radio.setPALevel(RF24_PA_MIN);    
  radio.setDataRate(RF24_250KBPS);  
  radio.setChannel(115);            
  radio.setRetries(15, 15);
  radio.openWritingPipe(address);
  radio.stopListening(); // Trạm phát chỉ truyền dữ liệu đi
  
  Serial.println(F("[RF24] San sang truyen du lieu!"));
}

void loop() {
  // 1. Đọc cảm biến & Logic tự động (Định kỳ 2 giây/lần để bảo vệ tuổi thọ DHT22)
  if (millis() - lastSensorRead >= 2000) {
    lastSensorRead = millis();
    
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int16_t l = analogRead(LDR_PIN);

    if (!isnan(t) && !isnan(h)) {
      sysData.temp = t;
      sysData.humid = h;
      sysData.light = l;

      // Logic tự động cho Đèn (Phát hiện sườn thay đổi)
      bool currentAutoLight = (sysData.light > LIGHT_THRESHOLD);
      if (currentAutoLight != lastAutoLight) {
        sysData.lightState = currentAutoLight;
        lastAutoLight = currentAutoLight;
        sendImmediately = true; // Phát hiện sự thay đổi tự động, yêu cầu gửi ngay
      }

      // Logic tự động cho Quạt (Phát hiện sườn thay đổi)
      bool currentAutoMotor = (sysData.temp > TEMP_THRESHOLD || sysData.humid > HUMID_THRESHOLD);
      if (currentAutoMotor != lastAutoMotor) {
        sysData.motorState = currentAutoMotor;
        lastAutoMotor = currentAutoMotor;
        sendImmediately = true; // Phát hiện sự thay đổi tự động, yêu cầu gửi ngay
      }
    }
  }

  // 2. Kiểm tra và xử lý nút nhấn thủ công (Ghi đè tức thì, không độ trễ)
  if (digitalRead(BTN_LIGHT) == LOW) {
    delay(200); // Chống rung phím (Debounce)
    sysData.lightState = !sysData.lightState; // Đảo trạng thái Đèn
    sendImmediately = true; // Gửi trạng thái mới ngay lập tức sang bên Thu
    Serial.print(F("[BTN] Nut Bam Den -> ")); Serial.println(sysData.lightState ? "ON" : "OFF");
  }

  if (digitalRead(BTN_MOTOR) == LOW) {
    delay(200); // Chống rung phím (Debounce)
    sysData.motorState = !sysData.motorState; // Đảo trạng thái Quạt
    sendImmediately = true; // Gửi trạng thái mới ngay lập tức sang bên Thu
    Serial.print(F("[BTN] Nut Bam Quat -> ")); Serial.println(sysData.motorState ? "ON" : "OFF");
  }

  // 3. Truyền dữ liệu đi (Khi có thay đổi tức thì từ nút bấm/cảm biến hoặc định kỳ 3 giây để giữ kết nối)
  if (sendImmediately || (millis() - lastSend >= 3000)) {
    lastSend = millis();
    sendImmediately = false;

    bool ok = radio.write(&sysData, sizeof(SystemData));
    if (ok) {
      Serial.print(F("[TX OK] T:")); Serial.print(sysData.temp, 1);
      Serial.print(F(" H:")); Serial.print(sysData.humid, 1);
      Serial.print(F(" L:")); Serial.print(sysData.light);
      Serial.print(F(" | LED:")); Serial.print(sysData.lightState ? "ON" : "OFF");
      Serial.print(F(" FAN:")); Serial.println(sysData.motorState ? "ON" : "OFF");
    } else {
      Serial.println(F("[TX FAIL] Ben Thu ko phan hoi"));
    }
  }
}
#endif

// ================================================================
// PHẦN 2: TRẠM THU (RECEIVER) - CÓ LCD, ĐIỀU KHIỂN L298 & LED
// ================================================================
#ifdef thu

LiquidCrystal_I2C lcd(0x27, 16, 2);

// Định nghĩa chân điều khiển rơ-le LED và mạch cầu H L298N điều khiển Quạt
#define RELAY_LIGHT 5
#define IN1 6
#define IN2 7

SystemData sysData = {0.0, 0.0, 1023, false, false}; 
uint32_t lastRecv = 0;

void setup() {
  Serial.begin(9600);
  delay(2000);
  Serial.println(F("\n--- KHOI DONG TRAM THU (LCD, L298 & LED) ---"));

  // Thiết lập các chân điều khiển thiết bị đầu ra
  pinMode(RELAY_LIGHT, OUTPUT);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  
  // Tắt toàn bộ thiết bị ban đầu
  digitalWrite(RELAY_LIGHT, LOW);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  // Khởi động màn hình hiển thị LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.print(F("Dang ket noi..."));

  if (!radio.begin()) {
    lcd.setCursor(0, 1); 
    lcd.print(F("RF24 BI LOI!    "));
    while (1);
  }

  radio.setPALevel(RF24_PA_MIN);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(115);
  radio.setRetries(15, 15);
  
  radio.openReadingPipe(1, address);
  radio.startListening(); // Trạm thu liên tục lắng nghe

  Serial.println(F("[RF24] San sang nhan va thi hanh..."));
}

void loop() {
  // 1. Nhận gói tin đồng bộ từ bên Phát
  if (radio.available()) {
    radio.read(&sysData, sizeof(SystemData));
    lastRecv = millis();
    
    // --- THI HÀNH ĐIỀU KHIỂN CHẤP HÀNH ---
    
    // Thực thi rơ-le đèn LED
    digitalWrite(RELAY_LIGHT, sysData.lightState ? HIGH : LOW);

    // Thực thi mạch cầu H L298N điều khiển động cơ Quạt
    if (sysData.motorState) {
      digitalWrite(IN1, HIGH); 
      digitalWrite(IN2, LOW);
    } else {
      digitalWrite(IN1, LOW); 
      digitalWrite(IN2, LOW);
    }

    // --- CẬP NHẬT THÔNG TIN LÊN LCD ---
    lcd.setCursor(0, 0);
    lcd.print("T:"); lcd.print(sysData.temp, 1); lcd.print("C ");
    lcd.print("H:"); lcd.print(sysData.humid, 1); lcd.print("%  ");
    
    lcd.setCursor(0, 1);
    lcd.print("L:"); lcd.print(sysData.light);
    lcd.print(" D:"); lcd.print(sysData.lightState ? "ON " : "OF ");
    lcd.print("M:"); lcd.print(sysData.motorState ? "ON " : "OF ");

    // Xuất thông tin giám sát qua Serial
    Serial.print(F("[RX] T:")); Serial.print(sysData.temp, 1);
    Serial.print(F(" H:")); Serial.print(sysData.humid, 1);
    Serial.print(F(" L:")); Serial.print(sysData.light);
    Serial.print(F(" | LED:")); Serial.print(sysData.lightState ? "ON" : "OFF");
    Serial.print(F(" FAN:")); Serial.println(sysData.motorState ? "ON" : "OFF");
  }

  // 2. Cảnh báo mất kết nối an toàn (Nếu quá 7 giây không nhận được gói tin)
  if (lastRecv > 0 && (millis() - lastRecv > 7000)) {
    lcd.setCursor(0, 1);
    lcd.print(F("MAT KET NOI...  "));
    
    // Tắt toàn bộ thiết bị để bảo vệ hệ thống khi mất liên lạc
    digitalWrite(RELAY_LIGHT, LOW);
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
  }
}
#endif
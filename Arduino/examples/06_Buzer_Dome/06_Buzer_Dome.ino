#define BUZZER_PIN 48
 void setup() {
   pinMode(BUZZER_PIN, OUTPUT);  // 设置引脚为输出模式
   Serial.begin(115200);         // 串口初始化，用于调试
   Serial.println("无源蜂鸣器测试程序启动");  // 输出中文提示
 }
 void loop() {
   // 测试简单音阶：Do-Re-Mi-Fa-So-La-Si-Do
   tone(BUZZER_PIN, 262, 500);  // Do (C4)，持续500ms
   delay(500);
   tone(BUZZER_PIN, 294, 500);  // Re (D4)
   delay(500);
   tone(BUZZER_PIN, 330, 500);  // Mi (E4)
   delay(500);
   tone(BUZZER_PIN, 349, 500);  // Fa (F4)
   delay(500);
   tone(BUZZER_PIN, 392, 500);  // So (G4)
   delay(500);
   tone(BUZZER_PIN, 440, 500);  // La (A4)
   delay(500);
   tone(BUZZER_PIN, 494, 500);  // Si (B4)
   delay(500);
   tone(BUZZER_PIN, 523, 500);  // Do (C5)
   delay(500);

   noTone(BUZZER_PIN);  // 停止蜂鸣
   Serial.println("一轮音阶测试完成");  // 调试输出
   delay(2000);  // 每轮测试后暂停2秒
 }

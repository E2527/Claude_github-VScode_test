// シリアルから "M <left> <right>\n" (-255..255) を受け取り、
// L298N / TB6612 などのモータードライバーで左右のモーターを回すサンプル。
// receiver の --serial オプションと組み合わせて使う。
//
// 配線 (L298N の例、ピン番号は環境に合わせて変更):
//   左モーター  : ENA=5(PWM), IN1=7, IN2=8
//   右モーター  : ENB=6(PWM), IN3=9, IN4=10

const int L_PWM = 5, L_IN1 = 7, L_IN2 = 8;
const int R_PWM = 6, R_IN1 = 9, R_IN2 = 10;

// この時間指令が来なければ停止する (受信側が落ちた場合の保険)
const unsigned long TIMEOUT_MS = 500;

unsigned long lastCmdMs = 0;
char line[32];
size_t lineLen = 0;

void setMotor(int pwmPin, int in1, int in2, int speed) {
  speed = constrain(speed, -255, 255);
  digitalWrite(in1, speed > 0 ? HIGH : LOW);
  digitalWrite(in2, speed < 0 ? HIGH : LOW);
  analogWrite(pwmPin, abs(speed));
}

void drive(int left, int right) {
  setMotor(L_PWM, L_IN1, L_IN2, left);
  setMotor(R_PWM, R_IN1, R_IN2, right);
}

void handleLine(const char *s) {
  int left, right;
  if (sscanf(s, "M %d %d", &left, &right) == 2) {
    drive(left, right);
    lastCmdMs = millis();
  }
}

void setup() {
  int pins[] = {L_PWM, L_IN1, L_IN2, R_PWM, R_IN1, R_IN2};
  for (int p : pins) pinMode(p, OUTPUT);
  drive(0, 0);
  Serial.begin(115200);
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      line[lineLen] = '\0';
      handleLine(line);
      lineLen = 0;
    } else if (lineLen < sizeof(line) - 1) {
      line[lineLen++] = c;
    }
  }
  if (millis() - lastCmdMs > TIMEOUT_MS) drive(0, 0);
}

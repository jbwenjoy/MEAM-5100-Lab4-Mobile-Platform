/*
  My ip: 192.168.1.142
  My MAC: 86:67:25:82:66:64
*/

// #include <WiFi.h>
#include "body.h"
#include "html510.h"

// wifi and html
const char *ssid = "Furina";
const char *pwd = "Furinaaa";
IPAddress local_IP(192, 168, 1, 142);
IPAddress gateway_IP(192, 168, 1, 1);
IPAddress subnet_IP(255, 255, 255, 0);
WiFiServer wifi_server(8080);
HTML510Server html_server(80);

// define esp32c3 pins
// always available gpio: 1, 4, 5, 10, 18, 19
// only output: 8, 9
// 5ns output low: 0, 6, 7
// adc: 0, 1, 4, 5

// encoders
#define ENCODER_0 0  // left
#define ENCODER_1 1  // right
// bettery voltage
#define BATTERY 4
// motors
#define MOTOR_0 5  // motor 0 left pwm
#define MOTOR_1 6  // motor 1 right pwm
// motor directions
// #define DIR_PIN_00 7   // motor 0 direction control pin 0
// #define DIR_PIN_01 10  // motor 0 direction control pin 1
#define DIR_PIN_00 10  // motor 0 direction control pin 0
#define DIR_PIN_01 7   // motor 0 direction control pin 1
#define DIR_PIN_10 18  // motor 1 direction control pin 0
#define DIR_PIN_11 19  // motor 1 direction control pin 1

// define pwm
// pwm channel
#define LEDC_0 0  // motor 0 left
#define LEDC_1 1  // motor 1 right
// pwm properties
#define LEDC_RES_BITS 12  // LEDC resolution bit
#define LEDC_RES ((1 << LEDC_RES_BITS) - 1)
#define LEDC_FREQ 5000

// motor global variables
int des_speed = 4000;       // car desired speed, 0 - 4095
int des_speed_delta = 0;    // deriviation of desired speed, this var is used for accelating and slowing down using keyboard + and -
uint8_t motor_0_dir = 1;    // left wheel, 1 - forward, 0 - backward
uint8_t motor_1_dir = 1;    // right wheel, 1 - forward, 0 - backward
int motor_0_des_speed = 0;  // left wheel, 0 - 4095
int motor_1_des_speed = 0;  // right wheel, 0 - 4095
int turn_rate = 20;         // speed diff between L R wheel when turning, 0 - 100
uint8_t stop_flag = 0;      // 1 - stop, 0 - not stop
char curr_action = 'O';     // default is stop

// pid global variables
int encoderCount_0 = 0;
int encoderCount_1 = 0;
int encoderCount_0_nonclear = 0;
int encoderCount_1_nonclear = 0;
volatile unsigned long prevTime_0 = 0;
volatile unsigned long prevTime_1 = 0;
float Kp = 2, Ki = 0.3, Kd = 0.001;  // PID parameters
float last_RPM_0 = 0;
float last_RPM_1 = 0;
float integral_0 = 0;
float integral_1 = 0;
float derivative_0 = 0;
float derivative_1 = 0;
float RPM_0 = 0.0;
float RPM_1 = 0.0;
float deltaTime_0 = 0.0;
float deltaTime_1 = 0.0;
unsigned long lastTime_0 = 0;
unsigned long lastTime_1 = 0;
int call_PID_flag_0 = 0;
int call_PID_flag_1 = 0;

// autopilot parameters
uint8_t autopilot_flag = 0;           // autopilot activation variable, default is inactive 0, 1 is active
const int autopilot_series_len = 50;  // max number of actions autopilot will take
// int autopilot_time_arr[autopilot_series_len] = { 2000, 500, 2000, 500, -1 };       // delay ms for each action, -1 means end
char autopilot_action_arr[autopilot_series_len] = { 'F', 'F', 'R', 'F', 'F', 'R', 'F', 'F', 'R', 'F', 'F', 'R', 'Z' };      // 'F' 'B' 'L' 'R' 'O', 'Z' means end
int autopilot_speed_arr[autopilot_series_len] = { 4090, 1700, 3500, 4090, 1700, 3500, 4090, 1700, 3500, 4090, 1700, 3500, -1 };    // 0-4095, -1 means end
int8_t autopilot_turnrate_arr[autopilot_series_len] = { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 };  // 0 - 100, -1 means end
// int autopilot_counter_Lwheel[autopilot_series_len] = { 98, 0, 98, -1 };            // counts is equivalent to distance, 1 count = 10.205 mm, -1 means end
// int autopilot_counter_Rwheel[autopilot_series_len] = { 98, 20, 98, -1 };           // counts is equivalent to distance, 1 count = 10.205 mm, -1 means end
int autopilot_dist_counter[autopilot_series_len] = { 101, 8, 12, 60, 8, 13, 101, 8, 12, 60, 8, 13, -1 };  // average count of R and L wheel, 1 count = 10.205 mm, -1 means end
int autopilot_action_count = 0;                                         // indicate the index of current autopilot actions


// provide desired speed for the PID
// use specified speed and turn rate
int desiredSpeedLWheel(char action) {
  float rate = turn_rate;
  float Lspeed;
  switch (action) {
    case 'F':
      return des_speed + des_speed_delta;
    case 'B':
      return des_speed + des_speed_delta;
    case 'L':  // slower
      rate /= 100;
      if ((des_speed + des_speed_delta) * (1.0 + 0.5 * rate) > LEDC_RES) {
        Lspeed = (des_speed + des_speed_delta) * (1.0 - rate);
      } else {
        Lspeed = (des_speed + des_speed_delta) * (1.0 - 0.5 * rate);
      }
      return (int)Lspeed;
    case 'R':  // faster
      rate /= 100;
      Lspeed = (des_speed + des_speed_delta) * (1.0 + 0.5 * rate);
      if (Lspeed > LEDC_RES)
        Lspeed = LEDC_RES;
      return (int)Lspeed;
    default:
      return 0;
  }
}
int desiredSpeedRWheel(char action) {
  float rate = turn_rate;
  float Rspeed;
  switch (action) {
    case 'F':
      return des_speed + des_speed_delta;
    case 'B':
      return des_speed + des_speed_delta;
    case 'L':  // faster, but cannot be faster than 16384
      rate /= 100;
      Rspeed = (des_speed + des_speed_delta) * (1.0 + 0.5 * rate);
      if (Rspeed > LEDC_RES)
        Rspeed = LEDC_RES;
      return (int)Rspeed;
    case 'R':  // slower, but should consider that the other wheel cannot be faster than 16384
      rate /= 100;
      if ((des_speed + des_speed_delta) * (1.0 + 0.5 * rate) > LEDC_RES) {
        Rspeed = (des_speed + des_speed_delta) * (1.0 - rate);
      } else {
        Rspeed = (des_speed + des_speed_delta) * (1.0 - 0.5 * rate);
      }
      return (int)Rspeed;
    default:
      return 0;
  }
}


int calculatePID_0(float setpoint, float Kp, float Ki, float Kd, float &lastInput, unsigned long &lastTime, float RPM_val) {
  float error = setpoint - RPM_val;
  // float integral = integral + error;
  integral_0 = integral_0 + error;
  derivative_0 = 1000 * (RPM_val - last_RPM_0) / 1;
  int output = Kp * error + Ki * integral_0 + constrain(Kd * derivative_0, -0.1 * Kp * error, 0.1 * Kp * error);
  output = constrain(output, -80, 40);

  last_RPM_0 = RPM_val;
  return output;
}
int calculatePID_1(float setpoint, float Kp, float Ki, float Kd, float &lastInput, unsigned long &lastTime, float RPM_val) {
  float error = setpoint - RPM_val;
  // float integral = integral + error;
  integral_1 = integral_1 + error;
  derivative_1 = 1000 * (RPM_val - last_RPM_1) / 1;
  int output = Kp * error + Ki * integral_1 + constrain(Kd * derivative_1, -0.1 * Kp * error, 0.1 * Kp * error);
  output = constrain(output, -80, 40);

  last_RPM_1 = RPM_val;
  return output;
}


// manage stop flag, motor direction, and calculate motor desired speed
void takeAction() {
  switch (curr_action) {
    case 'F':
      stop_flag = 0;
      motor_0_dir = 1;
      digitalWrite(DIR_PIN_00, HIGH);
      digitalWrite(DIR_PIN_01, LOW);
      motor_1_dir = 1;
      digitalWrite(DIR_PIN_10, HIGH);
      digitalWrite(DIR_PIN_11, LOW);
      // Serial.print("\nDirection: Forward");
      motor_0_des_speed = desiredSpeedLWheel('F');
      motor_1_des_speed = desiredSpeedRWheel('F');
      break;
    case 'B':
      stop_flag = 0;
      motor_0_dir = 0;
      digitalWrite(DIR_PIN_00, LOW);
      digitalWrite(DIR_PIN_01, HIGH);
      motor_1_dir = 0;
      digitalWrite(DIR_PIN_10, LOW);
      digitalWrite(DIR_PIN_11, HIGH);
      // Serial.print("\nDirection: Backward");
      motor_0_des_speed = desiredSpeedLWheel('B');
      motor_1_des_speed = desiredSpeedRWheel('B');
      break;
    case 'L':
      stop_flag = 0;
      motor_0_dir = 1;
      digitalWrite(DIR_PIN_00, HIGH);
      digitalWrite(DIR_PIN_01, LOW);
      motor_1_dir = 1;
      digitalWrite(DIR_PIN_10, HIGH);
      digitalWrite(DIR_PIN_11, LOW);
      // Serial.print("\nDirection: Forward Left");
      motor_0_des_speed = desiredSpeedLWheel('L');
      motor_1_des_speed = desiredSpeedRWheel('L');
      break;
    case 'R':
      stop_flag = 0;
      motor_0_dir = 1;
      digitalWrite(DIR_PIN_00, HIGH);
      digitalWrite(DIR_PIN_01, LOW);
      motor_1_dir = 1;
      digitalWrite(DIR_PIN_10, HIGH);
      digitalWrite(DIR_PIN_11, LOW);
      // Serial.print("\nDirection: Forward Right");
      motor_0_des_speed = desiredSpeedLWheel('R');
      motor_1_des_speed = desiredSpeedRWheel('R');
      break;
    default:  // case 'O' 'S'
      stop_flag = 1;
      // Serial.print("\nDirection: Stop");
      motor_0_des_speed = desiredSpeedLWheel('O');
      motor_1_des_speed = desiredSpeedRWheel('O');
  }
}


void handleRoot() {
  html_server.sendhtml(body);
}
void handleSpeed() {
  des_speed = html_server.getVal();  // speed range from 0-4095, percent of duty cycle
  // Serial.print("\nHandle Speed: ");
  // Serial.print(des_speed);
}
void handleTurnRate() {
  turn_rate = html_server.getVal();  // turn rate range from -100-100, percent of speed difference between left and right wheel
  // Serial.print("\nHandle Turn Rate: ");
  // Serial.print(turn_rate);
}
void handleForward() {
  curr_action = 'F';
  // Serial.print("\nDirection: Forward");
}
void handleBackward() {
  curr_action = 'B';
  // Serial.print("\nDirection: Backward");
}
void handleForwardLeft() {
  curr_action = 'L';
  // Serial.print("\nDirection: Forward Left");
}
void handleForwardRight() {
  curr_action = 'R';
  // Serial.print("\nDirection: Forward Right");
}
void handleStop() {
  curr_action = 'O';
  // Serial.print("\nDirection: Stop");
}
// autopilot will automatically complete the circle
// activate/deactivate autopilot
void handleAutopilotOn() {
  autopilot_flag = 1;
  stop_flag = 0;
  encoderCount_0_nonclear = 0;  // clear these so that the distance of autopilot is 0 when starting
  encoderCount_1_nonclear = 0;
  // Serial.print("\nAutopilot activated");
}
void handleAutopilotOff() {
  autopilot_flag = 0;
  stop_flag = 1;
  handleStop();
  encoderCount_0_nonclear = 0;
  encoderCount_1_nonclear = 0;
  autopilot_action_count = 0;  // wait for autopilot to be activated next time
  // Serial.print("\nAutopilot deactivated");
}
// // autopilot
// void missionPlanner() {
//   int i = 0;
//   while (autopilot_flag){
//     if(autopilot_action_arr[i] == 'Z') {
//       autopilot_flag = 0;
//       break;
//     }
//     curr_action = autopilot_action_arr[i];
//     des_speed = autopilot_speed_arr[i];
//     turn_rate = autopilot_turnrate_arr[i];
//     takeAction();
//     delay(autopilot_time_arr[i]);
//     i++;
//   }
// }
void handleSpeedUp() {
  int delta_speed = 500;
  if (des_speed + des_speed_delta + delta_speed > 4095) {
    des_speed_delta = 4095 - des_speed;
  } else {
    des_speed_delta += delta_speed;
  }
  // Serial.print("\nSpeed up: ");
  // Serial.print(des_speed + des_speed_delta);
}
void handleSlowDown() {
  int delta_speed = -500;
  if (des_speed + delta_speed + des_speed_delta < 1600) {
    des_speed_delta = 1600 - des_speed;
  } else {
    des_speed_delta += delta_speed;
  }
  // Serial.print("\nSlow down: ");
  // Serial.print(des_speed + des_speed_delta);
}


void setup() {
  Serial.begin(115200);

  // wifi setup
  WiFi.mode(WIFI_MODE_AP);  // wifi in ap mode, no router
  WiFi.softAPConfig(local_IP, gateway_IP, subnet_IP);
  WiFi.softAP(ssid, pwd);
  wifi_server.begin();
  IPAddress softAP_IP = WiFi.softAPIP();
  // Serial.print("\n AP IP address: ");
  // Serial.print(softAP_IP);
  // Serial.print("\n SSID: ");
  // Serial.print(ssid);
  // Serial.print("\n Password: ");
  // Serial.print(pwd);

  // ledc setup
  ledcSetup(LEDC_0, LEDC_FREQ, LEDC_RES_BITS);
  ledcSetup(LEDC_1, LEDC_FREQ, LEDC_RES_BITS);
  // attach pwm channel to the GPIO pin
  ledcAttachPin(MOTOR_0, LEDC_0);
  ledcAttachPin(MOTOR_1, LEDC_1);
  // Set encoder and photointerrupter pins as inputs
  pinMode(ENCODER_0, INPUT_PULLUP);
  pinMode(ENCODER_1, INPUT_PULLUP);
  // Attach an interrupt for the encoder signal
  attachInterrupt(digitalPinToInterrupt(ENCODER_0), handleEncoderInterrupt_0, HIGH);
  attachInterrupt(digitalPinToInterrupt(ENCODER_1), handleEncoderInterrupt_1, HIGH);

  // set up direction control pins
  pinMode(DIR_PIN_00, OUTPUT);
  pinMode(DIR_PIN_01, OUTPUT);
  pinMode(DIR_PIN_10, OUTPUT);
  pinMode(DIR_PIN_11, OUTPUT);
  // default is forward
  digitalWrite(DIR_PIN_00, HIGH);
  digitalWrite(DIR_PIN_01, LOW);
  digitalWrite(DIR_PIN_10, HIGH);
  digitalWrite(DIR_PIN_11, LOW);

  // routines from HTML510.h
  html_server.begin();
  html_server.attachHandler("/", handleRoot);
  html_server.attachHandler("/autopilot_on", handleAutopilotOn);
  html_server.attachHandler("/autopilot_off", handleAutopilotOff);
  html_server.attachHandler("/speed_slider=", handleSpeed);
  html_server.attachHandler("/turn_rate_slider=", handleTurnRate);
  html_server.attachHandler("/F", handleForward);
  html_server.attachHandler("/B", handleBackward);
  html_server.attachHandler("/L", handleForwardLeft);
  html_server.attachHandler("/R", handleForwardRight);
  html_server.attachHandler("/O", handleStop);
  html_server.attachHandler("/S", handleStop);
  html_server.attachHandler("/+", handleSpeedUp);
  html_server.attachHandler("/-", handleSlowDown);
  delay(500);
}


void loop() {
  html_server.serve();

  // if (stop_flag) {
  //   ledcWrite(LEDC_0, 0);
  //   ledcWrite(LEDC_1, 0);
  // } else
  if (!autopilot_flag) {
    takeAction();

    if (stop_flag) {
      ledcWrite(LEDC_0, 0);
      ledcWrite(LEDC_1, 0);
    } else {
      // with PID
      // when first start, PID won't be called because PID flags are all 0
      // so we need to get it moving to let the interrupt function change the flags to 1
      if (call_PID_flag_0 == 0) {
        ledcWrite(LEDC_0, motor_0_des_speed);
      }
      if (call_PID_flag_1 == 0) {
        ledcWrite(LEDC_1, motor_1_des_speed);
      }
      if (motor_0_des_speed == motor_1_des_speed)  // go straight
      {
        int rpm_error_01 = RPM_0 - RPM_1;  // positive if RPM0 > RPM1
        if (call_PID_flag_0 == 1) {
          int setpoint_0 = motor_0_des_speed / 25;  // convert 0-4095 to about 0-80
          int output_0 = calculatePID_0(setpoint_0 + rpm_error_01 / 2, Kp, Ki, Kd, last_RPM_0, lastTime_0, RPM_0);
          int motorSpeed_0 = map(output_0, -100, 100, 4095, 0);
          ledcWrite(LEDC_0, motorSpeed_0);
          // Serial.print("\nPID0: ");
          // Serial.print(motorSpeed_0);
          call_PID_flag_0 = 0;
          encoderCount_0 = 0;
        }
        if (call_PID_flag_1 == 1) {
          int setpoint_1 = motor_1_des_speed / 25;  // convert 0-4095 to about 0-80
          int output_1 = calculatePID_1(setpoint_1 - rpm_error_01 / 2, Kp, Ki, Kd, last_RPM_1, lastTime_1, RPM_1);
          int motorSpeed_1 = map(output_1, -100, 100, 4095, 0);
          ledcWrite(LEDC_1, motorSpeed_1);
          // Serial.print("\nPID1: ");
          // Serial.print(motorSpeed_1);
          call_PID_flag_1 = 0;
          encoderCount_1 = 0;
        }
      } else {  // turning
        if (call_PID_flag_0 == 1) {
          int setpoint_0 = motor_0_des_speed / 25;  // convert 0-4095 to about 0-80
          int output_0 = calculatePID_0(setpoint_0 / 2, Kp, Ki, Kd, last_RPM_0, lastTime_0, RPM_0);
          int motorSpeed_0 = map(output_0, -100, 100, 4095, 0);
          ledcWrite(LEDC_0, motorSpeed_0);
          // Serial.print("\nPID0: ");
          // Serial.print(motorSpeed_0);
          call_PID_flag_0 = 0;
          encoderCount_0 = 0;
        }
        if (call_PID_flag_1 == 1) {
          int setpoint_1 = motor_1_des_speed / 25;  // convert 0-4095 to about 0-80
          int output_1 = calculatePID_1(setpoint_1 / 2, Kp, Ki, Kd, last_RPM_1, lastTime_1, RPM_1);
          int motorSpeed_1 = map(output_1, -100, 100, 4095, 0);
          ledcWrite(LEDC_1, motorSpeed_1);
          // Serial.print("\nPID1: ");
          // Serial.print(motorSpeed_1);
          call_PID_flag_1 = 0;
          encoderCount_1 = 0;
        }
      }

      // print rpm
      // Serial.print("\n N0: ");
      // Serial.print(RPM_0);
      // Serial.print("\t N1: ");
      // Serial.print(RPM_1);
      // Serial.print("\t N0-N1: ");
      // Serial.print(RPM_0 - RPM_1);

      // // without PID
      // ledcWrite(LEDC_0, motor_0_des_speed);
      // ledcWrite(LEDC_1, motor_1_des_speed);

      // Serial.print("\nMotor 0: ");
      // Serial.print(motor_0_des_speed);
      // Serial.print("\tMotor 1: ");
      // Serial.print(motor_1_des_speed);
    }

    // Serial.print("\nDes spd ");
    // Serial.print(des_speed);
    // Serial.print("\tTurn rate: ");
    // Serial.print(turn_rate);
    // Serial.print("\nStop flag: ");
    // Serial.print(stop_flag);
    // delay(10);

  } else {  // use autopilot
    if (autopilot_action_arr[autopilot_action_count] == 'Z' || autopilot_speed_arr[autopilot_action_count] == -1 || autopilot_turnrate_arr[autopilot_action_count] == -1 || autopilot_dist_counter[autopilot_action_count] == -1) {
      handleAutopilotOff();
      handleStop();
    } else {
      curr_action = autopilot_action_arr[autopilot_action_count];
      des_speed = autopilot_speed_arr[autopilot_action_count];
      turn_rate = autopilot_turnrate_arr[autopilot_action_count];
      takeAction();

      // with PID
      // when first start, PID won't be called because PID flags are all 0
      // so we need to get it moving to let the interrupt function change the flags to 1
      if (call_PID_flag_0 == 0) {
        ledcWrite(LEDC_0, motor_0_des_speed);
      }
      if (call_PID_flag_1 == 0) {
        ledcWrite(LEDC_1, motor_1_des_speed);
      }
      if (motor_0_des_speed == motor_1_des_speed)  // go straight
      {
        int rpm_error_01 = RPM_0 - RPM_1;  // positive if RPM0 > RPM1
        if (call_PID_flag_0 == 1) {
          int setpoint_0 = motor_0_des_speed / 25;  // convert 0-4095 to about 0-80
          int output_0 = calculatePID_0(setpoint_0 + rpm_error_01 / 2, Kp, Ki, Kd, last_RPM_0, lastTime_0, RPM_0);
          int motorSpeed_0 = map(output_0, -100, 100, 4095, 0);
          ledcWrite(LEDC_0, motorSpeed_0);
          // Serial.print("\nPID0: ");
          // Serial.print(motorSpeed_0);
          call_PID_flag_0 = 0;
          encoderCount_0 = 0;
        }
        if (call_PID_flag_1 == 1) {
          int setpoint_1 = motor_1_des_speed / 25;  // convert 0-4095 to about 0-80
          int output_1 = calculatePID_1(setpoint_1 - rpm_error_01 / 2, Kp, Ki, Kd, last_RPM_1, lastTime_1, RPM_1);
          int motorSpeed_1 = map(output_1, -100, 100, 4095, 0);
          ledcWrite(LEDC_1, motorSpeed_1);
          // Serial.print("\nPID1: ");
          // Serial.print(motorSpeed_1);
          call_PID_flag_1 = 0;
          encoderCount_1 = 0;
        }
      } else {  // turning
        if (call_PID_flag_0 == 1) {
          int setpoint_0 = motor_0_des_speed / 25;  // convert 0-4095 to about 0-80
          int output_0 = calculatePID_0(setpoint_0 / 2, Kp, Ki, Kd, last_RPM_0, lastTime_0, RPM_0);
          int motorSpeed_0 = map(output_0, -100, 100, 4095, 0);
          ledcWrite(LEDC_0, motorSpeed_0);
          // Serial.print("\nPID0: ");
          // Serial.print(motorSpeed_0);
          call_PID_flag_0 = 0;
          encoderCount_0 = 0;
        }
        if (call_PID_flag_1 == 1) {
          int setpoint_1 = motor_1_des_speed / 25;  // convert 0-4095 to about 0-80
          int output_1 = calculatePID_1(setpoint_1 / 2, Kp, Ki, Kd, last_RPM_1, lastTime_1, RPM_1);
          int motorSpeed_1 = map(output_1, -100, 100, 4095, 0);
          ledcWrite(LEDC_1, motorSpeed_1);
          // Serial.print("\nPID1: ");
          // Serial.print(motorSpeed_1);
          call_PID_flag_1 = 0;
          encoderCount_1 = 0;
        }
      }

      // print rpm
      // Serial.print("\n N0: ");
      // Serial.print(RPM_0);
      // Serial.print("\t N1: ");
      // Serial.print(RPM_1);
      // Serial.print("\t N0-N1: ");
      // Serial.print(RPM_0 - RPM_1);

      // Serial.print("\n");
      // Serial.print(motor_0_des_speed);
      // Serial.print("\t");
      // Serial.print(motor_1_des_speed);
      // Serial.print("\t");
      // Serial.print(autopilot_action_arr[autopilot_action_count]);
      // delay(autopilot_time_arr[i]);

      if (encoderCount_0_nonclear + encoderCount_1_nonclear >= 2 * autopilot_dist_counter[autopilot_action_count]) {
        autopilot_action_count++;
        encoderCount_0_nonclear = 0;
        encoderCount_1_nonclear = 0;
      }
      // Serial.println("AutoPilot!");
      // no delay in this case
    }
  }
}


void handleEncoderInterrupt_0() {
  encoderCount_0++;
  encoderCount_0_nonclear++;
  if (encoderCount_0 == 1) {
    // clear encoder count for the next interrupt
    encoderCount_0 = 0;
    // set pid flag to call pid
    call_PID_flag_0 = 1;

    unsigned long currentTime = millis();
    deltaTime_0 = (currentTime - lastTime_0);  // Convert milliseconds to seconds
    RPM_0 = (60 * 1 * 1000) / (20 * deltaTime_0);
    // Serial.print("\nRPM0: ");
    // Serial.print(RPM_0);
    lastTime_0 = currentTime;
  }
}
void handleEncoderInterrupt_1() {
  encoderCount_1++;
  encoderCount_1_nonclear++;
  if (encoderCount_1 == 1) {
    // clear encoder count for the next interrupt
    encoderCount_1 = 0;
    // set pid flag to call pid
    call_PID_flag_1 = 1;

    unsigned long currentTime = millis();
    deltaTime_1 = (currentTime - lastTime_1);  // Convert milliseconds to seconds
    RPM_1 = (60 * 1 * 1000) / (20 * deltaTime_1);
    // Serial.print("\tRPM1: ");
    // Serial.print(RPM_1);
    lastTime_1 = currentTime;
  }
}

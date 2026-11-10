#include<Arduino.h>
#include<EEPROM.h>
#include<math.h>
#include "TelemetryLink.h"

#define LeftSensor 13
#define RightSensor 12

#define ENA 3
#define ENB 11

#define In1 5
#define In2 6
#define In3 9
#define In4 10

int LeftVal;
int RightVal;

Telemetry telemetry;
TelemetryLink telemetryLink(Serial, 200); // HC-05: 9600 baud, telemetry 5 Hz.
const int bootIdAddress = 0; // EEPROM bytes 0..3, written only in setup().

/* Function cotrols motors to go straight ahead
 void Go_Straight(){
    analogWrite(ENA, 70);
    analogWrite(ENB, 70);
    digitalWrite(In1, 0);
    digitalWrite(In2, 1);
    digitalWrite(In3, 0);
    digitalWrite(In4, 1);
}

// Function cotrols motor to turn right
void TurnRight(){
    analogWrite(ENA, 80);
    analogWrite(ENB, 0);
    digitalWrite(In1, 0);
    digitalWrite(In2, 1);
    digitalWrite(In3, 0);
    digitalWrite(In4, 0);
}

// Function cotrols motor to turn left
void TurnLeft(){
    analogWrite(ENA, 0);
    analogWrite(ENB, 80);
    digitalWrite(In1, 0);
    digitalWrite(In2, 0);
    digitalWrite(In3, 0);
    digitalWrite(In4, 1);
}
*/

// Function controls motors to stop
void Stop(){
    analogWrite(ENA, 0);
    analogWrite(ENB, 0);
    digitalWrite(In1, 0);
    digitalWrite(In2, 0);
    digitalWrite(In3, 0);
    digitalWrite(In4, 0);
    telemetry.pwmL = 0;
    telemetry.pwmR = 0;
    telemetry.pidMilli = 0;
    telemetry.mode = 0;
}
// Set PID Gain

int basePWM = 70;
float Kp = 25.0f;
float Ki = 0.0f;
float Kd = 0.0f;

float integral = 0.0f;
float previousError = 0.0f;
bool hasPreviousError = false;

const unsigned long controlPeriodMs = 10;
unsigned long lastControlMs = 0;

void ResetPID() {
    integral = 0.0f;
    previousError = 0.0f;
    hasPreviousError = false;
}

void setForwardPWM(int leftPWM, int rightPWM){
    leftPWM = constrain(leftPWM, 0, 255);
    rightPWM = constrain(rightPWM, 0, 255);

    digitalWrite(In1, LOW);
    digitalWrite(In2, HIGH);

    digitalWrite(In3, LOW);
    digitalWrite(In4, HIGH);

    analogWrite(ENA, leftPWM);
    analogWrite(ENB, rightPWM);
    telemetry.pwmL = leftPWM;
    telemetry.pwmR = rightPWM;
    telemetry.mode = 1;
}

float CaculatePID(float error, float dt){
    float derivative = 0.0f;

    if(hasPreviousError){
        derivative = (error - previousError)/dt;
    }

    // Chua tich luy khi Ki = 0
    if(Ki != 0.0f){
        integral += error * dt;
        integral = constrain(integral, -5.0f, 5.0f);
    }
    else {
        integral = 0.0f;
    }

    float u = Kp * error + Ki * integral + Kd * derivative;

    // Update prvError
    previousError = error;
    hasPreviousError = true;

    return u;
}


void setup(){

    pinMode(LeftSensor, INPUT);
    pinMode(RightSensor, INPUT);
    pinMode(In1, OUTPUT);
    pinMode(In2, OUTPUT);
    pinMode(In3, OUTPUT);
    pinMode(In4, OUTPUT);
    pinMode(ENA, OUTPUT);
    pinMode(ENB, OUTPUT);

    digitalWrite(In1, LOW);
    digitalWrite(In2, LOW);
    digitalWrite(In3, LOW);
    digitalWrite(In4, LOW);
    digitalWrite(ENA, LOW);
    digitalWrite(ENB, LOW);

    Serial.begin(9600);

    uint32_t boot;
    EEPROM.get(bootIdAddress, boot);
    boot = (boot == 0xffffffffUL) ? 1 : boot + 1;
    EEPROM.put(bootIdAddress, boot);
    telemetry.boot = boot;
    telemetry.synthetic = false;
    telemetry.base = basePWM;
    telemetry.kpMilli = lroundf(Kp * 1000.0f);
    telemetry.kiMilli = lroundf(Ki * 1000.0f);
    telemetry.kdMilli = lroundf(Kd * 1000.0f);
    // No confirmed line-loss/search events exist in this controller.
    telemetry.losses = 0;
    telemetry.lastLoss = 0;

}

void controlStep(unsigned long elapsed){
    // poll() owns the validated target; consume it at the next PID step only.
    basePWM = telemetry.base;
    float dt = elapsed/1000.0f;

    // Write the value of two sensors
    LeftVal = digitalRead(LeftSensor);
    RightVal = digitalRead(RightSensor);
    telemetry.left = LeftVal;
    telemetry.right = RightVal;
    float error = RightVal - LeftVal;
    telemetry.errorMilli = lroundf(error * 1000.0f);

    // The adapter latches this flag. Only an Arduino reset permits motion again.
    if(telemetryLink.stopRequested || (LeftVal == HIGH && RightVal == HIGH)){
        Stop();
        ResetPID();
        return;
    }

    // Quy uoc error
    // 0-0 -> 0
    // 1-0 -> -1: Turn Left
    // 0-1 -> +1: Turn Right

    float u = CaculatePID(error, dt);

    float maxCorrection = (basePWM < 255 - basePWM) ? basePWM : 255 - basePWM;

    u = constrain(u, -maxCorrection, maxCorrection);
    telemetry.pidMilli = lroundf(u * 1000.0f);

    int leftPWM = (int)(basePWM + u);
    int rightPWM = (int)(basePWM - u);

    setForwardPWM(leftPWM, rightPWM);
}

void loop(){
    unsigned long now = millis();
    unsigned long elapsed = now - lastControlMs;
    if(elapsed >= controlPeriodMs){
        lastControlMs = now;
        controlStep(elapsed);
    }

    // Service UART even between PID steps and while stopped. No blocking waits.
    telemetryLink.poll(telemetry, millis());
    if(telemetryLink.stopRequested){
        Stop();
        ResetPID();
    }
}

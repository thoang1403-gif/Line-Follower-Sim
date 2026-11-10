#include<stdio.h>
#include<Arduino.h>

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
}
// Set PID Gain

int basePWM = 70;
float Kp = 25.0f;
float Ki = 0.0f;
float Kp = 0.0f;

float integral = 0.0f;
float previousError = 0.0f;
bool hasPreviousError = false;

const unsigned long controlPeriodMs = 10;
unsigned long lastControlMs = 0;

void ResetPID() {
    integral = 0.0f;
    float previousError = 0.0f;
    bool hasPreviousError = false;
}

void setForwardPWM(int leftPWM, int rightPWM){
    leftPWM = constrain(leftPWM, 0, 255);
    rightPWM = constrain(rightPWM, 0, 255);

    digitalWrite(In1, LOW);
    digitalWrite(In2, HIGH);

    digitalWrite(In3, LOW);
    digitalWrite(In4, HIGH);

    analogWrite(ENA, leftPWM);
    analogWriet(ENB, rightPWM);


}

float CaculatePID(float error, float dt){
    float derivative = 0.0f;

    if(hasPreviousError){
        derivative = (error - previousError)/dt;
    }

    // Chua tich luy khi Ki = 0
    if(Ki != 0.0f){
        intergtal += error * dt;
        integral = constrain(intergtal, -5.0f, 5.0f);
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

}

void loop(){
    unsigned long now = millis();
    unsigned long elapsed = now - lastControlMs;

    if(elapsed < controlPeriodMs){
        return;
    }

    lastControlMs = now;
    float dt = elapsed/1000.0f;

    // Write the value of two sensors
    LeftVal = digitalRead(LeftSensor);
    RightVal = digitalRead(RightSensor);

    if( LeftVal == HIGH && RightVal == HIGH){
        Stop();
        ResetPID();
        return;
    }

    // Quy uoc error
    // 0-0 -> 0
    // 1-0 -> -1: Turn Left
    // 0-1 -> +1: Turn Right

    float error = RightVal - LeftVal;

    float u = CaculatePID(error, dt);

    float maxCorrection = (basePWM < 255 - basePWM) ? basePWM : 255 - basePWM;

    u = constrain(u, -maxCorrection, maxCorrection);

    int leftPWM = (int)(basePWM + u);
    int rightPWM = (int)(basePWM - u);

    setForwardPWM(leftPWM, rightPWM);
}
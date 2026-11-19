#include <wiringPi.h>

#define PWM0 18

void initMyTone(int gpio, int freq) {
    pinMode(gpio, PWM_OUTPUT);
    pwmSetMode(PWM_MODE_MS);
    pwmSetClock(19);                       
//    if (freq > 0) pwmSetRange(1000000 / freq);
    pwmWrite(gpio, 0);                      
}

void myTone(int gpio, int freq, int dur) {
    int range = 1000000 / freq;
    pwmSetRange(range);
    pwmWrite(gpio, range / 2);           
    delay(dur);
    pwmWrite(gpio, 0);                 
}

#define DO 262
#define RE 294
#define MI 330
#define UNIT 180   // 16분음표 = 180ms (4분음표 = 720ms), 클수록 느려짐

// 박자: 16분음표 개수 (1=16분, 2=8분, 3=점8분, 4=4분, 8=2분)
int melody[] = { MI,RE,DO,RE, MI,MI,MI, RE,RE,RE, MI,MI,MI,
                 MI,RE,DO,RE, MI,MI,MI, RE,RE,MI,RE, DO };
int beats[]  = { 3, 1, 2, 2,  2, 2, 4,  2, 2, 4,  2, 2, 4,
                 3, 1, 2, 2,  2, 2, 4,  2, 2, 3, 1, 8 };

int main() {
    int n = sizeof(melody) / sizeof(melody[0]);
    wiringPiSetupGpio();
    initMyTone(PWM0, 0);
    for (int i = 0; i < n; i++) {
        myTone(PWM0, melody[i], beats[i] * UNIT - 20);
        delay(20);   // 같은 음이 연속될 때 끊어서 들리게
    }
    return 0;
}

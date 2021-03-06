// Code to run on a attiny85 connected to a raspberry pi through i2c.
// The two main functions of this is to power off the raspberry pi during the day. This is done by the raspberry pi telling the attiny through i2c how long to be turned off for.
// The attiny then turns the pi off using a mosfet. And will power it back on when needed.
// The second function is to work as a WDT for the raspberry Pi when is is runnign through the nihgt. So if the pi doesn't reset the WDT the attiny will power off and on the pi.

#include <TinyWireS.h>
#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/wdt.h>

#define I2C_SLAVE_ADDRESS 0x04 // Address of the slave
#define POWER_LED 1
#define PI_POWER_PIN 3
#define PI_POWER_OFF_WAIT_TIME 1000 // 10 seconds. = 10/0.01 as each tick is 0.01 seconds.
#define MINUTE_COUNTDOWN 108 // = (60/0.5)*90% as each interupt occurs every 0.5 seconds. -10% because of inaccurate internal clock
#define PI_WDT_RESET_VAL 30000 // 5 minutes 100*60*5


int piPowerOffWait = PI_POWER_OFF_WAIT_TIME;
uint16_t piSleepTime = 0; // Counting down the time until the pi will be turned on in minutes. If this is 0 the pi will be powered on.
volatile int minuteCountdown = MINUTE_COUNTDOWN;
unsigned int piWDTCountdown = PI_WDT_RESET_VAL;

enum State {
  PI_POWERED,
  PI_POWER_OFF_WAIT,        // Wait 30 seconds before powering off the Pi
  PI_SLEEPING,              // RPi is sleeping, 
  PI_WDT_FAILED             // WDT for RPi failed. Turn off and on.
};

State state = PI_POWERED;

void setup() {
  pinMode(PI_POWER_PIN, OUTPUT);
  pinMode(POWER_LED, OUTPUT);
  digitalWrite(POWER_LED, LOW);
  initTimer1();
  sei(); // enable interupts
  TinyWireS.begin(I2C_SLAVE_ADDRESS);
  TinyWireS.onReceive(receiveEvent);
  TinyWireS.onRequest(requestEvent);
  blink(2);
  wdt_enable(WDTO_8S); // enable 8 second WDT
}

void loop() { 
  switch(state) {
    case PI_POWERED:
      digitalWrite(PI_POWER_PIN, HIGH);
      break;
    case PI_POWER_OFF_WAIT:
      set_timer_if_not_running(1000); // wait 10 seconds.
      if (timer_finished()) {
        state = PI_SLEEPING;
      }
      break;
    case PI_SLEEPING:
      digitalWrite(PI_POWER_PIN, LOW);
      if (piSleepTime == 0) {
        state = PI_POWERED;
      }
      break;
    case PI_WDT_FAILED:
      digitalWrite(PI_POWER_PIN, LOW);
      set_timer_if_not_running(1000); // wait 10 seconds.
      if (timer_finished()) {
        piWDTCountdown = PI_WDT_RESET_VAL;
        state = PI_POWERED;
        blink(3);
      }
      break;
  }
  
  if (piWDTCountdown <= 0) {
    state = PI_WDT_FAILED;
  }
  tws_delay(10);
  wdt_reset();
  TinyWireS_stop_check();
  update_power_led();
  timer_tick();
  pi_wdt_tick();
}

void pi_wdt_tick() {
  if (state != PI_POWERED) {
    return;
  }
  if (piWDTCountdown >= 1) {
    piWDTCountdown--;
  } else {
    state = PI_WDT_FAILED;
  }
}


int t = -1;  // -1 = no timer running, 0 = timer finished. After timer finished is checked it gets set to -1 again.

void set_timer_if_not_running(int x) {
  if (t == -1) {
    t = x;
  }
}

bool timer_finished() {
  if (t == 0) {
    t = -1;
    return true;
  } else {
    return false;
  }
}

void timer_tick() {
  if (t >=1) {
    t--;
  }
}

// Gets called when the ATtiny receives an i2c request
void requestEvent() {
  TinyWireS.send(0x03);
}

void receiveEvent(uint8_t howMany) {
  howMany--;
  byte h;
  byte l;
  switch(TinyWireS.receive()) {
    case 0x11:
      minuteCountdown = MINUTE_COUNTDOWN;
      h = TinyWireS.receive();
      howMany--;
      l = TinyWireS.receive();
      howMany--;
      cli();
      piSleepTime = (h << 8) + l;
      sei();
      state = PI_POWER_OFF_WAIT;
      blink(5);
      break;
    case 0x12:
      blink(1);
      piWDTCountdown = PI_WDT_RESET_VAL;
      break;
    default:
      blink(3);
      break;
  }
  while (howMany >= 1) {
    TinyWireS.receive();
  }
}

void blink(int x) {
  while(x--) {
    wdt_reset();
    digitalWrite(POWER_LED, LOW);
    tws_delay(200);
    digitalWrite(POWER_LED, HIGH);
    tws_delay(200);
    digitalWrite(POWER_LED, LOW);
    tws_delay(200);
  }
}

//========POWER PIN=========
// Slowly turns the LED on and off
int powerPinState = 0;
int powerPinIntensity = 0;

void update_power_led() {
  switch(powerPinState){
    case 0:
      powerPinIntensity++;
      if (powerPinIntensity >= 254) {
        powerPinState = 1;
      }
      break;
    case 1:
     powerPinIntensity--;
      if (powerPinIntensity <= 5) {
        powerPinState = 0;
      }
      break;
  }
  analogWrite(POWER_LED, powerPinIntensity);
}

//=======TIMER=======
// This timer interrupt is used to countdown the piSleepTime. 

void initTimer1() {
  TCCR1 |= (1 << CTC1);  // clear timer on compare match
  TCCR1 |= (1 << CS13) | (1 << CS12) | (1 << CS11) | (1 << CS10); //clock prescaler 16384
  OCR1C = 244; // 0.5*8MHz/16384=244.1 Will interrupt every 0.5 seconds
  TIMSK |= (1 << OCIE1A); // enable compare match interrupt
}

ISR(TIMER1_COMPA_vect) {
  if(minuteCountdown <= 0) {
    minuteCountdown = 120; // =60/0.5 because interrupt every 0.5 seconds
    if (piSleepTime > 0) {
      piSleepTime--;
    }
  } else {
    minuteCountdown--;
  }
}



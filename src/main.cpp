#include "FastInterruptEncoder.h"
#include "esp_event.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <cstdlib> // For abs()

// Define encoder (input) pins
#define ENC_1_PIN_A GPIO_NUM_4
#define ENC_1_PIN_B GPIO_NUM_5
#define ENC_2_PIN_A GPIO_NUM_6
#define ENC_2_PIN_B GPIO_NUM_7
// Define output pins
#define OUT_PIN_A GPIO_NUM_13
#define OUT_PIN_B GPIO_NUM_14

/* #define ENCODER_READ_DELAY    2 */
Encoder enc1(ENC_1_PIN_A, ENC_1_PIN_B, FULLQUAD, 250); // To change direction simply swap PIN_A and PIN_B
Encoder enc2(ENC_2_PIN_B, ENC_2_PIN_A, FULLQUAD, 250); 

// Define a custom event base and events
ESP_EVENT_DECLARE_BASE(EVENT_BASE);
ESP_EVENT_DEFINE_BASE(EVENT_BASE);

unsigned long encodertimer = 0;
int cntr_a = 0;       // counter for scale 1
int cntr_b = 0;       // counter for scale 2
int last_count = 0;   // cntr_a + cntr_b
int diff = 0;         // difference between currently read change (cntr_a + cntr_b) and last read change (both scales)
int current = 0;      // current position in gray code (0-3)
int next = 0;         // next position in gray code (0-3)
bool dir = false;     // true = increment "up"; false = decrement "down"

hw_timer_t * timer = NULL;

void IRAM_ATTR Update_IT_callback()
{ 
  enc1.loop(); 
  enc2.loop(); 
}

// Note - quadrature output to DRO
//
// I. Quadrature signal is represented as 2 bit gray code *not* binary count thus 
// (dir==true) logic oscillates between OUT_PIN_A and OUT_PIN_B as only one pin per 
// increment/decrement ever needs to be toggled. 
//
// II. GPIO.outw1tc (clear, sets pin low) and GPIO.outw1ts (set, sets pin high) is *much* 
// faster than gpio_set_level() or digitalWrite()

static void my_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  if (event_base == EVENT_BASE) {
    switch (event_id) {
      case 0:
        //ESP_LOGI("EVENT", "Gray 1 00");
        if (dir == true) {GPIO.out_w1tc = ((uint32_t)1 << OUT_PIN_A ); } else { GPIO.out_w1tc = ((uint32_t)1 << OUT_PIN_B ); }
        break;
      case 1:
        //ESP_LOGI("EVENT", "Gray 2 01");
        if (dir == true) {GPIO.out_w1ts = ((uint32_t)1 << OUT_PIN_B ); } else { GPIO.out_w1tc = ((uint32_t)1 << OUT_PIN_A ); }
        break;
      case 2:
        //ESP_LOGI("EVENT", "Gray 3 11");
        if (dir == true) {GPIO.out_w1ts = ((uint32_t)1 << OUT_PIN_A ); } else { GPIO.out_w1ts = ((uint32_t)1 << OUT_PIN_B ); }
        break;
      case 3:
        //ESP_LOGI("EVENT", "Gray 4 10");
        if (dir == true) {GPIO.out_w1tc = ((uint32_t)1 << OUT_PIN_B ); } else { GPIO.out_w1ts = ((uint32_t)1 << OUT_PIN_A ); }
        break;
      default:
        ESP_LOGW("EVENT", "Received unknown event ID: %ld", event_id);
        break;
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  if (enc1.init(0)) {
    Serial.println("Encoder 1 Initialization OK");
  } else {
    Serial.println("Encoder 1 Initialization Failed");
    while(1);
  }

  if (enc2.init(1)) {
    Serial.println("Encoder 2 Initialization OK");
  } else {
    Serial.println("Encoder 2 Initialization Failed");
    while(1);
  }

  /* Use 1st timer of 4 */
  /* 1 tick take 1/(80MHZ/80) = 1us so we set divider 80 and count up */
  timer = timerBegin(0, 80, true);
  /* Attach onTimer function to our timer */
  timerAttachInterrupt(timer, &Update_IT_callback, true);
  /* Set alarm to call onTimer function every 100 ms -> 100 Hz */
  timerAlarmWrite(timer, 10000, true);
  /* Start an alarm */
  timerAlarmEnable(timer);  

  // Initialize output pins
  gpio_set_direction(OUT_PIN_A, GPIO_MODE_OUTPUT);
  gpio_set_direction(OUT_PIN_B, GPIO_MODE_OUTPUT);
  GPIO.out_w1tc = ((uint32_t)1 << OUT_PIN_A );
  GPIO.out_w1tc = ((uint32_t)1 << OUT_PIN_B );

  // Initialize event loop
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Register event handler
  ESP_ERROR_CHECK(esp_event_handler_register(EVENT_BASE, ESP_EVENT_ANY_ID, &my_event_handler, NULL));


}

void loop() {

  if ((millis() > (encodertimer + 10)) || (millis() < encodertimer)) {
    cntr_a = enc1.getTicks();
    cntr_b = enc2.getTicks();
    diff = (cntr_a + cntr_b) - last_count;
    last_count = (cntr_a + cntr_b);
    encodertimer = millis();

    // Set direction and get absolute value for number of ticks
    if (diff < 0) {
      dir = false;
      diff = abs(diff);
    } else {
      dir = true;
    }

    // Send it!!
    for ( int cnt = 0; cnt < diff; cnt++ ){
      if (dir == true){
        if (current == 3) { next = 0; } else { next = (current + 1); };
        /*Serial.print(current);
        Serial.print('\t');
        Serial.print(next);
        Serial.print('\t');
        Serial.println("up");*/
        esp_event_post(EVENT_BASE, next, NULL, 0, portMAX_DELAY);
        current = next;
      } else {
      if (current == 0) { next = 3; } else { next = (current - 1); };
        /*Serial.print(current);
        Serial.print('\t');
        Serial.print(next);
        Serial.print('\t');  
        Serial.println("down");*/
        esp_event_post(EVENT_BASE, next, NULL, 0, portMAX_DELAY);
        current = next;
      }
    }
  }
}

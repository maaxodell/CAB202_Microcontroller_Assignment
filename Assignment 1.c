#include <avr/io.h>
#include <avr/interrupt.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <util/delay.h>

// Setting, clearing, and reading bits in registers - (WRITE_BIT is a combination of CLEAR_BIT & SET_BIT)
// Source: Lawrence Buckingham in CAB202 materials
#define SET_BIT(reg, pin)		    (reg) |= (1 << (pin))
#define CLEAR_BIT(reg, pin)		    (reg) &= ~(1 << (pin))
#define WRITE_BIT(reg, pin, value)  (reg) = (((reg) & ~(1 << (pin))) | ((value) << (pin)))
#define BIT_VALUE(reg, pin)		    (((reg) >> (pin)) & 1)
#define BIT_IS_SET(reg, pin)	    (BIT_VALUE((reg),(pin))==1)


// * LCD DEFINITONS - Source: Lawrence Buckingham in CAB202 materials * //

// 4-pin mode
#define LCD_USING_4PIN_MODE (1)

// data port definitions
#define LCD_DATA4_DDR (DDRC)
#define LCD_DATA5_DDR (DDRC)
#define LCD_DATA6_DDR (DDRC)
#define LCD_DATA7_DDR (DDRC)
#define LCD_DATA4_PORT (PORTC)
#define LCD_DATA5_PORT (PORTC)
#define LCD_DATA6_PORT (PORTC)
#define LCD_DATA7_PORT (PORTC)
#define LCD_DATA4_PIN (2)
#define LCD_DATA5_PIN (3)
#define LCD_DATA6_PIN (4)
#define LCD_DATA7_PIN (5)

// RS and enable port definitions
#define LCD_RS_DDR (DDRC)
#define LCD_ENABLE_DDR (DDRC)
#define LCD_RS_PORT (PORTC)
#define LCD_ENABLE_PORT (PORTC)
#define LCD_RS_PIN (0)
#define LCD_ENABLE_PIN (1)

// commands
#define LCD_CLEARDISPLAY 0x01
#define LCD_RETURNHOME 0x02
#define LCD_ENTRYMODESET 0x04
#define LCD_DISPLAYCONTROL 0x08
#define LCD_CURSORSHIFT 0x10
#define LCD_FUNCTIONSET 0x20
#define LCD_SETCGRAMADDR 0x40
#define LCD_SETDDRAMADDR 0x80

// flags for display entry mode
#define LCD_ENTRYRIGHT 0x00
#define LCD_ENTRYLEFT 0x02
#define LCD_ENTRYSHIFTINCREMENT 0x01
#define LCD_ENTRYSHIFTDECREMENT 0x00

// flags for display on/off control
#define LCD_DISPLAYON 0x04
#define LCD_DISPLAYOFF 0x00
#define LCD_CURSORON 0x02
#define LCD_CURSOROFF 0x00
#define LCD_BLINKON 0x01
#define LCD_BLINKOFF 0x00

// flags for display/cursor shift
#define LCD_DISPLAYMOVE 0x08
#define LCD_CURSORMOVE 0x00
#define LCD_MOVERIGHT 0x04
#define LCD_MOVELEFT 0x00

// flags for function set
#define LCD_8BITMODE 0x10
#define LCD_4BITMODE 0x00
#define LCD_2LINE 0x08
#define LCD_1LINE 0x00
#define LCD_5x10DOTS 0x04
#define LCD_5x8DOTS 0x00

// functions
void lcd_init(void);
void lcd_write_string(uint8_t x, uint8_t y, char string[]);
void lcd_write_char(uint8_t x, uint8_t y, char val);
void lcd_clear(void);
void lcd_home(void);
void lcd_createChar(uint8_t, uint8_t[]);
void lcd_setCursor(uint8_t, uint8_t); 
void lcd_noDisplay(void);
void lcd_display(void);
void lcd_noBlink(void);
void lcd_blink(void);
void lcd_noCursor(void);
void lcd_cursor(void);
void lcd_leftToRight(void);
void lcd_rightToLeft(void);
void lcd_autoscroll(void);
void lcd_noAutoscroll(void);
void scrollDisplayLeft(void);
void scrollDisplayRight(void);
size_t lcd_write(uint8_t);
void lcd_command(uint8_t);
void lcd_send(uint8_t, uint8_t);
void lcd_write4bits(uint8_t);
void lcd_write8bits(uint8_t);
void lcd_pulseEnable(void);
uint8_t _lcd_displayfunction;
uint8_t _lcd_displaycontrol;
uint8_t _lcd_displaymode;

// * END LCD DEFINITIONS * //


// function declarations
void uart_setup(unsigned int ubrr);
void pin_setup();
void interrupt_setup();
void lcd_setup();
void modify_string(char line1input[], char line2input[]);
void insert_char(int line, int pos, char input);
void clear_string();
void display(char lcd_line1[], char lcd_line2[]);
unsigned char uart_getchar(void);
void uart_printchar(unsigned char data);
void uart_printstring(char str[]);
void timer_setup();
void locked_display();
void enable();
void ftoa(float n, char * res, int afterpoint);

// uart definitions
#define BAUD 9600
#define MYUBRR F_CPU/16/BAUD-1

// timer definitions
#define FREQ (16000000.0)
#define PRESCALE (1024.0)

// global variables
char company_name[] = "O'DELL SECURITY";
int code[4];
int try_code[4];
int digits_pressed;
bool locked = false;
bool unlocked = false;
bool disabled = false;
int unlock_attempts = 3;

// for use with the timer interrupts
volatile int timer_overflow;

// two lines to be displayed on the lcd screen
char display_line1[16];
char display_line2[16];

void master_setup(void) {
    // setup I/O registers
    pin_setup();

    // setup uart communication
    uart_setup(MYUBRR);

    // setup interrupt registers
    interrupt_setup();

    // setup LCD display
    lcd_setup();

    // print startup message
    uart_printstring("// O'DELL SECURITY //\nSet your 4-digit code: ");  
}

void process(void) {
    // always send the two global lines to the lcd screen
    display(display_line1, display_line2);

    if (disabled) {
        // disable the safe for 1 minute after 3 incorrect attempts
        TCCR0A = 0;
	    TCCR0B = 5;

        char t[64];
        double elapsed = (timer_overflow * 256.0 + TCNT0) * PRESCALE / FREQ;
        double remaining = (61.0 - elapsed);
        ftoa(remaining, t, 0);

        lcd_write_string(14, 2, t);

        if (remaining < 1) enable();
    }

    if (locked) {
        // turn on red led and turn off green
        SET_BIT(PORTD, 3);
        CLEAR_BIT(PORTD, 2);
    } else if (unlocked) {
        // turn on green led and turn off red
        SET_BIT(PORTD, 2);
        CLEAR_BIT(PORTD, 3);
    }
}

int main(void) {
    // run the setup function
    master_setup();

    // infinite loop of process function
    for ( ;; ) {
        process();
    }
}


// setup functions
void uart_setup(unsigned int ubrr) {
	// setup all UART registers
    UBRR0H = (unsigned char)(ubrr>>8);
    UBRR0L = (unsigned char)(ubrr);
	UCSR0B = (1 << RXEN0) | (1 << TXEN0);
	UCSR0C = (3 << UCSZ00);
}

void pin_setup() {
    // setup button DDR registers
    CLEAR_BIT(DDRD, 4);
    CLEAR_BIT(DDRD, 5);
    CLEAR_BIT(DDRD, 6);
    CLEAR_BIT(DDRD, 7);
    CLEAR_BIT(DDRB, 0);
    CLEAR_BIT(DDRB, 1);
    CLEAR_BIT(DDRB, 2);
    CLEAR_BIT(DDRB, 3);
    CLEAR_BIT(DDRB, 4);
    CLEAR_BIT(DDRB, 5);

    // setup LED DDR registers
    SET_BIT(DDRD, 2);
    SET_BIT(DDRD, 3);
}

void interrupt_setup() {
    // global interrupts
    sei();
    
    // pinchange interrupts
    PCICR = (1 << PCIE0) | (1 << PCIE2);
    PCMSK0 = (1 << PCINT0) | (1 << PCINT1) | (1 << PCINT2) | (1 << PCINT3) | (1 << PCINT4) | (1 << PCINT5);
    PCMSK2 = (1 << PCINT20) | (1 << PCINT21) | (1 << PCINT22) | (1 << PCINT23);
    
    // timer interrupt
    TIMSK0 = 1;
}

void lcd_setup() {
    // setup lcd and display startup message
    lcd_init();
    modify_string(company_name, "Set Code:");
}


// auxiliary functions
void modify_string(char line1input[], char line2input[]) {
    // change the lcd display strings
    for (int i = 0; i < strlen(line1input); i++) {
        display_line1[i] = line1input[i];
    }

    for (int q = 0; q < strlen(line2input); q++) {
        display_line2[q] = line2input[q];
    }
}

void insert_char(int line, int pos, char input) {
    // insert a character into a lcd display string
    if (line == 1) {
        display_line1[pos] = input;
    } else if (line == 2) {
        display_line2[pos] = input;
    }
}

void clear_string() {
    // fill both strings with whitespace (avoids leftover characters from the previous string being displayed)
    for (int i = 0; i < 16; i++) {
        display_line1[i] = ' ';
        display_line2[i] = ' ';
    }
}

void display(char lcd_line1[], char lcd_line2[]) {
    // write both strings to the lcd
    lcd_write_string(0, 0, lcd_line1);
    lcd_write_string(0, 1, lcd_line2);
}


// uart functions
void uart_printchar(unsigned char character) {
    // Source: Lawrence Buckingham in CAB202 materials

    // wait for empty
    while (!(UCSR0A & (1<<UDRE0)));
    
    // send data
    UDR0 = character; 	
}

void uart_printstring(char str[]) {
    // Source: Lawrence Buckingham in CAB202 materials
    int i = 0;
    
    // send characters one by one
    while (str[i] != 0) {
        uart_printchar(str[i]);
        i++;
    }

    // signal end of string
    uart_printchar(0);
}


// codes and comparison
void code_add(int number) {
    // update passcode and increment control variable
    code[digits_pressed] = number;
    digits_pressed++;
}

void try_code_add(int number) {
    // update the attempt code and increment control variable
    try_code[digits_pressed] = number;
    digits_pressed++;
}

bool codes_match() {
    // compare the entered code with the correct one
    digits_pressed = 0;
    
    for (int i = 0; i < 4; i++) {
        if (try_code[i] != code[i]) {
            return false;
        }
    }

    // return true if no differences
    return true;
}


// safe functions
void locked_display() {
    // convert number of attempts to character for lcd display
    int attempt_int = unlock_attempts;
    char attempt_char[1];
    sprintf(attempt_char, "%d", attempt_int);
    
    // reset string
    clear_string();
    
    // display attempts and enter prompt, taking (s) into account
    if (unlock_attempts == 1) {
        modify_string("  Attempt Left", "Enter Code:");
    } else
    {
        modify_string("  Attempts Left", "Enter Code:");
    }
    insert_char(1, 0, attempt_char[0]);
}

void lock_safe() {
    // sets all variables appropriately when locked
    locked = true;
    digits_pressed = 0;

    // print to UART
    uart_printstring("\n\nCode Set - Safe Locked.");
    uart_printstring("\nEnter Code: ");
    
    // send to LCD
    lcd_clear();
    clear_string();
    modify_string(company_name, "Enter Code:");
}


// access
void access_granted() {
    // sets all variables appropriately when successfully unlocked
    digits_pressed = 0;
    unlocked = true;
    locked = false;
    
    // reset attempt code
    memset(try_code, 0, 4);
    
    // UART and LCD
    uart_printstring("\nCorrect Code // Access Granted");
    clear_string();
    modify_string("Correct Code", "Access Granted");
}

void access_denied() {
    // sets all variables appropriately when incorrect code entered
    digits_pressed = 0;
    unlock_attempts--;

    // reset attempt code
    memset(try_code, 0, 4);
    
    // UART and LCD
    uart_printstring("\nIncorrect Code // Access Denied");
    uart_printstring("\nEnter Code: ");
    locked_display();
}

void disable() {
    // sets all variables appropriately when safe disabled
    timer_overflow = 0;
    disabled = true;

    // UART and LCD
    uart_printstring("\n\nToo many attempts. Safe temporarily disabled.");
    clear_string();
    modify_string("Safe Disabled.", "Try again in:");
}

void enable() {
    // sets all variables appropriately when safe re-enabled
    disabled = false;
    unlock_attempts = 3;
    digits_pressed = 0;

    // UART and LCD
    lcd_clear();
    clear_string();
    uart_printstring("\n\nSafe enabled. Enter Code: ");
    modify_string("3 Attempts Left", "Enter Code:");
}


// button press handler
void handle_press(int button_pressed) {
    
    if (!locked && !disabled) {
        // append the passcode
        code_add(button_pressed);

        // print using uart
        char num[1];
        sprintf(num, "%d", button_pressed);
        uart_printchar((unsigned char)num[0]);
        
        // display on LCD (hidden)
        if (digits_pressed == 1) insert_char(2, 9, ' ');
        insert_char(2, 9 + digits_pressed, '*');

    } else if (locked && !disabled) {
        // append the attempt passcode
        try_code_add(button_pressed);
        
        // print using uart
        char try_num[1];
        sprintf(try_num, "%d", button_pressed);
        uart_printchar((unsigned char)try_num[0]);

        // display on LCD (hidden)
        if (digits_pressed == 1) insert_char(2, 11, ' ');
        insert_char(2, 11 + digits_pressed, '*');
        
    }

    if (digits_pressed == 4) {
        if (!locked) {
            lock_safe();
        } else if (locked) {
            if (unlock_attempts == 1) {
                disable();
            } else {
                if (codes_match() && !disabled) {
                    access_granted();
                } else if (!codes_match() && !disabled) {
                    access_denied();
                }  
            }
        } 
    }
}


// interrupt service routines
ISR(PCINT0_vect) {
    // PORT B buttons
    if (BIT_IS_SET(PINB, 5)) handle_press(1);
    if (BIT_IS_SET(PINB, 4)) handle_press(2);
    if (BIT_IS_SET(PINB, 3)) handle_press(3);
    if (BIT_IS_SET(PINB, 2)) handle_press(4);
    if (BIT_IS_SET(PINB, 1)) handle_press(5);
    if (BIT_IS_SET(PINB, 0)) handle_press(6);
}

ISR(PCINT2_vect) {
    // PORT D buttons
    if (BIT_IS_SET(PIND, 7)) handle_press(7);
    if (BIT_IS_SET(PIND, 6)) handle_press(8);
    if (BIT_IS_SET(PIND, 5)) handle_press(9);
    if (BIT_IS_SET(PIND, 4)) handle_press(0);
}

ISR(TIMER0_OVF_vect) {
	// timer
    timer_overflow++;
}


// * TIMER FUNCTIONS - Source: Lawrence Buckingham in CAB202 materials * //

// Reverses a string 'str' of length 'len' 
void reverse(char * str, int len) {
  int i = 0, j = len - 1, temp;
  while (i < j) {
    temp = str[i];
    str[i] = str[j];
    str[j] = temp;
    i++;
    j--;
  }
}

// Converts a given integer x to string str[].
int intToStr(int x, char str[], int d) {
  int i = 0;
  while (x) {
    str[i++] = (x % 10) + '0';
    x = x / 10;
  }

  // If number of digits required is more, then 
  // add 0s at the beginning 
  while (i < d)
    str[i++] = '0';

  reverse(str, i);
  str[i] = '\0';
  return i;
}

// Converts a floating-point/double number to a string. 
void ftoa(float n, char * res, int afterpoint) {
  // Extract integer part 
  int ipart = (int) n;

  // Extract floating part 
  float fpart = n - (float) ipart;

  // convert integer part to string 
  int i = intToStr(ipart, res, 0);

  // check for display option after point 
  if (afterpoint != 0) {
    res[i] = '.'; // add dot 

    // Get the value of fraction part upto given no. 
    // of points after dot. The third parameter  
    // is needed to handle cases like 233.007 
    fpart = fpart * pow(10, afterpoint);

    intToStr((int) fpart, res + i + 1, afterpoint);
  }
}

// * END TIMER FUCNTIONS * //


// * LCD FUNCTIONS - Source: Lawrence Buckingham in CAB202 materials * //

void lcd_init(void){
  //dotsize
  if (LCD_USING_4PIN_MODE){
    _lcd_displayfunction = LCD_4BITMODE | LCD_1LINE | LCD_5x8DOTS;
  } else {
    _lcd_displayfunction = LCD_8BITMODE | LCD_1LINE | LCD_5x8DOTS;
  }
  
  _lcd_displayfunction |= LCD_2LINE;

  // RS Pin
  LCD_RS_DDR |= (1 << LCD_RS_PIN);
  // Enable Pin
  LCD_ENABLE_DDR |= (1 << LCD_ENABLE_PIN);
  
  #if LCD_USING_4PIN_MODE
    //Set DDR for all the data pins
    LCD_DATA4_DDR |= (1 << LCD_DATA4_PIN);
    LCD_DATA5_DDR |= (1 << LCD_DATA5_PIN);
    LCD_DATA6_DDR |= (1 << LCD_DATA6_PIN);    
    LCD_DATA7_DDR |= (1 << LCD_DATA7_PIN);

  #else
    //Set DDR for all the data pins
    LCD_DATA0_DDR |= (1 << LCD_DATA0_PIN);
    LCD_DATA1_DDR |= (1 << LCD_DATA1_PIN);
    LCD_DATA2_DDR |= (1 << LCD_DATA2_PIN);
    LCD_DATA3_DDR |= (1 << LCD_DATA3_PIN);
    LCD_DATA4_DDR |= (1 << LCD_DATA4_PIN);
    LCD_DATA5_DDR |= (1 << LCD_DATA5_PIN);
    LCD_DATA6_DDR |= (1 << LCD_DATA6_PIN);
    LCD_DATA7_DDR |= (1 << LCD_DATA7_PIN);
  #endif 

  // SEE PAGE 45/46 OF Hitachi HD44780 DATASHEET FOR INITIALIZATION SPECIFICATION!

  // according to datasheet, we need at least 40ms after power rises above 2.7V
  // before sending commands. Arduino can turn on way before 4.5V so we'll wait 50
  _delay_us(50000); 
  // Now we pull both RS and Enable low to begin commands (R/W is wired to ground)
  LCD_RS_PORT &= ~(1 << LCD_RS_PIN);
  LCD_ENABLE_PORT &= ~(1 << LCD_ENABLE_PIN);
  
  //put the LCD into 4 bit or 8 bit mode
  if (LCD_USING_4PIN_MODE) {
    // this is according to the hitachi HD44780 datasheet
    // figure 24, pg 46

    // we start in 8bit mode, try to set 4 bit mode
    lcd_write4bits(0b0111);
    _delay_us(4500); // wait min 4.1ms

    // second try
    lcd_write4bits(0b0111);
    _delay_us(4500); // wait min 4.1ms
    
    // third go!
    lcd_write4bits(0b0111); 
    _delay_us(150);

    // finally, set to 4-bit interface
    lcd_write4bits(0b0010); 
  } else {
    // this is according to the hitachi HD44780 datasheet
    // page 45 figure 23

    // Send function set command sequence
    lcd_command(LCD_FUNCTIONSET | _lcd_displayfunction);
    _delay_us(4500);  // wait more than 4.1ms

    // second try
    lcd_command(LCD_FUNCTIONSET | _lcd_displayfunction);
    _delay_us(150);

    // third go
    lcd_command(LCD_FUNCTIONSET | _lcd_displayfunction);
  }

  // finally, set # lines, font size, etc.
  lcd_command(LCD_FUNCTIONSET | _lcd_displayfunction);  

  // turn the display on with no cursor or blinking default
  _lcd_displaycontrol = LCD_DISPLAYON | LCD_CURSOROFF | LCD_BLINKOFF;  
  lcd_display();

  // clear it off
  lcd_clear();

  // Initialize to default text direction (for romance languages)
  _lcd_displaymode = LCD_ENTRYLEFT | LCD_ENTRYSHIFTDECREMENT;
  // set the entry mode
  lcd_command(LCD_ENTRYMODESET | _lcd_displaymode);
}

/********** high level commands, for the user! */
void lcd_write_string(uint8_t x, uint8_t y, char string[]){
  lcd_setCursor(x,y);
  for(int i=0; string[i]!='\0'; ++i){
    lcd_write(string[i]);
  }
}

void lcd_write_char(uint8_t x, uint8_t y, char val){
  lcd_setCursor(x,y);
  lcd_write(val);
}

void lcd_clear(void){
  lcd_command(LCD_CLEARDISPLAY);  // clear display, set cursor position to zero
  _delay_us(2000);  // this command takes a long time!
}

void lcd_home(void){
  lcd_command(LCD_RETURNHOME);  // set cursor position to zero
  _delay_us(2000);  // this command takes a long time!
}

// Allows us to fill the first 8 CGRAM locations
// with custom characters
void lcd_createChar(uint8_t location, uint8_t charmap[]) {
  location &= 0x7; // we only have 8 locations 0-7
  lcd_command(LCD_SETCGRAMADDR | (location << 3));
  for (int i=0; i<8; i++) {
    lcd_write(charmap[i]);
  }
}

void lcd_setCursor(uint8_t col, uint8_t row){
  if ( row >= 2 ) {
    row = 1;
  }
  
  lcd_command(LCD_SETDDRAMADDR | (col + row*0x40));
}

// Turn the display on/off (quickly)
void lcd_noDisplay(void) {
  _lcd_displaycontrol &= ~LCD_DISPLAYON;
  lcd_command(LCD_DISPLAYCONTROL | _lcd_displaycontrol);
}

void lcd_display(void) {
  _lcd_displaycontrol |= LCD_DISPLAYON;
  lcd_command(LCD_DISPLAYCONTROL | _lcd_displaycontrol);
}

// Turns the underline cursor on/off
void lcd_noCursor(void) {
  _lcd_displaycontrol &= ~LCD_CURSORON;
  lcd_command(LCD_DISPLAYCONTROL | _lcd_displaycontrol);
}

void lcd_cursor(void) {
  _lcd_displaycontrol |= LCD_CURSORON;
  lcd_command(LCD_DISPLAYCONTROL | _lcd_displaycontrol);
}

// Turn on and off the blinking cursor
void lcd_noBlink(void) {
  _lcd_displaycontrol &= ~LCD_BLINKON;
  lcd_command(LCD_DISPLAYCONTROL | _lcd_displaycontrol);
}

void lcd_blink(void) {
  _lcd_displaycontrol |= LCD_BLINKON;
  lcd_command(LCD_DISPLAYCONTROL | _lcd_displaycontrol);
}

// These commands scroll the display without changing the RAM
void scrollDisplayLeft(void) {
  lcd_command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVELEFT);
}

void scrollDisplayRight(void) {
  lcd_command(LCD_CURSORSHIFT | LCD_DISPLAYMOVE | LCD_MOVERIGHT);
}

// This is for text that flows Left to Right
void lcd_leftToRight(void) {
  _lcd_displaymode |= LCD_ENTRYLEFT;
  lcd_command(LCD_ENTRYMODESET | _lcd_displaymode);
}

// This is for text that flows Right to Left
void lcd_rightToLeft(void) {
  _lcd_displaymode &= ~LCD_ENTRYLEFT;
  lcd_command(LCD_ENTRYMODESET | _lcd_displaymode);
}

// This will 'right justify' text from the cursor
void lcd_autoscroll(void) {
  _lcd_displaymode |= LCD_ENTRYSHIFTINCREMENT;
  lcd_command(LCD_ENTRYMODESET | _lcd_displaymode);
}

// This will 'left justify' text from the cursor
void lcd_noAutoscroll(void) {
  _lcd_displaymode &= ~LCD_ENTRYSHIFTINCREMENT;
  lcd_command(LCD_ENTRYMODESET | _lcd_displaymode);
}

/*********** mid level commands, for sending data/cmds */

inline void lcd_command(uint8_t value) {
  //
  lcd_send(value, 0);
}

inline size_t lcd_write(uint8_t value) {
  lcd_send(value, 1);
  return 1; // assume sucess
}

/************ low level data pushing commands **********/

// write either command or data, with automatic 4/8-bit selection
void lcd_send(uint8_t value, uint8_t mode) {
  //RS Pin
  LCD_RS_PORT &= ~(1 << LCD_RS_PIN);
  LCD_RS_PORT |= (!!mode << LCD_RS_PIN);

  if (LCD_USING_4PIN_MODE) {
    lcd_write4bits(value>>4);
    lcd_write4bits(value);
  } else {
    lcd_write8bits(value); 
  } 
}

void lcd_pulseEnable(void) {
  //Enable Pin
  LCD_ENABLE_PORT &= ~(1 << LCD_ENABLE_PIN);
  _delay_us(1);    
  LCD_ENABLE_PORT |= (1 << LCD_ENABLE_PIN);
  _delay_us(1);    // enable pulse must be >450ns
  LCD_ENABLE_PORT &= ~(1 << LCD_ENABLE_PIN);
  _delay_us(100);   // commands need > 37us to settle
}

void lcd_write4bits(uint8_t value) {
  //Set each wire one at a time

  LCD_DATA4_PORT &= ~(1 << LCD_DATA4_PIN);
  LCD_DATA4_PORT |= ((value & 1) << LCD_DATA4_PIN);
  value >>= 1;

  LCD_DATA5_PORT &= ~(1 << LCD_DATA5_PIN);
  LCD_DATA5_PORT |= ((value & 1) << LCD_DATA5_PIN);
  value >>= 1;

  LCD_DATA6_PORT &= ~(1 << LCD_DATA6_PIN);
  LCD_DATA6_PORT |= ((value & 1) << LCD_DATA6_PIN);
  value >>= 1;

  LCD_DATA7_PORT &= ~(1 << LCD_DATA7_PIN);
  LCD_DATA7_PORT |= ((value & 1) << LCD_DATA7_PIN);

  lcd_pulseEnable();
}

void lcd_write8bits(uint8_t value) {
  //Set each wire one at a time

  #if !LCD_USING_4PIN_MODE
    LCD_DATA0_PORT &= ~(1 << LCD_DATA0_PIN);
    LCD_DATA0_PORT |= ((value & 1) << LCD_DATA0_PIN);
    value >>= 1;

    LCD_DATA1_PORT &= ~(1 << LCD_DATA1_PIN);
    LCD_DATA1_PORT |= ((value & 1) << LCD_DATA1_PIN);
    value >>= 1;

    LCD_DATA2_PORT &= ~(1 << LCD_DATA2_PIN);
    LCD_DATA2_PORT |= ((value & 1) << LCD_DATA2_PIN);
    value >>= 1;

    LCD_DATA3_PORT &= ~(1 << LCD_DATA3_PIN);
    LCD_DATA3_PORT |= ((value & 1) << LCD_DATA3_PIN);
    value >>= 1;

    LCD_DATA4_PORT &= ~(1 << LCD_DATA4_PIN);
    LCD_DATA4_PORT |= ((value & 1) << LCD_DATA4_PIN);
    value >>= 1;

    LCD_DATA5_PORT &= ~(1 << LCD_DATA5_PIN);
    LCD_DATA5_PORT |= ((value & 1) << LCD_DATA5_PIN);
    value >>= 1;

    LCD_DATA6_PORT &= ~(1 << LCD_DATA6_PIN);
    LCD_DATA6_PORT |= ((value & 1) << LCD_DATA6_PIN);
    value >>= 1;

    LCD_DATA7_PORT &= ~(1 << LCD_DATA7_PIN);
    LCD_DATA7_PORT |= ((value & 1) << LCD_DATA7_PIN);
    
    lcd_pulseEnable();
  #endif
}
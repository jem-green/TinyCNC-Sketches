/*
  Mini CNC Plotter firmware, based in TinyCNC https://github.com/MakerBlock/TinyCNC-Sketches
  Send GCODE to this Sketch using gctrl.pde https://github.com/damellis/gctrl
  Convert SVG to GCODE with MakerBot Unicorn plugin for Inkscape available here https://github.com/martymcguire/inkscape-unicorn

  More information about the Mini CNC Plotter here (german, sorry): http://www.makerblog.at/2015/02/projekt-mini-cnc-plotter-aus-alten-cddvd-laufwerken/
  
  https://forum.arduino.cc/index.php?topic=491579.0
  
*/

#include <Servo.h>
#include <Stepper.h>

#define LINE_BUFFER_LENGTH 512

// Servo position for Up and Down
const int penZUp = 80;
const int penZDown = 40;

// Servo on PWM pin 11 for CNC Shield
const int penServoPin = 11;

// Should be right for DVD steppers, but is not too important here
const int stepsPerRevolution = 20;

// create servo object to control a servo
Servo penServo;

// Initialize steppers for X- and Y-axis using this Arduino pins for the CNC Shield
// From the pinout diagram
// S: Enable = 11
// X: Step = 2, Direction = 5
// Y: Step = 3, Direction = 6
// Z: Step = 4, Direction = 7

Stepper myStepperY(stepsPerRevolution, 2, 5, 8);
Stepper myStepperX(stepsPerRevolution, 3, 6, 8);

/* Structures, global variables    */
struct point {
  float x;
  float y;
  float z;
};

// Current position of plothead
struct point actuatorPos;

//  Drawing settings, should be OK
float StepInc = 1;
int StepDelay = 0;
int LineDelay = 50;
int penDelay = 50;
int StepXSpeed = 250;
int StepYSpeed = 250;

// Motor steps to go 1 millimeter.
// Use test sketch to go 100 steps. Measure the length of line.
// Calculate steps per mm. Enter here.
float StepsPerMillimeterX = 32.0;
float StepsPerMillimeterY = 32.0;
float StepsPerMillimeterZ = 1.0;  // Might be different scalling

// Drawing robot limits, in mm
// OK to start with. Could go up to 50 mm if calibrated well.
float Xmin = 0;
float Xmax = 40;
float Ymin = 0;
float Ymax = 40;
float Zmin = 0;
float Zmax = 1;

// Start at the bottom left corner with pen up
float Xpos = Xmin;
float Ypos = Ymin;
float Zpos = Zmax;

// Set to true to get debug output.
boolean verbose = true;

//  Needs to interpret
//  G1 for moving
//  G4 P300 (wait 150ms)
//  M300 S30 (pen down)
//  M300 S50 (pen up)
//  Discard anything with a (
//  Discard any other command!

/**********************
   void setup() - Initialisations
 ***********************/
void setup() {
  //  Setup
  Serial.begin( 9600 );

  penServo.attach(penServoPin);
  penServo.write(penZUp);
  Delay(200);

  // Decrease if necessary
  myStepperX.setSpeed(StepXSpeed);
  myStepperY.setSpeed(StepYSpeed);

  //  Set & move to initial default position
  // TBD

  //  Notifications!!!
  Serial.println("Mini CNC Plotter alive and kicking!");
  Serial.print("X range is from ");
  Serial.print(Xmin);
  Serial.print(" to ");
  Serial.print(Xmax);
  Serial.println(" mm.");
  Serial.print("Y range is from ");
  Serial.print(Ymin);
  Serial.print(" to ");
  Serial.print(Ymax);
  Serial.println(" mm.");

  // Disable Stepper to reduce heat
  StepperDisable();
}

/**********************
   void loop() - Main loop
 ***********************/
void loop()
{
  Delay(200);
  char line[ LINE_BUFFER_LENGTH ];
  char c;
  int lineIndex;
  bool lineIsComment, lineSemiColon;

  lineIndex = 0;
  lineSemiColon = false;
  lineIsComment = false;

  while (1) {

    // Serial reception - Mostly from Grbl, added semicolon support
    while ( Serial.available() > 0 ) {
      c = Serial.read();
      if (( c == '\n') || (c == '\r') ) {             // End of line reached
        if ( lineIndex > 0 ) {                        // Line is complete. Then execute!
          line[ lineIndex ] = '\0';                   // Terminate string
          if (verbose) {
            Serial.print( "Received : ");
            Serial.println( line );
          }
          processIncomingLine( line, lineIndex );
          lineIndex = 0;
        }
        else {
          // Empty or comment line. Skip block.
        }
        lineIsComment = false;
        lineSemiColon = false;
        Serial.println("ok");
      }
      else {
        if ( (lineIsComment) || (lineSemiColon) ) {   // Throw away all comment characters
          if ( c == ')' )  lineIsComment = false;     // End of comment. Resume line.
        }
        else {
          if ( c <= ' ' ) {                           // Throw away whitepace and control characters
          }
          else if ( c == '/' ) {                    // Block delete not supported. Ignore character.
          }
          else if ( c == '(' ) {                    // Enable comments flag and ignore all characters until ')' or EOL.
            lineIsComment = true;
          }
          else if ( c == ';' ) {
            lineSemiColon = true;
          }
          else if ( lineIndex >= LINE_BUFFER_LENGTH - 1 ) {
            Serial.println( "ERROR - lineBuffer overflow" );
            lineIsComment = false;
            lineSemiColon = false;
          }
          else if ( c >= 'a' && c <= 'z' ) {        // Upcase lowercase
            line[ lineIndex++ ] = c - 'a' + 'A';
          }
          else {
            line[ lineIndex++ ] = c;
          }
        }
      }
    }
  }
}

void processIncomingLine( char* line, int charNB ) {
  int currentIndex = 0;
  char buffer[ 64 ];                                 // Hope that 64 is enough for 1 parameter
  struct point newPos;


  newPos.x = 0.0;
  newPos.y = 0.0;

  //  Needs to interpret
  //  G0 for moving - no pen
  //  G1 for moving - with pen
  //  G4 P300 (wait 150ms)
  //  G4 S30 (wait 30s)
  //  G1 X60 Y30
  //  G1 X30 Y50
  //  m3 (pen down)
  //  M3 S30 (pen down)
  //  M3 S50 (pen up)
  //  M5 (pen up)
  //  M5 S30 (pen down)
  //  M5 S50 (pen up)
  //  Discard anything with a (
  //  Discard any other command!

  while ( currentIndex < charNB ) {
    switch ( line[ currentIndex++ ] ) {              // Select command, if any
      case 'G':
        {
          switch (GetInt(line, currentIndex)) {         // Select G command
            case 0:                                     // G00 & G01 - Movement or fast movement. Same here
            case 1:
            {
              newPos.x = actuatorPos.x;
              newPos.y = actuatorPos.y;
              do
              {
                switch (GetChar(line, currentIndex))
                {
                  case 'X':
                    {
                      newPos.x = GetFloat(line, currentIndex);
                      break;
                    }
                  case 'Y':
                    {
                      newPos.y = GetFloat(line, currentIndex);
                      break;
                    }
                  default:
                    {
                      break;
                    }
                }
              } while (currentIndex < charNB);

              if (verbose) {
                Serial.print( "From position : X=" );
                Serial.print( actuatorPos.x );
                Serial.print( ",Y=" );
                Serial.println( actuatorPos.y );

                Serial.print( "To position : X=" );
                Serial.print( newPos.x );
                Serial.print( ",Y=" );
                Serial.println( newPos.y );
              }

              drawLine(actuatorPos.x, actuatorPos.y, newPos.x, newPos.y );
              //        Serial.println("ok");
              actuatorPos.x = newPos.x;
              actuatorPos.y = newPos.y;
              break;
            }
            case 4: // G04 - Dwel time in miliseconds or seconds
            {
              do
              {
                switch (GetChar(line, currentIndex))
                {
                  case 'P':
                    {
                      Dwell(GetInt(line, currentIndex));
                      break;
                    }
                  case 'S':
                    {
                      Dwell(GetInt(line, currentIndex)*1000);
                      break;
                    }
                }
              } while (currentIndex < charNB);
            }
          }
          break;
        }
      case 'M':
        {
          switch (GetInt(line, currentIndex)) {         // Select M command
            case 17: // Enable stepper
              {
                StepperEnable();
                break;
              }
            case 18: // Disable stepper
              {
                StepperDisable();
                break;
              }
            case 3:
              {
                do
                {
                  switch (GetChar(line, currentIndex))
                  {
                    case 'S':
                      {
                        float Spos = GetInt(line, currentIndex);
                        if (Spos == 30) {
                          PenDown();
                        }
                        if (Spos == 50) {
                          PenUp();
                        }
                        break;
                      }
                    default:
                    {
                      PenDown();
                    }
                  }
                } while (currentIndex < charNB);
                break;
              }
            case 5:
              {
                do
                {
                  switch (GetChar(line, currentIndex))
                  {
                    case 'S':
                      {
                        float Spos = GetInt(line, currentIndex);
                        if (Spos == 30) {
                          PenDown();
                        }
                        if (Spos == 50) {
                          PenUp();
                        }
                        break;
                      }
                     default:
                     {
                      PenUp();
                     }
                  }
                } while (currentIndex < charNB);
                break;
              }
            case 114:                                // M114 - Repport position
              {
                Serial.print( "Absolute position : X = " );
                Serial.print( actuatorPos.x );
                Serial.print( ", Y = " );
                Serial.println( actuatorPos.y );
                break;
              }
            default:
              {
                Serial.print( "Command not recognized : M");
                Serial.println( buffer );
              }
          }
        }
    }
  }
}


/*********************************
   Draw a line from (x0;y0) to (x1;y1).
   Bresenham algo from https://www.marginallyclever.com/blog/2013/08/how-to-build-an-2-axis-arduino-cnc-gcode-interpreter/
 **********************************/
void drawLine(float x0, float y0, float x1, float y1) {

  if (verbose)
  {
    Serial.print("x0, y0: ");
    Serial.print(x0);
    Serial.print(",");
    Serial.print(y0);
    Serial.print(" mm. x1, y1: ");
    Serial.print(x1);
    Serial.print(",");
    Serial.print(y1);
    Serial.println(" mm.");
  }

  //  Bring instructions within limits
  if (x1 >= Xmax) {
    x1 = Xmax;
  }
  if (x1 <= Xmin) {
    x1 = Xmin;
  }
  if (y1 >= Ymax) {
    y1 = Ymax;
  }
  if (y1 <= Ymin) {
    y1 = Ymin;
  }

  if (verbose)
  {
    Serial.print("x0, y0: ");
    Serial.print(x0);
    Serial.print(",");
    Serial.print(y0);
    Serial.print(" mm. x1, y1: ");
    Serial.print(x1);
    Serial.print(",");
    Serial.print(y1);
    Serial.println(" mm.");
  }

  //  Convert coordinates to steps
  x1 = (int)(x1 * StepsPerMillimeterX);
  y1 = (int)(y1 * StepsPerMillimeterY);
  x0 = (int)(x0 * StepsPerMillimeterX);
  y0 = (int)(y0 * StepsPerMillimeterY);

  // Could leave Xpos and Ypos being in steps and make this clear
  // this might be ok
  // at the moment revise this to pass both x0, y0


  //  Let's find out the change for the coordinates
  long dx = abs(x1 - x0);
  long dy = abs(y1 - y0);
  int sx = x0 < x1 ? StepInc : -StepInc;
  int sy = y0 < y1 ? StepInc : -StepInc;

  long i;
  long over = 0;

  StepperEnable();
  if (verbose)
  {
    Serial.println("Draw line");
  }
  if (dx > dy) {
    for (i = 0; i < dx; ++i) {
      myStepperX.step(sx);
      over += dy;
      if (over >= dx) {
        over -= dx;
        myStepperY.step(sy);
      }
      Delay(StepDelay);
    }
  }
  else {
    for (i = 0; i < dy; ++i) {
      myStepperY.step(sy);
      over += dx;
      if (over >= dy) {
        over -= dy;
        myStepperX.step(sx);
      }
      Delay(StepDelay);
    }
  }
  StepperDisable();

  if (verbose)
  {
    Serial.print("dx, dy:");
    Serial.print(dx);
    Serial.print(",");
    Serial.print(dy);
    Serial.println(" steps.");
  }

  if (verbose)
  {
    Serial.print("x0, y0: ");
    Serial.print(x0);
    Serial.print(",");
    Serial.print(y0);
    Serial.print(" steps. x1, y1: ");
    Serial.print(x1);
    Serial.print(",");
    Serial.print(y1);
    Serial.println(" steps.");
  }

  //  Delay before any next lines are submitted
  Delay(LineDelay);
  //  Update the position (in steps)
  Xpos = x1;
  Ypos = y1;
}

//  Raises pen
void PenUp() {
  penServo.write(penZUp);
  Delay(LineDelay);
  Zpos = (int)(Zmax * StepsPerMillimeterZ);
  if (verbose) {
    Serial.println("Pen up!");
  }
}
//  Lowers pen
void PenDown() {
  penServo.write(penZDown);
  Delay(LineDelay);
  Zpos = (int)(Zmin * StepsPerMillimeterZ);
  if (verbose) {
    Serial.println("Pen down.");
  }
}

// Enables stepper
void StepperEnable() {
  myStepperX.enable(true);
  myStepperY.enable(true);
  Delay(LineDelay);
  if (verbose) {
    Serial.println("Stepper enable.");
  }
}

// Disables stepper
void StepperDisable() {
  myStepperX.enable(false);
  myStepperY.enable(false);
  Delay(LineDelay);
  if (verbose) {
    Serial.println("Stepper disable.");
  }
}

// Override Delay so that is uses mills

void Delay(unsigned long ms) {
  unsigned long currentMillis = millis();
  do {
  } while ((millis() - currentMillis) < ms);
}

void Dwell(unsigned long ms) {
  unsigned long currentMillis = millis();
  do {
  } while ((millis() - currentMillis) < ms);
  if (verbose) {
    Serial.print("Dwell ");
    Serial.println(ms);
  }
}

//

float GetFloat(char *line, int &index) {
  char buffer[64];
  float data;
  char c;
  int i;
  for (i = 0; i < 64; i++)
  {
    c = line[index + i];
    if (((c <= '9') && (c >= '0')) || (c == '.')) {
      buffer[i] = c;
    }
    else {
      buffer[i] = '\0';
      break;
    }
  }
  index = index + i;
  data = atof(buffer);
//  if (verbose == true)
//  {
//    Serial.print("float=");
//    Serial.println(data);
//  }
  return (data);
}

int GetInt(char *line, int &index) {
  char buffer[64];
  int data;
  char c;
  int i;
  for (i = 0; i < 64; i++)
  {
    c = line[index + i];
    if ((c <= '9') && (c >= '0')) {
      buffer[i] = c;
    }
    else {
      buffer[i] = '\0';
      break;
    }
  }
  index = index + i;
  data = atoi(buffer);
//  if (verbose == true)
//  {
//    Serial.print("int=");
//    Serial.println(data);
//  }
  return (data);
}

char GetChar(char *line, int &index) {
  char buffer[64];
  int data;
  char c;
  int i;
  for (i = 0; i < 64; i++)
  {
    c = line[index + i];
    if ((c <= 'Z') && (c >= 'A')) {
      buffer[i] = c;
    }
    else {
      buffer[i] = '\0';
      break;
    }
  }
  index = index + 1;
//  if (verbose == true)
//  {
//    Serial.print("char=");
//    Serial.println(buffer);
//  }
  return (buffer[0]);
}

/*


  int GetInt(char *line, int **index){
  char buffer[64];
  char c;
  int i;
  for (int i = 0; i<64; i++)
  {
    c = line[*index + i];
    Serial.println(c);
    if (((c <= '9') && (c >= '0')) || (c == '.')) {
      buffer[i] = c;
    }
    else {
      buffer[i] = '\0';
      break;
    }
  }
   index = i;
  return(atoi(buffer));
  }
*/

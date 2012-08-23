/*
ReflectaFramesSerial.cpp - Library for sending frames of information from a Microcontroller to a PC over a serial port.
*/

#include "ReflectaFramesSerial.h"

// SLIP (http://www.ietf.org/rfc/rfc1055.txt) protocol special character definitions
// Used to find end of frame when using a streaming communications protocol
#define END            0xC0
#define ESCAPE         0xDB
#define ESCAPED_END    0xDC
#define ESCAPED_ESCAPE 0xDD

// State machine for incoming data.  Packet format is:
// Frame Sequence #, SLIP escaped
// Byte(s) of Payload, SLIP escaped
// CRC8 of Sequence # & Payload bytes, SLIP escaped
// SLIP END (0xc0)
#define WAITING_FOR_SEQUENCE 0 // Beginning of a new frame, waiting for the Sequence number
#define WAITING_FOR_BYTECODE 1 // Reading data until an END character is found
#define PROCESS_PAYLOAD      2 // END character found, check CRC and deliver frame
#define WAITING_FOR_RECOVERY 3 // Current frame is invalid, wait for an END character and start parsing again

namespace reflectaFrames
{
  // Checksum for the incoming frame, calculated byte by byte using XOR.  Compared against the checksum byte
  // which is stored in the last byte of the frame.
  byte readChecksum = 0;
  
  // Checksum for the outgoing frame, calculated byte by byte using XOR.  Added to the payload as the last byte of the frame. 
  byte writeChecksum = 0;
  
  // Sequence number of the incoming frames.  Compared against the sequence number at the beginning of the incoming frame
  //  to detect out of sequence frames which would point towards lost data or corrupted data.
  byte nextSequence = 0;
  
  // Sequence number of the outgoing frames.
  byte writeSequence = 0;
  
  // protocol parser escape state -- set when the ESC character is detected so the next character will be de-escaped
  int escaped = 0;
  
  // protocol parser state
  int state = WAITING_FOR_SEQUENCE;
  
  frameBufferAllocationFunction frameBufferAllocationCallback = NULL;
  frameReceivedFunction frameReceivedCallback = NULL;
  
  void setFrameReceivedCallback(frameReceivedFunction frameReceived)
  {
    frameReceivedCallback = frameReceived;
  }
  
  void setBufferAllocationCallback(frameBufferAllocationFunction frameBufferAllocation)
  {
    frameBufferAllocationCallback = frameBufferAllocation;
  }
  
  void writeEscaped(byte b)
  {
    switch(b)
    {
      case END:
        Serial.write(ESCAPE);
        Serial.write(ESCAPED_END);
        break;
      case ESCAPE:
        Serial.write(ESCAPE);
        Serial.write(ESCAPED_ESCAPE);
        break;
      default:
        Serial.write(b);
        break;
    }
    writeChecksum ^= b;
  }
  
  byte sendFrame(byte* frame, byte frameLength)
  {
    writeChecksum = 0;
    writeEscaped(writeSequence);
    for (byte index = 0; index < frameLength; index++)
    {
      writeEscaped(frame[index]);
    }
    writeEscaped(writeChecksum);
    Serial.write(END);

    // On Teensies, use the extended send_now to perform an undelayed send
    #ifdef USBserial_h_
    Serial.send_now();
    #endif
    
    return writeSequence++;
  }
  
  void sendError(byte errorCode)
  {
    byte buffer[2];
    buffer[0] = FRAMES_ERROR;
    buffer[1] = errorCode;
    sendFrame(buffer, 2);
  }
  
  void sendWarning(byte warningCode)
  {
    byte buffer[2];
    buffer[0] = FRAMES_WARNING;
    buffer[1] = warningCode;
    sendFrame(buffer, 2);
  }
  
  void sendMessage(String message)
  {
    byte bufferLength = message.length() + 3;
    byte buffer[bufferLength];
    
    buffer[0] = FRAMES_MESSAGE;
    buffer[1] = message.length();
    message.getBytes(buffer + 2, bufferLength - 2);
    
    // Strip off the trailing '\0' that Arduino String.getBytes insists on postpending
    sendFrame(buffer, bufferLength - 1);
  }
  
  int readUnescaped(byte &b)
  {
    b = Serial.read();
    
    if (escaped)
    {
      switch (b)
      {
        case ESCAPED_END:
          b = END;
          break;
        case ESCAPED_ESCAPE:
          b = ESCAPE;
          break;
        default:
          sendWarning(FRAMES_WARNING_UNEXPECTED_ESCAPE);
          state = WAITING_FOR_RECOVERY;
          break;
      }
      escaped = 0;
      readChecksum ^= b;
    }
    else
    {
      if (b == ESCAPE)
      {
        escaped = 1;
        return 0; // read escaped value on next pass
      }
      if (b == END)
      {
        switch (state)
        {
          case WAITING_FOR_RECOVERY:
            readChecksum = 0;
            state = WAITING_FOR_SEQUENCE;
            break;
          case WAITING_FOR_BYTECODE:
            state = PROCESS_PAYLOAD;
            break;
          default:
            sendWarning(FRAMES_WARNING_UNEXPECTED_END);
            state = WAITING_FOR_RECOVERY;
            break;
        }
      }
      else
      {
        readChecksum ^= b;
      }
    }
    
    return 1;
  }
  
  const byte frameBufferSourceLength = 64;
  byte* frameBufferSource = NULL;
  
  // Default frame buffer allocator for when caller does not set one.
  byte frameBufferAllocation(byte** frameBuffer)
  {
    *frameBuffer = frameBufferSource;
    return frameBufferSourceLength;
  }
  
  void reset()
  {
    nextSequence = 0;
    writeSequence = 0;
    Serial.flush();
  }
  
  void setup(int speed)
  {
    if (frameBufferAllocationCallback == NULL)
    {
      frameBufferSource = (byte*)malloc(frameBufferSourceLength);
      frameBufferAllocationCallback = frameBufferAllocation;
    }
    
    Serial.begin(speed);
    Serial.flush();
  }
  
  byte* frameBuffer;
  byte frameBufferLength;
  byte frameIndex = 0;

  byte sequence;

  uint32_t lastFrameReceived;
  
  void loop()
  {
    byte b;
    
    while (Serial.available())
    {
      if (readUnescaped(b))
      {
        switch (state)
        {
          case WAITING_FOR_RECOVERY:
            break;
            
          case WAITING_FOR_SEQUENCE:
            sequence = b;
            if (nextSequence++ != sequence)
            {
              sendMessage("Expected " + String(nextSequence - 1, HEX) + " received " + String(sequence, HEX) );
              nextSequence = sequence + 1;
              sendWarning(FRAMES_WARNING_OUT_OF_SEQUENCE);
            }
            
            frameBufferLength = frameBufferAllocationCallback(&frameBuffer);
            frameIndex = 0; // Reset the buffer pointer to beginning
            readChecksum = sequence;
            state = WAITING_FOR_BYTECODE;
            break;
            
          case WAITING_FOR_BYTECODE:
            if (frameIndex == frameBufferLength)
            {
              sendError(FRAMES_ERROR_BUFFER_OVERFLOW);
              state = WAITING_FOR_RECOVERY;
              readChecksum = 0;
            }
            else
            {
              frameBuffer[frameIndex++] = b;
            }
            break;
            
          case PROCESS_PAYLOAD:
            lastFrameReceived = millis();
            if (readChecksum == 0) // zero expected because finally XOR'd with itself
            {
              // TODO: add a MessageReceived callback too
              
              if (frameReceivedCallback != NULL)
              {
                frameReceivedCallback(sequence, frameIndex - 1, frameBuffer);
              }
            }
            else
            {
              sendWarning(FRAMES_WARNING_CRC_MISMATCH);
              state = WAITING_FOR_RECOVERY;
              readChecksum = 0;
            }
            state = WAITING_FOR_SEQUENCE;
            break;
        }
      }
    }
  }
}
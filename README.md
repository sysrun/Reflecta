__Reflecta__ is a communications library for Arduino that enables remote calling of functions, sending & receiving frames of byte data or string messages, and receiving high speed 'heartbeats' of sensor data.

# Why? #

Arduino easily connects to a PC over USB as a Virtual COM port but turning this serial data stream into a reliable communications channel takes considerable work.  Firmata is a comparable library but it has some limitations:

- Firmata is based on MIDI and uses a '7 bit data' design.  The 8th bit is reserved for 'commands' so all raw data sent over the wire has to be converted to/from 7 bits.

- Firmata is difficult to extend with new methods due to its SYSEX design and 7-bitness.

- Firmata doesn't detect corrupt or lost data.  Testing using USB Virtual COM with two-chip (USB chip -> UART -> MCU chip) boards like the UNO detected frequent data corruption at speeds over 9600 baud.  RF communications like Bluetooth or Zigbee need to be able to detect data loss and corruption in order to modify their sending behavior based on changing physical conditions such as antenna blockage or distance.

The goal is to develop a protocol that fixes these issues, specifically:

- Use 'escaping' to delineate frames so we don't need to convert payload data to 7 bits.

- Add Sequence and Checksum to detect data loss and corruption.

- Make it easy to extend the protocol with new functions (bind) and discover what functions are implemented (queryinterface).  Minimize the amount of work needed for a library to be exposed for remote calling.

- Design for CPU and communications efficiency in order to take best advantage of limited microcontroller resources.

# Getting Started #

To install, download the contents of the repository as a zip file and decompress into your ['sketchbook' folder](http://arduino.cc/it/Reference/Libraries) (on Windows this is My Documents -> arduino) in a subdirectory named 'libraries'.  When you are done you should have a file like My Documents\arduino\libraries\ReflectaFramesSerial\ReflectaFramesSerial.h.  Now you can restart the Arduino IDE and use Sketch -> Import Library.

__Examples__

[BasicReflecta](https://github.com/JayBeavers/Reflecta/blob/master/BasicReflecta/BasicReflecta.ino) -- opens a Reflecta listener at 9600 baud and exposes the Arduino Core functions to be called.  This is the 'Arduino side' of the conversation.

[NodeClient](https://github.com/JayBeavers/Reflecta/tree/master/NodeClient) -- a [NodeJS](http://nodejs.org/) library that will talk to BasicReflecta and call Arduino functions such as digitalWrite.  See [simple.js sample](https://github.com/JayBeavers/Reflecta/blob/master/NodeClient/samples/simple.js) as an example.

# How? #

Reflecta is four Arduino libraries and one NodeJS client:

- [ReflectaFramesSerial](https://github.com/JayBeavers/Reflecta/tree/master/ReflectaFramesSerial) packages byte[] data into frames over a stream, adds Sequence to detect lost frames, and adds Checksum to detect data corruption. Uses the Arduino Serial library for communications, a future Raw Hid implementation is planned.

- [ReflectaFunctions](https://github.com/JayBeavers/Reflecta/tree/master/ReflectaFunctions) is a remote function calling protocol that builds on top of ReflectaFrames.  Arduino functions are registered by calling _bind(interfaceId, function pointer)_.

  ReflectaFunctions uses a stack-based approach to calling functions. push() parameters on the stack, invoke function(s), functions pop() their parameters off the stack and push() their return values back on.

  ReflectaFunctions exposes bind() and queryInterface() to determine which interfaces (e.g. function groups) are on the Arduino.

- [ReflectaArduinoCore](https://github.com/JayBeavers/Reflecta/tree/master/ReflectaArduinoCore) is a binding of the Arduino core library functions such as pinMode, digitalRead, analogWrite to the 'ARDU1' interface.

- [ReflectaHeartbeat](https://github.com/JayBeavers/Reflecta/tree/master/ReflectaHeartbeat) is a library for reading digital and analog pins very efficienctly, calling functions and gathering their results into a data packet, and sending the results at a fixed frequency to the host PC.  ReflectaHeartbeat is optimized to quickly gather data off the Arduino while effectively sharing the CPU by using asynchronous polling inside loop() rather than delay().

- [NodeClient](https://github.com/JayBeavers/Reflecta/tree/master/NodeClient) is a NodeJS library built on top of node-serialport.  NodeClient uses ReflectaFunctions to queryInterface and dynamically load javascript objects for the Arduino libraries.

---

## Design ##

After reviewing existing technologies, the approach settled on is:

- Start with the [STK500 protocol](http://www.atmel.com/Images/doc2591.pdf) from Atmel which has MESSAGE\_START, SEQUENCE\_NUMBER, and a simple 8 bit xor CHECKSUM.
- STK500 doesn't escape the data, so substitute [SLIP framing](http://www.ietf.org/rfc/rfc1055.txt) for the STK500 message start/message size design.  SLIP is very simple to understand and code.

### [SLIP](http://www.ietf.org/rfc/rfc1055.txt), (e.g. Serial Line IP) In A Nutshell ###

SLIP defines two special characters, __END__ (0xC0) and __ESCAPE__ (0xDB), that must be escaped if they are found in the frame of payload data.  The pseudocode for slip encoding is:

    for each byte in payload[]

        if byte == ESCAPE (0xDB) write ESCAPE (0xDB) + ESCAPED_ESCAPE (0xDD)

        else if byte == END (0xC0) write ESCAPE (0xDB) + ESCAPED_END (0xDC)

        else write byte

    write END (0xC0)

### Frame Layout ###

Over the wire, a frame of data looks like:

    Sequence Payload[] Checksum END

__Sequence__ is a byte that increments on each frame sent and rolls over from 255 back to 0.

__Payload[]__ is the data bytes you want to transfer.

__Checksum__ is calculated using XOR ( ^= ) on each unescaped byte of Sequence and Payload[].  Checksum is validated by calling XOR on each byte of the incoming frame such that when the END character is reached, the current value of the calculated checksum should be zero because the frame's checksum just XORed with itself.

This compares well to STK500 and Firmata in efficiency (only 3 byte overhead per message) and ease of calculation.  The length of payload is inferred from the length between END characters rather than encoded into the frame.
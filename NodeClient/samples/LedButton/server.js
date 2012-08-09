// Local settings, change to suit
var portName = 'COM4'; // See node-serialport for documentation
var httpPort = 8088;
var ledPin = 11;

var app = require('http').createServer(handler)
  , io = require('socket.io').listen(app)
  , fs = require('fs')
  , util = require('util')
  , Reflecta = require('../../reflecta.js')

// static webserver
function handler (req, res) {
  fs.readFile(__dirname + '/index.html',
    function (err, data) {
      if (err) {
        res.writeHead(500);
        return res.end('Error loading index.html');
      }

      res.writeHead(200);
      res.end(data);
    });
}

// Open a connection to the arduino and callback when port is opened
var reflecta = new Reflecta(portName, function(err) {
  if (err) {
    reflecta.close(function() { done(err); });
    return;
  }

  reflecta.on('error', function(err, frame, checksum) { console.log(err + ' - ' + util.inspect(frame) + ' - ' + checksum); });

  // Wait until Arduino port is opened to listen for incoming connections  
  app.listen(httpPort);

  // Listen for incoming socketio messages
  io.sockets.on('connection', function (socket) {

    var ledState = 0;    
    socket.on('toggle', function() {
      console.log('Received (toggle)')
      ledState ^= 1;
      reflecta.ardu1.gpio.digitalWrite(ledPin, ledState);
    });

  });
});
#!/usr/bin/env ruby

require 'serialport'

dev = SerialPort.new("/dev/ttyUSB0", 19200)

# sync
dev.write "\e\e\e\e\e\e\e\e\e\x00"
dev.flush
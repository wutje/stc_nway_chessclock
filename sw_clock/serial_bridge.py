#!/usr/bin/env python3
import serial_asyncio
import sys, asyncio, aiofiles
from functools import partial
import signal

BAUD=9600

PORT = sys.argv[1]
FN_IN = sys.argv[2]
FN_OUT = sys.argv[3]

SYNC_BYTE = b's' ## from uart.c
MSG_LEN = 5 ## inferred from uart.c

print(f"Opening {FN_IN} for reading")
PIPEIN = aiofiles.open(FN_IN, 'rb')
print(f"Opening {FN_OUT} for writing")
PIPEOUT = open(FN_OUT, 'wb')

snooper = None;

class Accumulator:
    def __init__(self, msglen, name):
        self.msglen = msglen
        self.i = 0;
        self.bytes = [0]*msglen
        self.name = name
    def add(self, b):
        self.bytes[self.i] = b
        if self.i != 0 or b == SYNC_BYTE:
            self.i += 1
        if self.i == self.msglen:
            msg = b''.join(self.bytes)
            hx = msg.hex(' ')
            sys.stderr.write(f"{self.name}: {msg} ({hx})\n")
            self.i = 0

async def read_pipe():
    async with PIPEIN as f:
        accu = Accumulator(msglen=MSG_LEN, name = 'Pipe in')
        while True:
            try:
                b = await f.read(1)
            except asyncio.exceptions.CancelledError:
                print("pipe in cancelled")
                return
            if len(b) == 0:
                print("EOF")
                return
            accu.add(b)
            print(f"PIPEIN {b}")
            snooper.write(b)

class SerialSnoop(asyncio.Protocol):
    def __init__(self, pipe_task):
        super().__init__()
        self.pipe_task = pipe_task
        self.accu = Accumulator(msglen=MSG_LEN, name = 'Serial in')

    def connection_made(self, transport):
        global snooper
        snooper = self
        self.transport = transport
        transport.serial.rts = False

    def data_received(self, data):
        print(f"SERIALIN {data}")
        self.accu.add(data)
        try:
            PIPEOUT.write(data)
            PIPEOUT.flush()
        except BrokenPipeError:
            print("output pipe closed")
            self.transport.close()
            self.pipe_task.cancel()

    def connection_lost(self, exc):
        print("serial connection closed")
        self.pipe_task.cancel()

    def write(self, b):
        self.transport.write(b)

async def main():
    loop = asyncio.get_event_loop()
    pipe_task = asyncio.create_task(read_pipe())
    print(f"Opening {PORT} for reading/writing")
    coro = serial_asyncio.create_serial_connection(loop, partial(SerialSnoop, pipe_task), PORT, baudrate=BAUD)
    serial_task = asyncio.create_task(coro)
    print("starting")
    await asyncio.gather(serial_task, pipe_task)

signal.signal(signal.SIGINT, signal.SIG_DFL)
asyncio.run(main())

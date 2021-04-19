import serial
import sys

s = serial.Serial(sys.argv[1], 9600, timeout = 1)

def m(v):
    if v >= 65 and v <= 122:
        return chr(v)
    else:
        return int(v)

s.reset_input_buffer()
while True:
    b = s.read(6)
    if len(b) == 0:
        continue;
    elif len(b) != 6:
        print("TRUNC")
    l = map(m, b)
    print(list(l))

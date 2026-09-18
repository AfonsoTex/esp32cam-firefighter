This directory contains a copy of the public-domain lgpio 0.2.2 header for Windows IntelliSense and syntax checks only.
Source: https://github.com/joan2937/lg/blob/master/lgpio.h
The unused Linux gpio.h include is guarded on Windows; all library declarations are unchanged.
This is not a Windows implementation of lgpio. Build and link on the Raspberry Pi using its installed liblgpio-dev and -llgpio; do not copy this directory to the Pi.